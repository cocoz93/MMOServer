// ==========================================================================
// CMonitorManager — 서버 모니터링 지표 저장소
//
// [책임]
//  - 서버 런타임 지표를 Interlocked 카운터/게이지/히스토그램으로 수집
//  - 워커 스레드, 게임 루프 스레드 등 다중 스레드에서 안전하게 기록
//  - HTTP 엔드포인트(2단계)에서 읽어 Prometheus 형식으로 노출
//
// [소유권]
//  - main()이 값으로 소유, GameServer/IOCPServer는 레퍼런스로 참조
// ==========================================================================
#pragma once

#include <WinSock2.h>
#include <Windows.h>
#include <cstdint>

class CMonitorManager
{
public:
    CMonitorManager() = default;
    ~CMonitorManager() = default;

    CMonitorManager(const CMonitorManager&) = delete;
    CMonitorManager& operator=(const CMonitorManager&) = delete;

    // ══════════════════════════════════════════════════════════════
    // 카운터 (monotonically increasing)
    //
    // 워커 스레드 핫패스(per-packet)에서 동시 갱신되는 카운터를
    // alignas(64)로 캐시 라인 분리하여 false sharing 방지
    // ══════════════════════════════════════════════════════════════

    // ── Recv 핫 카운터 (ProcessRecv 경로, 모든 워커 스레드) ──
    alignas(64) volatile LONG64 _recvPackets = 0;   // 수신 패킷 누적
    volatile LONG64 _recvBytes = 0;                  // 수신 바이트 누적
    volatile LONG64 _wsaRecvCalls = 0;               // WSARecv 시스템 콜 횟수

    // ── Send 핫 카운터 (ProcessSend/PostSend/RequestSendMsg 경로, 모든 워커 스레드) ──
    alignas(64) volatile LONG64 _sendPackets = 0;    // 논리적 송신 패킷 누적 (RequestSendMsg에서 Enqueue 성공 시)
    volatile LONG64 _sendBytes = 0;                  // 송신 바이트 누적
    volatile LONG64 _wsaSendCalls = 0;               // WSASend 시스템 콜 횟수
    volatile LONG64 _wsaSendCompletions = 0;         // WSASend 완료 횟수 (ProcessSend IOCP 콜백)
    volatile LONG64 _sendEnqueuedBytes = 0;          // SendQ Enqueue 바이트 누적 (_sendBytes와의 차이 = 체류량)
    volatile LONG64 _sendContention = 0;             // PostSend 경합 (이미 송신 중이라 건너뛴 횟수)

    // ── 세션/에러 카운터 (per-session 또는 rare, 낮은 빈도) ──
    alignas(64) volatile LONG64 _sessionCreated = 0;    // 세션 생성 누적
    volatile LONG64 _sessionDestroyed = 0;               // 세션 소멸 누적
    volatile LONG64 _acceptFailed = 0;                   // Accept 거부 (인덱스 부족)
    volatile LONG64 _sessionTimedOut = 0;                // 타이밍 휠 타임아웃 킥
    volatile LONG64 _packetErrors = 0;                   // 패킷 에러 (크기 검증 실패, 알 수 없는 타입)
    volatile LONG64 _sendQueueOverflow = 0;              // SendQ 오버플로 (Enqueue 실패)
    volatile LONG64 _partialSend = 0;                    // 진짜 partial send (WSASend 성공이나 일부만 전송) → disconnect 횟수
    volatile LONG64 _recvBufferOverflow = 0;             // RecvQ 오버플로 (수신 버퍼 가득 참)
    volatile LONG64 _sendDiscardedBytes = 0;             // Disconnect 시 SendQ 잔여 바이트 (체류량 보정용)

    // ══════════════════════════════════════════════════════════════
    // 게이지 (up/down)
    // ══════════════════════════════════════════════════════════════
    alignas(64) volatile LONG _sessionCount = 0;    // 현재 동접 수
    // 이벤트 큐 크기는 ThreadSafeQueue::GetSize()로 직접 조회

    // ══════════════════════════════════════════════════════════════
    // 게임 루프 전용 카운터 (alignas(64)로 캐시 라인 분리)
    //
    // 워커 스레드 카운터와 false sharing 방지를 위해 별도 struct로 격리
    // ══════════════════════════════════════════════════════════════
    struct alignas(64) GameLoopCounters
    {
        volatile LONG64 _zoneChangeCount = 0;   // 존 이동 횟수
        volatile LONG64 _cheatDetected = 0;     // 이동 검증 실패 (치트 감지) — 미구현, 향후 서버 권위 좌표 검증 시 사용 예정

        // 구간별 시간 (마이크로초 누적, counter)
        // Prometheus에서 rate(phase_sum) / rate(tick_count) → 틱당 평균 소비 시간
        volatile LONG64 _phaseNetworkUs = 0;        // ProcessNetworkEvents 소비 시간
        volatile LONG64 _phaseGameLogicUs = 0;      // TickAll + 섹터 변경 + 클램핑 브로드캐스트
        volatile LONG64 _phaseBroadcastSyncUs = 0;  // 주기적 위치 동기화 브로드캐스트

        // 이벤트 큐 크기 (게이지, 게임 루프 스레드에서만 갱신)
        // ProcessNetworkEvents() 진입 전 스냅샷 → 소비 못 따라가면 누적되는 걸 감지
        volatile LONG _eventQueueSize = 0;

        // 브로드캐스트 비용 (counter)
        // 평균 대상 수 = rate(targets) / rate(calls)
        volatile LONG64 _broadcastCalls = 0;        // 브로드캐스트 호출 횟수
        volatile LONG64 _broadcastTargets = 0;      // 브로드캐스트 대상 수 누적

        // Tick 히스토그램
        //
        // 버킷 경계: 1, 5, 10, 20, 40, 60, 80, 100, 200 ms
        // 마지막 버킷(인덱스 9)은 200ms 초과 (+Inf)
        // 비누적 방식 저장 → Prometheus 노출 시 누적으로 변환
        static constexpr int TICK_BUCKET_COUNT = 10;
        static constexpr double TICK_BUCKET_BOUNDS[TICK_BUCKET_COUNT - 1] = {
            1.0, 5.0, 10.0, 20.0, 40.0, 60.0, 80.0, 100.0, 200.0
        };

        volatile LONG64 _tickBuckets[TICK_BUCKET_COUNT] = {};
        volatile LONG64 _tickSumUs = 0;   // 합계 (마이크로초)
        volatile LONG64 _tickCount = 0;   // 총 tick 수

        // Tick 시간 기록 (밀리초 단위)
        void RecordTickTime(double ms)
        {
            LONG64 us = static_cast<LONG64>(ms * 1000.0);
            InterlockedExchangeAdd64(&_tickSumUs, us);
            InterlockedIncrement64(&_tickCount);

            // 해당 버킷에 1 증가
            for (int i = 0; i < TICK_BUCKET_COUNT - 1; ++i)
            {
                if (ms <= TICK_BUCKET_BOUNDS[i])
                {
                    InterlockedIncrement64(&_tickBuckets[i]);
                    return;
                }
            }
            // 모든 경계 초과 → +Inf 버킷
            InterlockedIncrement64(&_tickBuckets[TICK_BUCKET_COUNT - 1]);
        }
    } _gameLoop;

    // ══════════════════════════════════════════════════════════════
    // 워커 스레드별 처리량
    //
    // alignas(64)로 캐시 라인 분리 — 각 워커가 자기 슬롯만 갱신
    // ══════════════════════════════════════════════════════════════
    static constexpr int MAX_WORKER_THREADS = 64;

    struct alignas(64) WorkerCounter
    {
        volatile LONG64 completionCount = 0;
    };

    WorkerCounter _workerCounters[MAX_WORKER_THREADS] = {};
    volatile LONG _workerThreadCount = 0;

    // 워커 스레드 시작 시 호출 → 슬롯 인덱스 반환 (-1: 슬롯 초과)
    int RegisterWorkerThread()
    {
        LONG index = InterlockedIncrement(&_workerThreadCount) - 1;
        if (index >= MAX_WORKER_THREADS)
        {
            InterlockedDecrement(&_workerThreadCount);
            return -1;
        }
        return static_cast<int>(index);
    }

};
