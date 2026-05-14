#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include <iostream>
#include <string>
#include <cstdio>
#include <conio.h>
#include <io.h>
#include <fcntl.h>
#include "Config.h"
#include "DummyManager.h"
#include "StatsDisplay.h"

#pragma comment(lib, "ws2_32.lib")

// ─────────────────────────────────────────────────────────────────
// 설정 입력
// ─────────────────────────────────────────────────────────────────
static Config InputConfig()
{
    Config cfg;

    wprintf(L"=== Echo Dummy Client 설정 ===\n");
    wprintf(L"Server IP [127.0.0.1]: ");
    {
        std::string s;
        std::getline(std::cin, s);
        if (!s.empty()) cfg.serverIp = s;
    }

    wprintf(L"Port [6000]: ");
    {
        std::string s;
        std::getline(std::cin, s);
        if (!s.empty()) cfg.port = std::stoi(s);
    }

    wprintf(L"ClientCount (1/50/100) [1]: ");
    {
        std::string s;
        std::getline(std::cin, s);
        if (!s.empty()) cfg.clientCount = std::stoi(s);
    }

    wprintf(L"OverSendCount (1/100/200) [1]: ");
    {
        std::string s;
        std::getline(std::cin, s);
        if (!s.empty()) cfg.overSendCount = std::stoi(s);
    }

    wprintf(L"LoopDelayMs [100]: ");
    {
        std::string s;
        std::getline(std::cin, s);
        if (!s.empty()) cfg.loopDelayMs = std::stoi(s);
    }

    wprintf(L"DisconnectTest (0/1) [0]: ");
    {
        std::string s;
        std::getline(std::cin, s);
        if (!s.empty()) cfg.disconnectTest = (std::stoi(s) != 0);
    }

    return cfg;
}

// ─────────────────────────────────────────────────────────────────
// 진입점
// ─────────────────────────────────────────────────────────────────
int main()
{
    _setmode(_fileno(stdout), _O_U16TEXT);
    _setmode(_fileno(stderr), _O_U16TEXT);

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        wprintf(L"[Error] WSAStartup 실패\n");
        return 1;
    }

    Config cfg = InputConfig();

        wprintf(L"\n[Echo Dummy] start. Server=%hs:%d, Clients=%d\n\n",
            cfg.serverIp.c_str(), cfg.port, cfg.clientCount);
    Sleep(1000);

    // 콘솔 커서 숨기기 (깜빡임 방지)
    CONSOLE_CURSOR_INFO cursorInfo;
    GetConsoleCursorInfo(GetStdHandle(STD_OUTPUT_HANDLE), &cursorInfo);
    cursorInfo.bVisible = FALSE;
    SetConsoleCursorInfo(GetStdHandle(STD_OUTPUT_HANDLE), &cursorInfo);

    DummyManager  manager(cfg);
    StatsDisplay  display(manager.GetStats(), cfg, cfg.clientCount);

    manager.Start();
    display.Start();

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
    display.Stop();
    manager.Stop();

    // 커서 복원
    cursorInfo.bVisible = TRUE;
    SetConsoleCursorInfo(GetStdHandle(STD_OUTPUT_HANDLE), &cursorInfo);

    WSACleanup();
    wprintf(L"\n[Echo Dummy] 종료 완료.\n");
    return 0;
}
