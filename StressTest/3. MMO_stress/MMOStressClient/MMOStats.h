#pragma once
#include <atomic>
#include <cstdint>
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

    void RecordRtt(int64_t ms)
    {
        rttSumMs.fetch_add(ms);
        rttSamples.fetch_add(1);

        // atomic max
        int64_t curMax = rttMaxMs.load();
        while (ms > curMax && !rttMaxMs.compare_exchange_weak(curMax, ms)) {}

        // atomic min
        int64_t curMin = rttMinMs.load();
        while (ms < curMin && !rttMinMs.compare_exchange_weak(curMin, ms)) {}

        // 히스토그램 버킷
        double dMs = static_cast<double>(ms);
        for (int i = 0; i < RTT_BUCKET_COUNT - 1; ++i)
        {
            if (dMs <= RTT_BUCKET_BOUNDS[i])
            {
                rttBuckets[i].fetch_add(1);
                return;
            }
        }
        rttBuckets[RTT_BUCKET_COUNT - 1].fetch_add(1);  // +Inf
    }
};
