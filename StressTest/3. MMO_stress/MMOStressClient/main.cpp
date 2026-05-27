#include <WinSock2.h>
#include <Windows.h>
#include <cstdio>
#include <conio.h>
#include <io.h>
#include <fcntl.h>
#include <vector>
#include <memory>
#include <algorithm>

#include "MMOStressConfig.h"
#include "MMOStats.h"
#include "DummyManager.h"
#include "StressMonitorServer.h"

// 전역 설정 / 통계
MMOStressConfig g_Config;
MMOStats    g_Stats;

int main()
{
    _setmode(_fileno(stdout), _O_U16TEXT);
    _setmode(_fileno(stderr), _O_U16TEXT);

    // WSA 초기화
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        wprintf(L"[Error] WSAStartup failed: %d\n", WSAGetLastError());
        return 1;
    }

    // 설정 로드
    g_Config.Load();

    // ── DummyManager N개 분산 생성 ──────────────────────────────
    const int totalClients    = g_Config.clientCount;
    const int perThread       = g_Config.clientsPerThread;
    const int threadCount     = (totalClients + perThread - 1) / perThread;

    wprintf(L"\n");
    wprintf(L"=============================================\n");
    wprintf(L"  MMO Stress Client\n");
    wprintf(L"  Target : %hs:%d\n", g_Config.serverIp.c_str(), g_Config.port);
    wprintf(L"  Clients: %d  (%d threads x %d)\n", totalClients, threadCount, perThread);
    wprintf(L"=============================================\n");
    wprintf(L"\n");

    std::vector<std::unique_ptr<DummyManager>> managers;
    managers.reserve(threadCount);

    int remaining = totalClients;
    for (int i = 0; i < threadCount; ++i)
    {
        int count = (std::min)(perThread, remaining);
        managers.push_back(std::make_unique<DummyManager>(g_Config, g_Stats, count));
        remaining -= count;
    }

    StressMonitorServer monitor(g_Stats, g_Config.monitorPort);

    for (auto& mgr : managers)
        mgr->Start();
    monitor.Start();

    // ── 메인 루프 (1초 주기 콘솔 출력 + 종료 감시) ──────────────
    int64_t startMs       = static_cast<int64_t>(GetTickCount64());
    int64_t prevRecvPkts  = 0;
    int64_t prevSendPkts  = 0;

    while (true)
    {
        // 시간 제한 종료
        if (g_Config.testDurationSec > 0)
        {
            int64_t elapsed = (static_cast<int64_t>(GetTickCount64()) - startMs) / 1000;
            if (elapsed >= g_Config.testDurationSec) break;
        }

        // 'q' 키 종료
        if (_kbhit())
        {
            int ch = _getch();
            if (ch == 'q' || ch == 'Q') break;
        }

        // 1초 주기 통계 출력
        using mo = std::memory_order;
        int connected = g_Stats.connectedCount.load(mo::relaxed);
        int ready     = g_Stats.readyCount.load(mo::relaxed);

        int64_t curRecv = g_Stats.recvPackets.load(mo::relaxed);
        int64_t curSend = g_Stats.sendPackets.load(mo::relaxed);
        int64_t recvPps = curRecv - prevRecvPkts;
        int64_t sendPps = curSend - prevSendPkts;
        prevRecvPkts = curRecv;
        prevSendPkts = curSend;

        int64_t err = g_Stats.disconnectFromServer.load(mo::relaxed);
        int64_t rttMax = g_Stats.rttMaxMs.load(mo::relaxed);
        int64_t rttSamples = g_Stats.rttSamples.load(mo::relaxed);

        // RTT 근사 p50/p99 (히스토그램 버킷에서 추정)
        int64_t p50 = 0, p99 = 0;
        if (rttSamples > 0)
        {
            int64_t cum = 0;
            int64_t target50 = (rttSamples + 1) / 2;
            int64_t target99 = (rttSamples * 99 + 99) / 100;
            static const int64_t bounds[] = { 1, 5, 10, 20, 50, 100, 200, 500, 1000, 9999 };
            for (int i = 0; i < MMOStats::RTT_BUCKET_COUNT; ++i)
            {
                cum += g_Stats.rttBuckets[i].load(mo::relaxed);
                if (p50 == 0 && cum >= target50) p50 = bounds[i];
                if (p99 == 0 && cum >= target99) p99 = bounds[i];
            }
        }

        wprintf(L"\r[Connected %d | Ready %d] Send %lld pps  Recv %lld pps  |  RTT p50=%lld p99=%lld max=%lld ms  |  Err %lld       ",
                connected, ready, sendPps, recvPps, p50, p99, rttMax, err);

        Sleep(1000);
    }

    // ── 종료 ────────────────────────────────────────────────────
    wprintf(L"\n[Main] Shutting down...\n");
    monitor.Stop();
    for (auto& mgr : managers)
        mgr->Stop();

    WSACleanup();
    wprintf(L"[Main] Done.\n");
    return 0;
}
