// ==========================================================================
// CMonitorManager — 서버 모니터링 지표 저장소
//
// [책임]
//  - 서버 런타임 지표를 atomic 카운터/게이지/히스토그램으로 수집
//  - 워커 스레드, 게임 루프 스레드 등 다중 스레드에서 안전하게 기록
//  - HTTP 엔드포인트(2단계)에서 읽어 Prometheus 형식으로 노출
//
// [소유권]
//  - GameServer가 값으로 소유, IOCPServer는 포인터로 참조
// ==========================================================================
#pragma once

#include <cstdint>
#include <atomic>

class CMonitorManager
{
public:
    CMonitorManager() = default;
    ~CMonitorManager() = default;

    CMonitorManager(const CMonitorManager&) = delete;
    CMonitorManager& operator=(const CMonitorManager&) = delete;

    // ══════════════════════════════════════════════════════════════
    // 카운터 (monotonically increasing)
    // ══════════════════════════════════════════════════════════════
    std::atomic<int64_t> _recvPackets{0};       // 수신 패킷 누적
    std::atomic<int64_t> _sendPackets{0};        // 송신 패킷 누적
    std::atomic<int64_t> _recvBytes{0};          // 수신 바이트 누적
    std::atomic<int64_t> _sendBytes{0};          // 송신 바이트 누적
    std::atomic<int64_t> _sessionCreated{0};     // 세션 생성 누적
    std::atomic<int64_t> _sessionDestroyed{0};   // 세션 소멸 누적

    // ══════════════════════════════════════════════════════════════
    // 게이지 (up/down)
    // ══════════════════════════════════════════════════════════════
    std::atomic<int32_t> _sessionCount{0};       // 현재 동접 수
    std::atomic<int32_t> _eventQueueSize{0};     // 네트워크 이벤트 큐 길이

    // ══════════════════════════════════════════════════════════════
    // Tick 히스토그램 (게임 루프 전용)
    //
    // 버킷 경계: 1, 5, 10, 20, 40, 60, 80, 100, 200 ms
    // 마지막 버킷(인덱스 9)은 200ms 초과 (+Inf)
    // 비누적 방식 저장 → Prometheus 노출 시 누적으로 변환
    // ══════════════════════════════════════════════════════════════
    static constexpr int TICK_BUCKET_COUNT = 10;
    static constexpr double TICK_BUCKET_BOUNDS[TICK_BUCKET_COUNT - 1] = {
        1.0, 5.0, 10.0, 20.0, 40.0, 60.0, 80.0, 100.0, 200.0
    };

    std::atomic<int64_t> _tickBuckets[TICK_BUCKET_COUNT]{};
    std::atomic<int64_t> _tickSumUs{0};   // 합계 (마이크로초)
    std::atomic<int64_t> _tickCount{0};   // 총 tick 수

    // Tick 시간 기록 (밀리초 단위)
    void RecordTickTime(double ms)
    {
        int64_t us = static_cast<int64_t>(ms * 1000.0);
        _tickSumUs.fetch_add(us, std::memory_order_relaxed);
        _tickCount.fetch_add(1, std::memory_order_relaxed);

        // 해당 버킷에 1 증가
        for (int i = 0; i < TICK_BUCKET_COUNT - 1; ++i)
        {
            if (ms <= TICK_BUCKET_BOUNDS[i])
            {
                _tickBuckets[i].fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }
        // 모든 경계 초과 → +Inf 버킷
        _tickBuckets[TICK_BUCKET_COUNT - 1].fetch_add(1, std::memory_order_relaxed);
    }

    // ══════════════════════════════════════════════════════════════
    // 워커 스레드별 처리량
    //
    // alignas(64)로 캐시 라인 분리 — 각 워커가 자기 슬롯만 갱신
    // ══════════════════════════════════════════════════════════════
    static constexpr int MAX_WORKER_THREADS = 64;

    struct alignas(64) WorkerCounter
    {
        std::atomic<int64_t> completionCount{0};
    };

    WorkerCounter _workerCounters[MAX_WORKER_THREADS]{};
    std::atomic<int32_t> _workerThreadCount{0};

    // 워커 스레드 시작 시 호출 → 슬롯 인덱스 반환
    int RegisterWorkerThread()
    {
        return _workerThreadCount.fetch_add(1, std::memory_order_relaxed);
    }
};
