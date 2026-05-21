#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include <cstdio>
#include <conio.h>
#include <io.h>
#include <fcntl.h>
#include <ctime>
#include <string>
#include "Config.h"
#include "DummyManager.h"
#include "StressMonitorServer.h"

#pragma comment(lib, "ws2_32.lib")

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

    Config cfg;
    cfg.Load(argc > 1 ? argv[1] : nullptr);

        wprintf(L"\n[Custom Echo Stress] start. Server=%hs:%d, Clients=%d\n",
            cfg.serverIp.c_str(), cfg.port, cfg.clientCount);
        wprintf(L"[Custom Echo Stress] Prometheus metrics on port %d\n\n",
            cfg.monitorPort);
    Sleep(1000);

    DummyManager         manager(cfg);
    StressMonitorServer  monitor(manager.GetStats(), cfg.monitorPort);

    manager.Start();
    monitor.Start();

    // ── 종료 조건 대기 ──────────────────────────────────────────
    int64_t startMs = static_cast<int64_t>(GetTickCount64());

    while (true)
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
    const Stats& s = manager.GetStats();
    int mm = static_cast<int>(elapsedSec / 60);
    int ss = static_cast<int>(elapsedSec % 60);

    int64_t samples = s.rttSamples.load();
    int64_t avgRtt  = (samples > 0) ? (s.rttSumMs.load() / samples) : 0;
    int64_t minRtt  = (samples > 0) ? s.rttMinMs.load() : 0;
    int64_t maxRtt  = s.rttMaxMs.load();

    int64_t avgTps = (elapsedSec > 0) ? (s.recvCount.load() / elapsedSec) : 0;

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
        s.connectTotal.load(),
        s.connectFail.load(),
        s.disconnectFromServer.load(),
        s.sendCount.load(),
        s.recvCount.load(),
        avgTps,
        avgRtt, minRtt, maxRtt, samples,
        s.echoNotRecv.load(),
        s.packetError.load(),
        s.lateArrival.load(),
        s.sendBufferFull.load());

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

    WSACleanup();
    wprintf(L"\n[Custom Echo Stress] 종료 완료.\n");
    return 0;
}
