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

    // ── DB 저장 파이프라인 (USE_DB_WORKER) — dirty flag 비동기 저장 ──
    alignas(64) volatile LONG64 _dbSavedJobs = 0;    // UPSERT 성공 누적
    volatile LONG64 _dbFailedJobs = 0;               // UPSERT 실패 누적
    volatile LONG64 _dbDroppedJobs = 0;              // 백프레셔 드롭 누적 (슬롯 큐 상한 초과)
    static constexpr int MAX_DB_WORKERS = 16;
    volatile LONG64 _dbQueueDepth[MAX_DB_WORKERS] = {};  // 워커별 게이지: 마지막 배치 인출량
    volatile LONG _dbWorkerCount = 0;                    // 노출 루프 상한 (CDBWorker::Start에서 설정)

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

        // 멤버십 변경 복사량 — BroadcastAroundSector(gather/enqueue 계측 대상) 밖의 송신(복사) 횟수.
        //   ProcessSectorChange·BroadcastEnterZone·BroadcastLeaveZone 전용인
        //   SendCreateOtherPlayer/SendDeletePlayer 호출 수. _broadcastTargets(계측 대상 복사)와
        //   비교해 비계측 복사의 비중을 가늠 → 정밀 측정(choke-point) 필요 여부 판단용.
        volatile LONG64 _membershipSends = 0;

        // 멤버십 변경 송신(복사) 시간 — ProcessSectorChange 구간 전용(섹터이동분).
        //   game_logic 페이즈(_phaseGameLogicUs)에 섞여 있던 멤버십 복사를 분리 측정 → 묶음 최적화 ROI 판단용.
        //   접속/퇴장(BroadcastEnter/LeaveZone)은 네트워크 페이즈라 1차 제외.
        volatile LONG64 _membershipCostUs = 0;

        // 비용종류별 계측 (counter, 마이크로초 누적) — 1단계: BroadcastAroundSector hot path 전용
        //
        // 기존 _phase*Us는 "루프 단계별"이라 하나의 브로드캐스트 비용이 network/broadcast_sync에
        // 흩어져 섞인다. 아래 3개는 "비용 종류별"로, 호출이 어느 단계든 같은 통에 누적한다.
        // 이로써 "복사(enqueue) vs 송신(flush) 누가 큰가"를 단계 경계와 무관하게 비교 가능.
        //   주의(1단계 범위): BroadcastAroundSector(move/stop/chat/sync/clamp)만 계측.
        //         BroadcastEnter/LeaveZone·ProcessSectorChange(접속/해제/존이동·섹터변경)의
        //         GetAroundPlayers·복사는 미포함 → 필요 시 2단계(RequestSendMsg choke point)로 확장.
        volatile LONG64 _broadcastGatherUs = 0;     // GetAroundPlayers 주변 모으기 (수신자 수 비례)
        volatile LONG64 _broadcastEnqueueUs = 0;    // 수신자별 처리(precount+배치AddRef+RequestSendMsg 복사)
        volatile LONG64 _flushSendUs = 0;           // FlushPendingSends 실제 송신(WSASend) — 틱 끝 1회

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

        // Handle-latency 히스토그램
        //
        // recv 이벤트 enqueue → 게임루프 처리완료(응답 송신 포함) 시간 (ms).
        // 클라 RTT(왕복)에서 이 값을 빼면 네트워크+클라(더미) 기여분이 분리된다.
        // 버킷 경계는 틱과 동일(1~200ms): 40ms 틱 게이트 대기가 지배적이라 범위가 겹침.
        static constexpr int HANDLE_BUCKET_COUNT = 10;
        static constexpr double HANDLE_BUCKET_BOUNDS[HANDLE_BUCKET_COUNT - 1] = {
            1.0, 5.0, 10.0, 20.0, 40.0, 60.0, 80.0, 100.0, 200.0
        };

        volatile LONG64 _handleBuckets[HANDLE_BUCKET_COUNT] = {};
        volatile LONG64 _handleSumUs = 0;   // 합계 (마이크로초)
        volatile LONG64 _handleCount = 0;   // 총 처리 이벤트 수

        // Handle-latency 기록 (밀리초 단위)
        void RecordHandleLatency(double ms)
        {
            LONG64 us = static_cast<LONG64>(ms * 1000.0);
            InterlockedExchangeAdd64(&_handleSumUs, us);
            InterlockedIncrement64(&_handleCount);

            for (int i = 0; i < HANDLE_BUCKET_COUNT - 1; ++i)
            {
                if (ms <= HANDLE_BUCKET_BOUNDS[i])
                {
                    InterlockedIncrement64(&_handleBuckets[i]);
                    return;
                }
            }
            InterlockedIncrement64(&_handleBuckets[HANDLE_BUCKET_COUNT - 1]);
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
        volatile HANDLE threadHandle = nullptr;   // CPU 점유율 측정용 복제 핸들 (워커 시작 시 등록)
    };

    WorkerCounter _workerCounters[MAX_WORKER_THREADS] = {};
    volatile LONG _workerThreadCount = 0;

    // ══════════════════════════════════════════════════════════════
    // 스레드 CPU 점유율 측정용 핸들
    //
    // [목적] 진단정리 6의 "계측 사각지대" 보강 — 게임루프가 무제한 드워커
    //        루프에 갇혀도 외부 관측자(HTTP 스레드)가 GetThreadTimes로 CPU를 읽음
    // [원리] 게임루프 스레드가 시작 시 DuplicateHandle로 실핸들을 복제해 등록
    //        (GetCurrentThread() 의사핸들은 호출 스레드 기준이라 타 스레드에서 못 씀)
    // [수명] 프로세스 종료까지 유지 (단일 핸들, 명시적 Close 생략 — 진단용)
    // ══════════════════════════════════════════════════════════════
    volatile HANDLE _gameLoopThreadHandle = nullptr;

    // [USE_SEND_THREAD] 전용 송신 워커별 카운터 — 워커 카운터(_workerCounters)와 동일 패턴.
    //   alignas(64)로 워커 간 false sharing 차단. 워커이 0개(토글 OFF)면 _sendWorkerCount=0 →
    //   CPU 샘플러·백로그·flushUs 노출이 자동 생략(메트릭이 토글과 함께 꺼짐).
    static constexpr int MAX_SEND_WORKERS = 16;

    struct alignas(64) SendCounter
    {
        volatile HANDLE threadHandle = nullptr;   // CPU 점유율 측정용 복제 핸들 (워커 시작 시 등록)
        volatile LONG64 backlog      = 0;         // 마지막 drain 시 인출한 세션 수 (1틱 dirty 초과 = 송신 못 따라감)
        volatile LONG64 flushUs      = 0;         // 이 워커의 WSASend 누적(us) — 게임루프 flush_send 이전분
    };

    SendCounter   _sendCounters[MAX_SEND_WORKERS] = {};
    volatile LONG _sendWorkerCount = 0;           // 노출 루프 상한 (Start에서 워커 수로 설정)

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
