#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <cstdio>
#include <conio.h>
#include <io.h>
#include <fcntl.h>
#include <random>
#include <chrono>

#pragma comment(lib, "ws2_32.lib")

// ─────────────────────────────────────────────────────────────────
// 설정
// ─────────────────────────────────────────────────────────────────
struct Config
{
    std::string serverIp    = "127.0.0.1";
    int         port        = 6000;
    int         threadCount = 4;        // 워커 스레드 수
    int         delayMinMs  = 0;        // connect 후 close 전 대기 최소(ms)
    int         delayMaxMs  = 0;        // connect 후 close 전 대기 최대(ms)
};

// ─────────────────────────────────────────────────────────────────
// 통계
// ─────────────────────────────────────────────────────────────────
struct Stats
{
    std::atomic<int64_t> connectSuccess  {0};
    std::atomic<int64_t> connectFail     {0};
    std::atomic<int64_t> totalAttempted  {0};
    std::atomic<int64_t> currentConnected{0};

    // connect() 소요시간 (us)
    std::atomic<int64_t> latencySumUs    {0};
    std::atomic<int64_t> latencySamples  {0};
    std::atomic<int64_t> latencyMaxUs    {0};
    std::atomic<int64_t> latencyMinUs    {LLONG_MAX};
};

static Stats       g_stats;
static Config      g_config;
static std::atomic<bool> g_running{true};

// ─────────────────────────────────────────────────────────────────
// 설정 입력
// ─────────────────────────────────────────────────────────────────
static Config InputConfig()
{
    Config cfg;

    wprintf(L"=== Accept Stress Test 설정 ===\n");
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

    wprintf(L"ThreadCount [4]: ");
    {
        std::string s;
        std::getline(std::cin, s);
        if (!s.empty()) cfg.threadCount = std::stoi(s);
    }

    wprintf(L"DelayMinMs (close 전 대기 최소) [0]: ");
    {
        std::string s;
        std::getline(std::cin, s);
        if (!s.empty()) cfg.delayMinMs = std::stoi(s);
    }

    wprintf(L"DelayMaxMs (close 전 대기 최대) [0]: ");
    {
        std::string s;
        std::getline(std::cin, s);
        if (!s.empty()) cfg.delayMaxMs = std::stoi(s);
    }

    return cfg;
}

// ─────────────────────────────────────────────────────────────────
// connect 소요시간 기록
// ─────────────────────────────────────────────────────────────────
static void RecordLatency(int64_t us)
{
    g_stats.latencySumUs.fetch_add(us);
    g_stats.latencySamples.fetch_add(1);

    // max
    int64_t prev = g_stats.latencyMaxUs.load();
    while (us > prev && !g_stats.latencyMaxUs.compare_exchange_weak(prev, us));

    // min
    prev = g_stats.latencyMinUs.load();
    while (us < prev && !g_stats.latencyMinUs.compare_exchange_weak(prev, us));
}

// ─────────────────────────────────────────────────────────────────
// 워커 스레드 : connect → (delay) → closesocket 반복
// ─────────────────────────────────────────────────────────────────
static void WorkerThread()
{
    SOCKADDR_IN serverAddr;
    ZeroMemory(&serverAddr, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(static_cast<u_short>(g_config.port));
    inet_pton(AF_INET, g_config.serverIp.c_str(), &serverAddr.sin_addr);

    // 스레드별 랜덤
    std::mt19937 rng(static_cast<unsigned>(
        std::chrono::steady_clock::now().time_since_epoch().count()
        ^ std::hash<std::thread::id>{}(std::this_thread::get_id())));
    std::uniform_int_distribution<int> delayDist(g_config.delayMinMs, g_config.delayMaxMs);

    while (g_running)
    {
        g_stats.totalAttempted.fetch_add(1);

        // 소켓 생성
        SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET)
        {
            g_stats.connectFail.fetch_add(1);
            continue;
        }

        // connect 시간 측정
        auto t1 = std::chrono::steady_clock::now();
        int ret = connect(sock, (SOCKADDR*)&serverAddr, sizeof(serverAddr));
        auto t2 = std::chrono::steady_clock::now();

        if (ret == SOCKET_ERROR)
        {
            g_stats.connectFail.fetch_add(1);
            closesocket(sock);
            continue;
        }

        int64_t us = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
        RecordLatency(us);
        g_stats.connectSuccess.fetch_add(1);
        g_stats.currentConnected.fetch_add(1);

        // 지정된 딜레이만큼 연결 유지
        int delay = delayDist(rng);
        if (delay > 0)
            Sleep(delay);

        closesocket(sock);
        g_stats.currentConnected.fetch_sub(1);
    }
}

// ─────────────────────────────────────────────────────────────────
// 통계 출력 (1초 주기, 콘솔 덮어쓰기)
// ─────────────────────────────────────────────────────────────────
static void StatsThread()
{
    int64_t prevSuccess = 0;
    int64_t startMs = static_cast<int64_t>(GetTickCount64());

    while (g_running)
    {
        Sleep(1000);
        if (!g_running) break;

        int64_t nowMs   = static_cast<int64_t>(GetTickCount64());
        int64_t elapsed = (nowMs - startMs) / 1000;
        int64_t hh = elapsed / 3600;
        int64_t mm = (elapsed % 3600) / 60;
        int64_t ss = elapsed % 60;

        int64_t curSuccess = g_stats.connectSuccess.load();
        int64_t cps = curSuccess - prevSuccess;  // connects per second
        prevSuccess = curSuccess;

        int64_t attempted   = g_stats.totalAttempted.load();
        int64_t fail        = g_stats.connectFail.load();
        int64_t connected   = g_stats.currentConnected.load();
        int64_t samples     = g_stats.latencySamples.load();
        int64_t avgUs       = (samples > 0) ? (g_stats.latencySumUs.load() / samples) : -1;
        int64_t maxUs       = g_stats.latencyMaxUs.load();
        int64_t minUs       = g_stats.latencyMinUs.load();
        if (minUs == LLONG_MAX) minUs = -1;

        // 커서를 0,0으로 이동해 덮어쓰기
        COORD topLeft = {0, 0};
        SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), topLeft);

        wprintf(L"[Accept Stress Test]  Elapsed: %02lld:%02lld:%02lld              \n", hh, mm, ss);
        wprintf(L"-----------------------------------------------------\n");
        wprintf(L"  Threads     : %d                                    \n", g_config.threadCount);
        wprintf(L"  Delay       : %d ~ %d ms                           \n", g_config.delayMinMs, g_config.delayMaxMs);
        wprintf(L"-----------------------------------------------------\n");
        wprintf(L"[Connections]                                          \n");
        wprintf(L"  Attempted   : %-10lld                              \n", attempted);
        wprintf(L"  Success     : %-10lld                              \n", curSuccess);
        wprintf(L"  Fail        : %-10lld  <- 반드시 0                 \n", fail);
        wprintf(L"  Current     : %-10lld  (now connected)             \n", connected);
        wprintf(L"  CPS         : %-10lld  connects/sec               \n", cps);
        wprintf(L"-----------------------------------------------------\n");
        wprintf(L"[Connect Latency]                                      \n");
        if (avgUs >= 0)
        {
            wprintf(L"  avg         : %-6lld us                           \n", avgUs);
            wprintf(L"  max         : %-6lld us                           \n", maxUs);
            wprintf(L"  min         : %-6lld us                           \n", minUs);
        }
        else
        {
            wprintf(L"  avg         :   -- us                              \n");
            wprintf(L"  max         :   -- us                              \n");
            wprintf(L"  min         :   -- us                              \n");
        }
        wprintf(L"-----------------------------------------------------\n");

        wprintf(L"'q' 키로 종료                                         \n");
    }
}

// ─────────────────────────────────────────────────────────────────
// 최종 리포트
// ─────────────────────────────────────────────────────────────────
static void PrintFinalReport(int64_t elapsedMs)
{
    wprintf(L"\n\n");
    wprintf(L"=====================================================\n");
    wprintf(L"              Accept Stress Test 결과                \n");
    wprintf(L"=====================================================\n");
    wprintf(L"  총 시도        : %lld\n", g_stats.totalAttempted.load());
    wprintf(L"  성공           : %lld\n", g_stats.connectSuccess.load());
    wprintf(L"  실패           : %lld\n", g_stats.connectFail.load());

    double sec = elapsedMs / 1000.0;
    if (sec > 0)
    {
        double avgCps = g_stats.connectSuccess.load() / sec;
        wprintf(L"  평균 CPS       : %.1f connects/sec\n", avgCps);
    }

    int64_t samples = g_stats.latencySamples.load();
    if (samples > 0)
    {
        wprintf(L"  Latency avg    : %lld us\n", g_stats.latencySumUs.load() / samples);
        wprintf(L"  Latency max    : %lld us\n", g_stats.latencyMaxUs.load());
        int64_t minUs = g_stats.latencyMinUs.load();
        wprintf(L"  Latency min    : %lld us\n", (minUs == LLONG_MAX) ? 0 : minUs);
    }
    wprintf(L"=====================================================\n");
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

    g_config = InputConfig();

    // delayMax가 delayMin보다 작으면 보정
    if (g_config.delayMaxMs < g_config.delayMinMs)
        g_config.delayMaxMs = g_config.delayMinMs;

    wprintf(L"\n[AcceptStress] Start. Server=%hs:%d, Threads=%d\n\n",
        g_config.serverIp.c_str(), g_config.port, g_config.threadCount);
    Sleep(1000);

    // 콘솔 커서 숨기기
    CONSOLE_CURSOR_INFO cursorInfo;
    GetConsoleCursorInfo(GetStdHandle(STD_OUTPUT_HANDLE), &cursorInfo);
    cursorInfo.bVisible = FALSE;
    SetConsoleCursorInfo(GetStdHandle(STD_OUTPUT_HANDLE), &cursorInfo);

    int64_t startMs = static_cast<int64_t>(GetTickCount64());

    // 통계 스레드
    std::thread statsThread(StatsThread);

    // 워커 스레드
    std::vector<std::thread> workers;
    workers.reserve(g_config.threadCount);
    for (int i = 0; i < g_config.threadCount; i++)
        workers.emplace_back(WorkerThread);

    // 종료 조건 대기: q 키 입력
    while (g_running)
    {
        if (_kbhit())
        {
            int ch = _getch();
            if (ch == 'q' || ch == 'Q')
            {
                g_running = false;
                break;
            }
        }
        Sleep(100);
    }

    // 정리
    for (auto& w : workers)
    {
        if (w.joinable()) w.join();
    }
    statsThread.join();

    int64_t elapsedMs = static_cast<int64_t>(GetTickCount64()) - startMs;

    // 커서 복원
    cursorInfo.bVisible = TRUE;
    SetConsoleCursorInfo(GetStdHandle(STD_OUTPUT_HANDLE), &cursorInfo);

    PrintFinalReport(elapsedMs);

    WSACleanup();
    return 0;
}
