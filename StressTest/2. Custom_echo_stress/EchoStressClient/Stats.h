#pragma once
#include <atomic>
#include <cstdint>
#include <climits>

struct Stats
{
    // --- 에러 카운터 ---
    std::atomic<int64_t> connectFail        {0};
    std::atomic<int64_t> disconnectFromServer{0};
    std::atomic<int64_t> echoNotRecv        {0};
    std::atomic<int64_t> packetError        {0};
    std::atomic<int64_t> lateArrival       {0};
    std::atomic<int64_t> connectTotal       {0};

    // --- PPS (누적 카운터) ---
    std::atomic<int64_t> sendCount          {0};
    std::atomic<int64_t> recvCount          {0};

    // --- RTT (누적 통계) ---
    std::atomic<int64_t> rttSumMs           {0};
    std::atomic<int64_t> rttSamples         {0};
    std::atomic<int64_t> rttMaxMs           {0};
    std::atomic<int64_t> rttMinMs           {LLONG_MAX};

    // --- RTT 히스토그램 (비누적, Prometheus 노출 시 누적 변환) ---
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

    // --- 현재 연결 수 ---
    std::atomic<int>     connectedCount     {0};

    // --- 클라이언트 병목 감지 ---
    std::atomic<int64_t> loopDurationMs     {0};   // 루프 1회 소요 시간 (gauge, 최근 값)
    std::atomic<int64_t> sendBufferFull     {0};   // 송신 버퍼 가득참 횟수 (counter)
    std::atomic<int>     pendingPackets     {0};   // 전체 미응답 패킷 수 (gauge)
};
