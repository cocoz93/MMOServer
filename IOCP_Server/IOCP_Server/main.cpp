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
#include "../../Shared/Common/Logger.h"
#include "../../Shared/Common/ErrorLog.h"

std::atomic<bool> running{true};
std::mutex mtx;
std::condition_variable cv;

// 프로세스 전체 종료 컨트롤러이므로 메인문에 빼둔다
void SignalProcessShutdown()
{
    running = false;
    cv.notify_one();
}

// Ctrl+C / 콘솔 종료 → graceful shutdown 트리거 (server.Stop에서 DB 최종저장·드레인 수행)
BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType)
{
    switch (ctrlType)
    {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_SHUTDOWN_EVENT:
    case CTRL_LOGOFF_EVENT:
        SignalProcessShutdown();
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
                     config.workerThreads, config.sendWorkers))
    {
        SLOG_ERROR("[Error] Server Init failed");
        return 1;
    }

    // DB 저장 파이프라인 초기화 (USE_DB_WORKER=0이면 no-op)
    {
        DBConfig dbConfig;
        dbConfig.host              = config.dbHost;
        dbConfig.port              = config.dbPort;
        dbConfig.user              = config.dbUser;
        dbConfig.password          = config.dbPassword;
        dbConfig.database          = config.dbDatabase;
        dbConfig.connectTimeoutSec = config.dbConnectTimeoutSec;
        dbConfig.rwTimeoutSec      = config.dbRwTimeoutSec;
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
    return 0;
}
