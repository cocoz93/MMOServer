#pragma once
#include <cstdint>
#include <climits>
#include <atomic>
#include <algorithm>

// ─────────────────────────────────────────────────────────────────
// ThreadStats — 네트워크 스레드 전용 (스레드당 1개)
//   Prometheus/Display 스레드에서 안전하게 읽을 수 있도록 atomic 사용.
//   단일 writer + alignas(64) 캐시라인 분리이므로 relaxed 충분.
// ─────────────────────────────────────────────────────────────────
struct alignas(64) ThreadStats
{
    // --- 에러 카운터 (이벤트 빈도 낮음) ---
    std::atomic<int64_t> connectFail{0};
    std::atomic<int64_t> disconnectFromServer{0};
    std::atomic<int64_t> echoNotRecv{0};
    std::atomic<int64_t> packetError{0};
    std::atomic<int64_t> lateArrival{0};
    std::atomic<int64_t> connectTotal{0};

    // --- PPS (누적 카운터, 매 패킷 기록 — 가장 hot) ---
    std::atomic<int64_t> sendCount{0};
    std::atomic<int64_t> recvCount{0};

    // --- RTT (누적 통계, 매 수신 패킷 기록) ---
    std::atomic<int64_t> rttSumMs{0};
    std::atomic<int64_t> rttSamples{0};
    std::atomic<int64_t> rttMaxMs{0};
    std::atomic<int64_t> rttMinMs{LLONG_MAX};

    // --- RTT 히스토그램 (비누적, Prometheus 노출 시 누적 변환) ---
    // 버킷 경계(ms): 1, 5, 10, 20, 50, 100, 200, 500, 1000, +Inf
    static constexpr int    RTT_BUCKET_COUNT = 10;
    static constexpr double RTT_BUCKET_BOUNDS[RTT_BUCKET_COUNT - 1] = {
        1.0, 5.0, 10.0, 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0
    };
    std::atomic<int64_t> rttBuckets[RTT_BUCKET_COUNT];

    void RecordRtt(int64_t ms)
    {
        rttSumMs.fetch_add(ms, std::memory_order_relaxed);
        rttSamples.fetch_add(1, std::memory_order_relaxed);

        int64_t curMax = rttMaxMs.load(std::memory_order_relaxed);
        if (ms > curMax) rttMaxMs.store(ms, std::memory_order_relaxed);

        int64_t curMin = rttMinMs.load(std::memory_order_relaxed);
        if (ms < curMin) rttMinMs.store(ms, std::memory_order_relaxed);

        // 히스토그램 버킷
        double dMs = static_cast<double>(ms);
        for (int i = 0; i < RTT_BUCKET_COUNT - 1; ++i)
        {
            if (dMs <= RTT_BUCKET_BOUNDS[i])
            {
                rttBuckets[i].fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }
        rttBuckets[RTT_BUCKET_COUNT - 1].fetch_add(1, std::memory_order_relaxed);
    }

    // --- 공격 테스트 ---
    std::atomic<int64_t> attackPacketsSent{0};

    // --- 현재 연결 수 ---
    std::atomic<int>     connectedCount{0};

    // --- 클라이언트 병목 감지 ---
    std::atomic<int64_t> loopDurationMs{0};
    std::atomic<int64_t> sendBufferFull{0};
    std::atomic<int>     pendingPackets{0};

    ThreadStats() noexcept
    {
        for (auto& b : rttBuckets)
            b.store(0, std::memory_order_relaxed);
    }
};

// ─────────────────────────────────────────────────────────────────
// MergedStats — 읽기 전용 합산 스냅샷 (DisplayLoop / Prometheus / 최종 리포트)
// ─────────────────────────────────────────────────────────────────
struct MergedStats
{
    int64_t connectFail         = 0;
    int64_t disconnectFromServer= 0;
    int64_t echoNotRecv         = 0;
    int64_t packetError         = 0;
    int64_t lateArrival         = 0;
    int64_t connectTotal        = 0;

    int64_t sendCount           = 0;
    int64_t recvCount           = 0;

    int64_t rttSumMs            = 0;
    int64_t rttSamples          = 0;
    int64_t rttMaxMs            = 0;
    int64_t rttMinMs            = LLONG_MAX;

    int64_t rttBuckets[ThreadStats::RTT_BUCKET_COUNT] = {};

    int64_t attackPacketsSent   = 0;

    int     connectedCount     = 0;

    static constexpr int MAX_THREADS = 4;
    int64_t loopDurationMs[MAX_THREADS] = {};
    int     threadCount        = 0;
    int64_t sendBufferFull     = 0;
    int     pendingPackets     = 0;
};

inline MergedStats MergeThreadStats(const ThreadStats* v, int count)
{
    MergedStats m;
    for (int idx = 0; idx < count; ++idx)
    {
        const auto& ts = v[idx];
        m.connectFail          += ts.connectFail.load(std::memory_order_relaxed);
        m.disconnectFromServer += ts.disconnectFromServer.load(std::memory_order_relaxed);
        m.echoNotRecv          += ts.echoNotRecv.load(std::memory_order_relaxed);
        m.packetError          += ts.packetError.load(std::memory_order_relaxed);
        m.lateArrival          += ts.lateArrival.load(std::memory_order_relaxed);
        m.connectTotal         += ts.connectTotal.load(std::memory_order_relaxed);

        m.sendCount            += ts.sendCount.load(std::memory_order_relaxed);
        m.recvCount            += ts.recvCount.load(std::memory_order_relaxed);

        m.rttSumMs             += ts.rttSumMs.load(std::memory_order_relaxed);
        m.rttSamples           += ts.rttSamples.load(std::memory_order_relaxed);
        m.rttMaxMs              = (std::max)(m.rttMaxMs,
                                    ts.rttMaxMs.load(std::memory_order_relaxed));
        m.rttMinMs              = (std::min)(m.rttMinMs,
                                    ts.rttMinMs.load(std::memory_order_relaxed));

        for (int i = 0; i < ThreadStats::RTT_BUCKET_COUNT; ++i)
            m.rttBuckets[i]    += ts.rttBuckets[i].load(std::memory_order_relaxed);

        m.attackPacketsSent    += ts.attackPacketsSent.load(std::memory_order_relaxed);

        m.connectedCount       += ts.connectedCount.load(std::memory_order_relaxed);

        if (m.threadCount < MergedStats::MAX_THREADS)
            m.loopDurationMs[m.threadCount] = ts.loopDurationMs.load(std::memory_order_relaxed);
        m.threadCount++;
        m.sendBufferFull       += ts.sendBufferFull.load(std::memory_order_relaxed);
        m.pendingPackets       += ts.pendingPackets.load(std::memory_order_relaxed);
    }
    return m;
}
