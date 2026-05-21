#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include <cstdio>
#include <conio.h>
#include <io.h>
#include <fcntl.h>
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
    monitor.Stop();
    manager.Stop();

    WSACleanup();
    wprintf(L"\n[Custom Echo Stress] 종료 완료.\n");
    return 0;
}
