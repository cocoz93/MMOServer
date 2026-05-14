#include "StatsDisplay.h"
#include <Windows.h>
#include <cstdio>
#include <climits>

StatsDisplay::StatsDisplay(const Stats& stats, const Config& config, int totalClients)
    : _stats(stats)
    , _config(config)
    , _totalClients(totalClients)
    , _startTimeMs(static_cast<int64_t>(GetTickCount64()))
{
}

StatsDisplay::~StatsDisplay()
{
    Stop();
}

void StatsDisplay::Start()
{
    _running = true;
    _statsThread = std::thread(&StatsDisplay::StatsLoop, this);
}

void StatsDisplay::Stop()
{
    _running = false;
    if (_statsThread.joinable())
        _statsThread.join();
}

// ─────────────────────────────────────────────────────────────────
// Stats Thread (1초 주기)
// ─────────────────────────────────────────────────────────────────
void StatsDisplay::StatsLoop()
{
    int64_t prevSend = 0;
    int64_t prevRecv = 0;

    while (_running)
    {
        Sleep(1000);

        int64_t nowMs    = static_cast<int64_t>(GetTickCount64());
        int64_t elapsed  = (nowMs - _startTimeMs) / 1000;

        // PPS 계산 (1초 슬라이딩)
        int64_t curSend  = _stats.sendCount.load();
        int64_t curRecv  = _stats.recvCount.load();
        int64_t sendPPS  = curSend - prevSend;
        int64_t recvPPS  = curRecv - prevRecv;
        prevSend = curSend;
        prevRecv = curRecv;

        PrintStats(elapsed, sendPPS, recvPPS);
    }
}

// ─────────────────────────────────────────────────────────────────
// 콘솔 출력 (커서를 0,0으로 이동해 덮어쓰기)
// ─────────────────────────────────────────────────────────────────
void StatsDisplay::PrintStats(int64_t elapsedSec, int64_t sendPPS, int64_t recvPPS) const
{
    COORD topLeft;
    topLeft.X = 0;
    topLeft.Y = 0;
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), topLeft);

    int64_t hh  = elapsedSec / 3600;
    int64_t mm  = (elapsedSec % 3600) / 60;
    int64_t ss  = elapsedSec % 60;

    int connected = _stats.connectedCount.load();

    int64_t samples = _stats.rttSamples.load();
    int64_t rttAvg  = (samples > 0) ? (_stats.rttSumMs.load() / samples) : -1;
    int64_t rttMax  = _stats.rttMaxMs.load();
    int64_t rttMin  = _stats.rttMinMs.load();
    if (rttMin == LLONG_MAX) rttMin = -1;

    // 각 줄은 충분한 공백으로 끝내 이전 출력을 덮어씀
        wprintf(L"[Custom Echo Stress]  Elapsed: %02lld:%02lld:%02lld                    \n",
            hh, mm, ss);
        wprintf(L"-----------------------------------------------------\n");
        wprintf(L"Clients       : %d / %d (connected / total)         \n",
            connected, _totalClients);
        wprintf(L"ConnectTotal  : %lld                                 \n",
            _stats.connectTotal.load());
        wprintf(L"-----------------------------------------------------\n");
        wprintf(L"[Errors]                                              \n");
        wprintf(L"  ConnectFail          : %-10lld                   \n",
            _stats.connectFail.load());
        wprintf(L"  DisconnectFromServer : %-10lld  <- 반드시 0      \n",
            _stats.disconnectFromServer.load());
        wprintf(L"  EchoNotRecv          : %-10lld  <- 누적증가 주의 \n",
            _stats.echoNotRecv.load());
        wprintf(L"  PacketError          : %-10lld  <- 반드시 0      \n",
            _stats.packetError.load());
        wprintf(L"  LateArrival          : %-10lld                   \n",
            _stats.lateArrival.load());
        wprintf(L"-----------------------------------------------------\n");
        wprintf(L"[Performance]                                         \n");
        wprintf(L"  Send PPS    : %-6lld pkt/s                        \n", sendPPS);
        wprintf(L"  Recv PPS    : %-6lld pkt/s                        \n", recvPPS);

    if (rttAvg >= 0)
    {
        wprintf(L"  RTT avg     : %4lld ms                           \n", rttAvg);
        wprintf(L"  RTT max     : %4lld ms                           \n", rttMax);
        wprintf(L"  RTT min     : %4lld ms                           \n", rttMin);
    }
    else
    {
        wprintf(L"  RTT avg     :   -- ms                            \n");
        wprintf(L"  RTT max     :   -- ms                            \n");
        wprintf(L"  RTT min     :   -- ms                            \n");
    }
    wprintf(L"-----------------------------------------------------\n");
    wprintf(L"'q' + Enter 로 종료                                   \n");
}
