#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>
#include <climits>

struct MMOStats
{
    // ── 접속 상태 ───────────────────────────────────────────────
    std::atomic<int>     connectedCount      {0};   // 현재 TCP 연결 수
    std::atomic<int>     readyCount           {0};   // S2C_CREATE_MY_PLAYER 수신 완료 수

    // ── 접속 누적 ───────────────────────────────────────────────
    std::atomic<int64_t> connectTotal         {0};   // 총 접속 시도 횟수
    std::atomic<int64_t> connectFail          {0};   // 접속 실패 횟수

    // ── 에러 ────────────────────────────────────────────────────
    std::atomic<int64_t> disconnectFromServer {0};   // 서버 측 연결 종료 횟수

    // ── PPS (누적 카운터, 1초 간격으로 차분 계산) ────────────────
    std::atomic<int64_t> sendPackets          {0};
    std::atomic<int64_t> recvPackets          {0};
    std::atomic<int64_t> sendBytes            {0};
    std::atomic<int64_t> recvBytes            {0};

    // ── 행동 카운터 (누적) ──────────────────────────────────────
    std::atomic<int64_t> moveStartSent        {0};
    std::atomic<int64_t> moveStopSent         {0};
    std::atomic<int64_t> heartbeatSent        {0};
    std::atomic<int64_t> chatSent             {0};
    std::atomic<int64_t> zoneChangeSent       {0};
    std::atomic<int64_t> zoneChangeFail       {0};
    std::atomic<int64_t> sendBufferFull       {0};   // 송신 버퍼 오버플로우 횟수

    // ── RTT (누적 통계) ─────────────────────────────────────────
    std::atomic<int64_t> rttSumMs             {0};
    std::atomic<int64_t> rttSamples           {0};
    std::atomic<int64_t> rttMaxMs             {0};
    std::atomic<int64_t> rttMinMs             {LLONG_MAX};

    // ── RTT 히스토그램 ──────────────────────────────────────────
    // 버킷 경계(ms): 1, 5, 10, 20, 50, 100, 200, 500, 1000, +Inf
    static constexpr int    RTT_BUCKET_COUNT = 10;
    static constexpr double RTT_BUCKET_BOUNDS[RTT_BUCKET_COUNT - 1] = {
        1.0, 5.0, 10.0, 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0
    };
    std::atomic<int64_t> rttBuckets[RTT_BUCKET_COUNT] = {};
};

// ── 스레드 로컬 통계 누적기 ─────────────────────────────────────
// DummyManager 스레드에서 루프마다 누적 후 MMOStats에 flush
struct StatsLocal
{
    // 게이지 델타 (양수/음수 모두 가능)
    int connectedDelta      = 0;
    int readyDelta          = 0;

    // 카운터 (누적)
    int64_t connectTotal         = 0;
    int64_t connectFail          = 0;
    int64_t disconnectFromServer = 0;
    int64_t sendPackets          = 0;
    int64_t recvPackets          = 0;
    int64_t sendBytes            = 0;
    int64_t recvBytes            = 0;
    int64_t moveStartSent        = 0;
    int64_t moveStopSent         = 0;
    int64_t heartbeatSent        = 0;
    int64_t chatSent             = 0;
    int64_t zoneChangeSent       = 0;
    int64_t zoneChangeFail       = 0;
    int64_t sendBufferFull       = 0;

    // RTT
    int64_t rttSumMs    = 0;
    int64_t rttSamples  = 0;
    int64_t rttMaxMs    = 0;
    int64_t rttMinMs    = LLONG_MAX;
    int64_t rttBuckets[MMOStats::RTT_BUCKET_COUNT] = {};

    void RecordRtt(int64_t ms)
    {
        rttSumMs += ms;
        rttSamples += 1;
        if (ms > rttMaxMs) rttMaxMs = ms;
        if (ms < rttMinMs) rttMinMs = ms;

        double dMs = static_cast<double>(ms);
        for (int i = 0; i < MMOStats::RTT_BUCKET_COUNT - 1; ++i)
        {
            if (dMs <= MMOStats::RTT_BUCKET_BOUNDS[i])
            {
                rttBuckets[i] += 1;
                return;
            }
        }
        rttBuckets[MMOStats::RTT_BUCKET_COUNT - 1] += 1;
    }

    void Flush(MMOStats& g)
    {
        if (connectedDelta != 0)       g.connectedCount.fetch_add(connectedDelta, std::memory_order_relaxed);
        if (readyDelta != 0)           g.readyCount.fetch_add(readyDelta, std::memory_order_relaxed);
        if (connectTotal != 0)         g.connectTotal.fetch_add(connectTotal, std::memory_order_relaxed);
        if (connectFail != 0)          g.connectFail.fetch_add(connectFail, std::memory_order_relaxed);
        if (disconnectFromServer != 0) g.disconnectFromServer.fetch_add(disconnectFromServer, std::memory_order_relaxed);
        if (sendPackets != 0)          g.sendPackets.fetch_add(sendPackets, std::memory_order_relaxed);
        if (recvPackets != 0)          g.recvPackets.fetch_add(recvPackets, std::memory_order_relaxed);
        if (sendBytes != 0)            g.sendBytes.fetch_add(sendBytes, std::memory_order_relaxed);
        if (recvBytes != 0)            g.recvBytes.fetch_add(recvBytes, std::memory_order_relaxed);
        if (moveStartSent != 0)        g.moveStartSent.fetch_add(moveStartSent, std::memory_order_relaxed);
        if (moveStopSent != 0)         g.moveStopSent.fetch_add(moveStopSent, std::memory_order_relaxed);
        if (heartbeatSent != 0)        g.heartbeatSent.fetch_add(heartbeatSent, std::memory_order_relaxed);
        if (chatSent != 0)            g.chatSent.fetch_add(chatSent, std::memory_order_relaxed);
        if (zoneChangeSent != 0)       g.zoneChangeSent.fetch_add(zoneChangeSent, std::memory_order_relaxed);
        if (zoneChangeFail != 0)       g.zoneChangeFail.fetch_add(zoneChangeFail, std::memory_order_relaxed);
        if (sendBufferFull != 0)       g.sendBufferFull.fetch_add(sendBufferFull, std::memory_order_relaxed);

        if (rttSumMs != 0)    g.rttSumMs.fetch_add(rttSumMs, std::memory_order_relaxed);
        if (rttSamples != 0)  g.rttSamples.fetch_add(rttSamples, std::memory_order_relaxed);

        // RTT max (CAS)
        if (rttMaxMs > 0)
        {
            int64_t cur = g.rttMaxMs.load(std::memory_order_relaxed);
            while (rttMaxMs > cur && !g.rttMaxMs.compare_exchange_weak(cur, rttMaxMs, std::memory_order_relaxed)) {}
        }
        // RTT min (CAS)
        if (rttMinMs < LLONG_MAX)
        {
            int64_t cur = g.rttMinMs.load(std::memory_order_relaxed);
            while (rttMinMs < cur && !g.rttMinMs.compare_exchange_weak(cur, rttMinMs, std::memory_order_relaxed)) {}
        }

        for (int i = 0; i < MMOStats::RTT_BUCKET_COUNT; ++i)
        {
            if (rttBuckets[i] != 0)
                g.rttBuckets[i].fetch_add(rttBuckets[i], std::memory_order_relaxed);
        }

        Reset();
    }

    void Reset()
    {
        connectedDelta = 0;
        readyDelta = 0;
        connectTotal = 0;
        connectFail = 0;
        disconnectFromServer = 0;
        sendPackets = 0;
        recvPackets = 0;
        sendBytes = 0;
        recvBytes = 0;
        moveStartSent = 0;
        moveStopSent = 0;
        heartbeatSent = 0;
        chatSent = 0;
        zoneChangeSent = 0;
        zoneChangeFail = 0;
        sendBufferFull = 0;
        rttSumMs = 0;
        rttSamples = 0;
        rttMaxMs = 0;
        rttMinMs = LLONG_MAX;
        std::memset(rttBuckets, 0, sizeof(rttBuckets));
    }
};
