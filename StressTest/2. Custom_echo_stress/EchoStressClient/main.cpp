#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include <cstdio>
#include <conio.h>
#include <io.h>
#include <fcntl.h>
#include <ctime>
#include <string>
#include <atomic>
#include "Config.h"
#include "DummyManager.h"
#include "StressMonitorServer.h"

#pragma comment(lib, "ws2_32.lib")

// ─────────────────────────────────────────────────────────────────
// 콘솔 종료 핸들러 — q 입력 외에 창 X 클릭(CTRL_CLOSE) / Ctrl+C 에서도
// 정상 종료 경로를 타도록 종료 플래그를 세운다.
//   CLOSE/LOGOFF/SHUTDOWN 이벤트는 핸들러가 "반환"하는 즉시 OS가 프로세스를
//   죽이므로, 메인 스레드가 리포트 저장을 끝낼 때까지 여기서 대기한다.
//   (taskkill /F 강제 종료는 핸들러를 거치지 않으므로 보호 불가)
// ─────────────────────────────────────────────────────────────────
static std::atomic<bool> g_shutdownRequested{ false };
static std::atomic<bool> g_reportDone{ false };

static BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType)
{
    switch (ctrlType)
    {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:    // 창 X 클릭
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        g_shutdownRequested.store(true, std::memory_order_release);
        // 메인 스레드가 리포트를 저장할 때까지 대기 (최대 ~4.5초, CLOSE 유예 내)
        for (int i = 0; i < 4500 && !g_reportDone.load(std::memory_order_acquire); ++i)
            Sleep(1);
        return TRUE;
    default:
        return FALSE;
    }
}

// ─────────────────────────────────────────────────────────────────
// 진입점
// ─────────────────────────────────────────────────────────────────
int main(int argc, char* argv[])
{
    _setmode(_fileno(stdout), _O_U16TEXT);
    _setmode(_fileno(stderr), _O_U16TEXT);

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        wprintf(L"[Error] WSAStartup 실패\n");
        return 1;
    }

    // 창 X 클릭 / Ctrl+C 에서도 리포트를 남기도록 핸들러 등록
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

    Config cfg;
    cfg.Load(argc > 1 ? argv[1] : nullptr);

        wprintf(L"\n[Custom Echo Stress] start. Server=%hs:%d, Clients=%d\n",
            cfg.serverIp.c_str(), cfg.port, cfg.clientCount);
        wprintf(L"[Custom Echo Stress] Prometheus metrics on port %d\n\n",
            cfg.monitorPort);
    Sleep(1000);

    DummyManager         manager(cfg);
    StressMonitorServer  monitor([&manager]() { return manager.GetMergedStats(); },
                                 cfg.monitorPort);

    manager.Start();
    monitor.Start();

    // ── 종료 조건 대기 ──────────────────────────────────────────
    int64_t startMs = static_cast<int64_t>(GetTickCount64());

    while (!g_shutdownRequested.load(std::memory_order_acquire))
    {
        // 시간 제한 종료
        if (cfg.testDurationSec > 0)
        {
            int64_t elapsed = (static_cast<int64_t>(GetTickCount64()) - startMs) / 1000;
            if (elapsed >= cfg.testDurationSec) break;
        }

        // 키 입력 종료 (q 입력)
        if (_kbhit())
        {
            int ch = _getch();
            if (ch == 'q' || ch == 'Q') break;
        }

        Sleep(100);
    }

    // ── 종료 ────────────────────────────────────────────────────
    int64_t elapsedSec = (static_cast<int64_t>(GetTickCount64()) - startMs) / 1000;

    monitor.Stop();
    manager.Stop();

    // ── 최종 리포트 ─────────────────────────────────────────────
    MergedStats s = manager.GetMergedStats();
    int mm = static_cast<int>(elapsedSec / 60);
    int ss = static_cast<int>(elapsedSec % 60);

    int64_t samples = s.rttSamples;
    int64_t avgRtt  = (samples > 0) ? (s.rttSumMs / samples) : 0;
    int64_t minRtt  = (samples > 0) ? s.rttMinMs : 0;
    int64_t maxRtt  = s.rttMaxMs;

    int64_t avgTps = (elapsedSec > 0) ? (s.recvCount / elapsedSec) : 0;

    // 리포트 문자열 생성 (콘솔 + 파일 공용)
    wchar_t report[2048];
    swprintf_s(report, _countof(report),
        L"\n"
        L"============================================\n"
        L"  [Test Result]  Duration: %02d:%02d\n"
        L"============================================\n"
        L"  Server         : %hs:%d\n"
        L"  Clients        : %d\n"
        L"  PacketSize     : %d ~ %d B\n"
        L"============================================\n"
        L"  Connect Total  : %lld\n"
        L"  Connect Fail   : %lld\n"
        L"  Disconn (Svr)  : %lld\n"
        L"--------------------------------------------\n"
        L"  Send Total     : %lld\n"
        L"  Recv Total     : %lld\n"
        L"  Avg TPS (recv) : %lld/s\n"
        L"--------------------------------------------\n"
        L"  RTT avg        : %lldms\n"
        L"  RTT min        : %lldms\n"
        L"  RTT max        : %lldms\n"
        L"  RTT samples    : %lld\n"
        L"--------------------------------------------\n"
        L"  Echo Timeout   : %lld\n"
        L"  Packet Error   : %lld\n"
        L"  Late Arrival   : %lld\n"
        L"  SendBuf Full   : %lld\n"
        L"============================================\n",
        mm, ss,
        cfg.serverIp.c_str(), cfg.port,
        cfg.clientCount,
        cfg.minPacketSize, cfg.maxPacketSize,
        s.connectTotal,
        s.connectFail,
        s.disconnectFromServer,
        s.sendCount,
        s.recvCount,
        avgTps,
        avgRtt, minRtt, maxRtt, samples,
        s.echoNotRecv,
        s.packetError,
        s.lateArrival,
        s.sendBufferFull);

    // 콘솔 출력
    wprintf(L"%s", report);

    // 파일 출력 (exe 경로/Results/EchoStress_YYYYMMDD_HHMMSS.txt)
    {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        std::wstring dir(exePath);
        dir = dir.substr(0, dir.find_last_of(L"\\/") + 1) + L"Results";
        CreateDirectoryW(dir.c_str(), NULL);

        time_t now = time(nullptr);
        struct tm lt;
        localtime_s(&lt, &now);

        wchar_t fileName[MAX_PATH];
        swprintf_s(fileName, _countof(fileName),
            L"%s\\EchoStress_%04d%02d%02d_%02d%02d%02d.txt",
            dir.c_str(),
            lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday,
            lt.tm_hour, lt.tm_min, lt.tm_sec);

        FILE* fp = nullptr;
        _wfopen_s(&fp, fileName, L"w, ccs=UTF-8");
        if (fp)
        {
            fwprintf(fp, L"%s", report);
            fclose(fp);
            wprintf(L"\n  Report saved: %s\n", fileName);
        }
        else
        {
            wprintf(L"\n  [Warning] Failed to save report file.\n");
        }
    }

    // 리포트 저장 완료 — CTRL_CLOSE 핸들러 대기 해제
    g_reportDone.store(true, std::memory_order_release);

    WSACleanup();
    wprintf(L"\n[Custom Echo Stress] 종료 완료.\n");
    return 0;
}
