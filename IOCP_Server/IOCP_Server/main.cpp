//
#include <iostream>
#include <memory>
#include <atomic>
#include <thread>
#include <chrono>
#include <condition_variable>
#include <mutex>

#include "GameServer.h"
#include "MapManager.h"
#include "MonitorManager.h"
#include "MonitorServer.h"
#include "ServerConfig.h"
#include "CoreAffinity.h"       // 게임스레드 코어 격리 마스크 주입
#include "../../Shared/Common/Logger.h"
#include "../../Shared/Common/ErrorLog.h"

std::atomic<bool> running{true};
std::mutex mtx;
std::condition_variable cv;

// main이 정리(server.Stop)를 끝냈는지 — 콘솔 종료 핸들러가 이걸 기다린다.
std::atomic<bool> shutdownComplete{false};

// CTRL_CLOSE/LOGOFF/SHUTDOWN에서 정리를 기다릴 상한.
//   이 이벤트들은 OS 유예(기본 5초) 후 프로세스를 강제 종료하므로 그보다 짧게 잡는다.
constexpr ULONGLONG SHUTDOWN_WAIT_MAX_MS = 4500;

// 프로세스 전체 종료 컨트롤러이므로 메인문에 빼둔다
//   running 변경은 반드시 mtx 안에서 — 락 없이 바꾸면 main이 wait 술어를 평가한 직후
//   대기 큐에 등록되기 전 구간에 끼어들어 notify가 유실된다(기상 유실).
void SignalProcessShutdown()
{
    {
        std::lock_guard<std::mutex> lk(mtx);
        running = false;
    }
    cv.notify_one();
}

// Ctrl+C / 콘솔 종료 → graceful shutdown 트리거 (server.Stop에서 DB 최종저장·드레인 수행)
BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType)
{
    switch (ctrlType)
    {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
        // 이 둘은 핸들러가 반환해도 프로세스가 계속 살아 있다 → main이 Stop()을 완주할 수 있다.
        SignalProcessShutdown();
        return TRUE;

    case CTRL_CLOSE_EVENT:
    case CTRL_SHUTDOWN_EVENT:
    case CTRL_LOGOFF_EVENT:
        // 이 셋은 핸들러가 반환하는 순간 OS가 프로세스를 종료한다.
        //   그냥 반환하면 main의 server.Stop()(SaveAllPlayers + DB 드레인)이 시작도 못 하고 잘려
        //   마지막 저장 주기 이후의 플레이어 위치가 통째로 유실된다.
        //   → 정리가 끝날 때까지 여기서 버틴다. 유예를 넘기면 어차피 OS가 죽이므로 포기하고 반환.
        SignalProcessShutdown();
        {
            const ULONGLONG deadline = GetTickCount64() + SHUTDOWN_WAIT_MAX_MS;
            while (!shutdownComplete.load(std::memory_order_acquire))
            {
                if (GetTickCount64() >= deadline)
                    break;
                Sleep(20);
            }
        }
        return TRUE;

    default:
        return FALSE;
    }
}

int main()
{
    // 로거 초기화 (RAII — 소멸자에서 Shutdown)
    shared::LoggerGuard loggerGuard;

    // INI 설정 파일 로드
    ServerConfig config;
    config.Load();

    // CPU 코어 핀(affinity) — Init/Start 전에 걸어야 이후 생성되는
    // worker/send/accept/gameloop 스레드가 전부 이 마스크를 상속한다.
    if (config.affinityMask != 0)
    {
        if (SetProcessAffinityMask(GetCurrentProcess(), static_cast<DWORD_PTR>(config.affinityMask)))
            SLOG_INFO("[Affinity] ProcessAffinityMask = 0x{:X}", config.affinityMask);
        else
            SLOG_WARN("[Affinity] SetProcessAffinityMask failed: {}", GetLastError());
    }

    // 게임스레드 코어 격리 마스크 주입 — 프로세스 affinity 직후, 스레드 생성(Init/Start/monitor) 전에 1회.
    //   config가 GameCore INI에서 도출한다(격리 off면 0,0). 이후 각 스레드가 진입부에서 자가-핀.
    CoreAffinity::SetIsolationMasks(config.gameCoreMask, config.ioCoreMask);
    if (config.gameCoreMask != 0)
        SLOG_INFO("[Affinity] CoreIsolation ON — game=0x{:X} io=0x{:X}",
                  config.gameCoreMask, config.ioCoreMask);

    SLOG_INFO("=== IOCP MMO Server ===");
    SLOG_INFO("Port: {}", config.port);
    SLOG_INFO("Max Clients: {}", config.maxClients);

    CMonitorManager monitor;
    std::unique_ptr<CMonitorServer> monitorSvr;
    if (config.monitorEnabled)
        monitorSvr = std::make_unique<CMonitorServer>(monitor, config.monitorPort);

    CGameServer server(monitor);

    if (!server.Init(config.mode, config.port, config.maxClients,
                     config.maps.data(), static_cast<int32_t>(config.maps.size()),
                     config.workerThreads, config.sendWorkers, config.rioWorkers))
    {
        SLOG_ERROR("[Error] Server Init failed");
        return 1;
    }

    // DB 저장 파이프라인 초기화 (USE_DB_WORKER=0이면 no-op)
    {
        DBConfig dbConfig;
        dbConfig.host              = config.dbHost;               // DB 서버 주소 (기본 127.0.0.1)
        dbConfig.port              = config.dbPort;               // DB 포트 (MySQL 기본 3306)
        dbConfig.user              = config.dbUser;               // 접속 계정
        dbConfig.password          = config.dbPassword;           // 접속 비밀번호
        dbConfig.database          = config.dbDatabase;           // 사용할 스키마명 (기본 gamedb)
        dbConfig.connectTimeoutSec = config.dbConnectTimeoutSec;  // 접속 시도 타임아웃(초) — 이 시간 안에 연결 못 하면 실패 반환
        dbConfig.rwTimeoutSec      = config.dbRwTimeoutSec;       // 읽기/쓰기 소켓 타임아웃(초) — DB 무응답 시 쿼리 무한대기 방지
        if (!server.InitDB(dbConfig, config.dbSavePeriodSec, config.dbWorkers, config.dbQueueMax))
        {
            SLOG_ERROR("[Error] DB init failed");
            return 1;
        }
    }

    // Ctrl+C / 콘솔 종료 시 graceful shutdown (server.Stop이 DB 드레인 수행)
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

    if (!server.Start())
    {
        SLOG_ERROR("[Error] Server Start failed");
        return 1;
    }

    if (monitorSvr) monitorSvr->Start();

    // main 스레드는 condition_variable로 대기
    {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&] { return !running; });
    }

    if (monitorSvr) monitorSvr->Stop();
    server.Stop();

    SLOG_INFO("Server shutdown complete");
    shutdownComplete.store(true, std::memory_order_release);   // 콘솔 종료 핸들러 해제
    return 0;
}
