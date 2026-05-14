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

    // --- PPS (매 초 stats 스레드가 읽은 뒤 초기화) ---
    std::atomic<int64_t> sendCount          {0};
    std::atomic<int64_t> recvCount          {0};

    // --- RTT (누적 통계) ---
    std::atomic<int64_t> rttSumMs           {0};
    std::atomic<int64_t> rttSamples         {0};
    std::atomic<int64_t> rttMaxMs           {0};
    std::atomic<int64_t> rttMinMs           {LLONG_MAX};

    // --- 현재 연결 수 ---
    std::atomic<int>     connectedCount     {0};
};
