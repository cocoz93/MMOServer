// ==========================================================================
// CGameServer — 서버 메인 컨트롤러
//
// [책임]
//  - 서버 모드에 따라 에코 테스트 / MMO 게임 서버 동작 결정
//  - CIOCPServer로부터 NetworkEvent를 Pop하여 게임 로직 처리
//  - 패킷 파싱 및 타입별 핸들러 호출
//  - 게임 루프 스레드 소유 (고정 프레임 Tick)
//  - CMapManager를 통해 다중 존 관리
//
// [사용 흐름]
//  1. main()에서 CGameServer 생성
//  2. Init(ServerMode, ...) → 모드에 따라 내부 구성
//  3. Start() → 네트워크 시작 + (게임 모드 시 게임 루프 시작)
//  4. Stop() → 게임 루프 종료 + 네트워크 종료
// ==========================================================================
#pragma once

#include <cstdint>
#include <thread>
#include <atomic>
#include <memory>
#include <vector>
#include <queue>
#include <unordered_set>
#include <unordered_map>

#include "IOCPServer.h"
#include "MapManager.h"
#include "Player.h"
#include "MonitorManager.h"
#include "Common.h"
#include "DB/DBWorker.h"

class CSerialBuffer;

class CGameServer
{
public:
    explicit CGameServer(CMonitorManager& monitor);
    ~CGameServer();

    // 에코 테스트 모드 초기화
    bool Init(ServerMode mode, int port, int maxClients);

    // 게임 서버 모드 초기화 (맵 설정 배열)
    bool Init(ServerMode mode, int port, int maxClients,
              const MapConfig* maps, int32_t mapCount, int workerThreads = 0, int sendWorkers = 0);

    // DB 저장 파이프라인 초기화 (USE_DB_WORKER=0이면 no-op). Start() 전에 호출.
    bool InitDB(const DBConfig& dbConfig, int savePeriodSec, int workerCount, int queueMax);

    // 서버 시작/종료
    bool Start();
    void Stop();

private:
    // 게임 루프 (별도 스레드, GameServer 모드 전용)
    void GameLoopThread();

    // NetworkEvent 디스패치
    void ProcessNetworkEvents();
    void OnConnected(int64_t sessionId);
    void OnDisconnected(int64_t sessionId);
    void OnReceived(int64_t sessionId, CSerialBuffer* pMsg);

    // 패킷 핸들러
    void RecvMoveStart(CPlayer* player, CSerialBuffer* pMsg);
    void RecvMoveStop(CPlayer* player, CSerialBuffer* pMsg);
    void RecvChat(CPlayer* player, CSerialBuffer* pMsg);
    void RecvZoneChange(CPlayer* player, CSerialBuffer* pMsg);
    void RecvAdminLogin(CPlayer* player, CSerialBuffer* pMsg);

#if USE_DB_WORKER
    // DB 저장 파이프라인 (dirty flag 기반 비동기 위치 저장)
    bool ShouldSave(const CPlayer* player) const;        // 저장 대상 판정(MOVING||dirty) — 주기·종료 공통
    DBSaveJob MakeSaveJob(const CPlayer* player) const;  // 위치 스냅샷 생성 (단건·배치 공용)
    void EnqueuePlayerSave(CPlayer* player);   // 단건: 스냅샷 enqueue + dirty clear (로그아웃)
    void TickPeriodicSave();                   // 주기: 저장 대상 배치를 1회 핸드오프
    void SaveAllPlayers();                     // 종료: 전원 최종 저장(안전망, dirty 무관)
#endif

    // 섹터 변경 시 시야 진입/이탈 브로드캐스트
    void ProcessSectorChange(CZone* zone, CPlayer* player,
                             int32_t oldSectorX, int32_t oldSectorY);

    // 섹터 변경 대기열에 추가 (중복 플레이어는 최초 출발 섹터만 유지)
    void PushSectorChange(CPlayer* player, int32_t oldSectorX, int32_t oldSectorY);

    // 존 입장/퇴장 브로드캐스트 (주변 상호 CREATE/DELETE 통보)
    void BroadcastEnterZone(CZone* zone, CPlayer* player, SpawnReason reason);
    void BroadcastLeaveZone(CZone* zone, CPlayer* player);

    // ── 플레이어 ID 발급 + 표시 속성 ──

    int32_t AllocPlayerId();
    static uint8_t CalcDisplayChar(int32_t playerId);
    static uint8_t CalcColorIndex(int32_t playerId);

    // ── 패킷 전송 추상화 ──
    // 모든 S2C는 직렬화 버퍼(CSerialBuffer)로 조립 후 아래 오버로드로 전송한다.

    // 단일 플레이어에게 패킷 전송 (CSerialBuffer — 소유권 1을 소비)
    // 빌더가 반환한 RefCount=1 버퍼를 넘기면, 송신 후 SubRef로 소유권을 회수한다.
    void SendPacket(CPlayer* target, CSerialBuffer* pMsg);

    // 주변 브로드캐스트 (CSerialBuffer — 빌더 RefCount=1 버퍼를 소비)
    void BroadcastAroundSector(CZone* zone, CPlayer* player, CSerialBuffer* pMsg, bool excludeSelf = true);

    // ── 섹터 묶음 경로 (USE_SECTOR_AGGREGATION) ──
    void MarkMoveDirty(CPlayer* player);   // 즉시 브로드캐스트 대신 dirty 등록 (중복 방지)
    void FlushSectorUpdates();             // 틱 끝: dirty를 섹터별로 묶어 주변에 송신
    // 섹터 좌표 기준 주변 9섹터에 묶음 패킷 전송 (BroadcastAroundSector의 소유권/계측 패턴 복제)
    void BroadcastSectorPacket(CZone* zone, int32_t sectorX, int32_t sectorY, CSerialBuffer* pMsg);

#if USE_BROADCAST_DIGEST
    // ── 수신섹터 digest 경로 (Phase 3) — FlushSectorUpdates를 대체 ──
    // 소스 섹터별 보류물(이동 번들 + 채팅)을 수신 섹터 기준으로 뒤집어, 이웃 9섹터 내용을
    // raw 바이트 1덩어리로 연접 후 주민당 RequestSendRaw 1회. 같은 섹터 주민은 수신 집합이 동일해 공유 가능.
    void RegisterSectorItem(CZone* zone, int32_t zoneId, int32_t sectorX, int32_t sectorY,
                            CSerialBuffer* pMsg);   // 보류 등록 (버퍼 소유권 1을 digest 배포 후 회수)
    void FlushSectorDigests();                      // 틱 끝: 번들 빌드 → 수신섹터 연접 → 배포 → 일괄 해제
#endif

#if USE_MEMBERSHIP_FANOUT_DEDUP
    // 멤버십 아웃바운드 팬아웃 (Phase 1) — 미리 빌드한 버퍼 1개를 여러 섹터의 플레이어에게 배치 AddRef로 전송.
    // BroadcastSectorPacket의 소유권/계측 패턴을 (주변 9섹터가 아닌) 임의 섹터 배열 버전으로 확장. 버퍼 소유권 1을 소비.
    void FanoutToSectors(CZone* zone,
                         const CSectorManager::SectorPos* sectors, int32_t sectorCount,
                         CSerialBuffer* pMsg, CPlayer* exclude);
#endif

    // 패킷별 전송 함수 (Fill + Send)
    void SendZoneInfo(CPlayer* target, CZone* zone);
    void SendCreateMyPlayer(CPlayer* target);
    void SendCreateOtherPlayer(CPlayer* target, CPlayer* player, SpawnReason reason = SpawnReason::NORMAL);
    void SendDeletePlayer(CPlayer* target, CPlayer* player);
#if USE_MEMBERSHIP_INBOUND_BUNDLE
    // [Phase 2] 인바운드 멤버십 배치 — 상대 목록을 배치 상한씩 잘라 mover 1명에게 송신 (엔트리당 _membershipSends 1 집계)
    void SendCreatePlayerBatch(CPlayer* target, CPlayer* const* others, int count);
    void SendDeletePlayerBatch(CPlayer* target, CPlayer* const* others, int count);
#endif
    void SendSyncPosition(CPlayer* target);
    void SendZoneChangeOk(CPlayer* target, int32_t mapId, int32_t channelIndex);
    void SendZoneChangeFail(CPlayer* target, uint8_t reason);

    // 벽 방향 검증 — 경계 위치에서 벽 쪽 이동 차단
    bool IsBlockedByWall(CZone* zone, CPlayer* player, Direction dir);

    // 패킷 타입별 최소 크기 반환 (0이면 알 수 없는 타입)
    static uint16_t GetExpectedSize(MsgType type);

private:
    ServerMode _mode;

    CMonitorManager& _monitor;
    std::unique_ptr<CIOCPServer> _network;
    CMapManager _mapManager;

    std::thread _gameThread;
    std::atomic<bool> _running;
    // 프레임 설정
    static constexpr int FRAME_PER_SEC = 25;
    static constexpr int FRAME_INTERVAL_MS = 1000 / FRAME_PER_SEC;  // 40ms

    static constexpr int CLEANUP_INTERVAL_FRAMES = 25 * 30;  // 30초마다 빈 채널 정리
    static constexpr int SYNC_INTERVAL_FRAMES = 13;          // 13 × 40ms ≈ 500ms 주기적 위치 동기화
    static constexpr float SYNC_DISTANCE_THRESHOLD_SQ = 4.0f; // 델타 동기화 임계값 제곱 (2타일)
    static constexpr float MOVE_START_ACCEPT_DIST_SQ = 64.0f; // C2S_MOVE_START 좌표 수용 임계값 제곱 (8타일)

    int32_t _defaultMapId = 0;  // 최초 접속 시 입장할 맵
    int32_t _nextPlayerId = 1;  // 전역 playerId 카운터 (싱글스레드 게임 루프)

    // 네트워크 경계: sessionId → CPlayer* (수신 시 플레이어 조회)
    //   sessionId 상위 16비트 = 세션 슬롯 인덱스 → 해시맵 대신 인덱스 배열 직접 접근 (캐시미스 제거).
    //   슬롯 재사용(ABA)은 FindPlayer의 _sessionId 일치 검사로 방어. 접근은 게임 스레드 단독.
    std::vector<CPlayer*> _sessionSlots;

    // 슬롯 조회 + 재사용 가드 — 등록된 세션이 아니면 nullptr
    CPlayer* FindPlayer(int64_t sessionId) const
    {
        uint16_t idx = CSession::ExtractIndex(sessionId);
        if (idx >= _sessionSlots.size())
            return nullptr;
        CPlayer* p = _sessionSlots[idx];
        return (p != nullptr && p->_sessionId == sessionId) ? p : nullptr;
    }

    // 프레임 진입 시점 이벤트를 스왑해 받는 로컬 큐 (멤버 재사용 → deque 블록 보존으로 매 프레임 재할당 방지)
    std::queue<NetworkEvent> _localEvents;
    int _cleanupFrameCount = 0;
    int _syncFrameCount = 0;

    // 섹터 변경 배치 처리용 대기열 (프레임 내 수집 → 틱 후 일괄 처리)
    std::vector<SectorChangeInfo> _pendingSectorChanges;
    std::unordered_set<CPlayer*> _sectorChangedSet;  // 이미 기록된 플레이어 필터

    // 프레임 재사용 버퍼 (힙 할당 방지 — clear()로 capacity 유지)
    std::vector<CPlayer*> _broadcastBuffer;       // BroadcastAroundSector 전용
    std::vector<CPlayer*> _eventAroundBuffer;     // 접속/해제/존이동 전용
    std::vector<SectorChangeInfo> _tickSectorChanges;
    std::vector<CPlayer*> _tickClampedPlayers;

    // USE_SECTOR_AGGREGATION: 이번 틱 이동/sync로 상태가 바뀐 플레이어 (틱 끝에 섹터별 묶음 송신)
    std::vector<CPlayer*> _dirtyMovers;

#if USE_BROADCAST_DIGEST
    // USE_BROADCAST_DIGEST: 존별 섹터 그리드에 이번 틱 보류물(번들+채팅)을 모았다가 틱 끝 digest로 배포.
    // zoneId 키로 보관 — 틱 중 존이 사라져도 해제 루프는 존 포인터 없이 안전 (기존 GetZone null 가드 패턴).
    struct ZonePending
    {
        int32_t countX = 0, countY = 0;                  // 섹터 그리드 크기 (존 최초 등록 시 확정)
        std::vector<std::vector<CSerialBuffer*>> items;  // flatIdx → 보류 버퍼들 (등록순)
        std::vector<uint64_t> recvEpoch;                 // 수신섹터 중복 마킹 (틱당 epoch 증가로 clear 생략)
    };
    std::unordered_map<int32_t, ZonePending> _pendingByZone;
    std::vector<std::pair<int32_t, int32_t>> _touchedSectors;   // (zoneId, flatIdx) — 보류물 있는 소스 섹터
    std::vector<std::pair<int32_t, int32_t>> _receiverSectors;  // (zoneId, flatIdx) — 이번 틱 수신 후보 (재사용)
    std::vector<char> _digestBuf;                               // 수신섹터별 연접 버퍼 (재사용)
    uint64_t _digestEpoch = 0;                                  // 64비트 — wrap 없음 (0 = 미마킹 초기값과 충돌 방지)
#endif

#if USE_MEMBERSHIP_INBOUND_BUNDLE
    // USE_MEMBERSHIP_INBOUND_BUNDLE: 인바운드 멤버십 배치 수집 전용 (ProcessSectorChange 내 수집→직렬화→송신 완결, 이월 없음)
    std::vector<CPlayer*> _membershipInboundBuffer;
#endif

    // 비용종류별 계측 — 틱 내 누적 후 틱 끝 1회 모니터 반영 (단일 게임루프 스레드 전용 → 누적 자체엔 원자연산 불필요)
    // BroadcastAroundSector가 network/game_logic/broadcast_sync 어느 단계에서 호출돼도 이 멤버에 합산된다.
    int64_t _tickBroadcastGatherUs = 0;   // GetAroundPlayers 주변 모으기
    int64_t _tickBroadcastEnqueueUs = 0;  // 수신자별 처리(복사 포함)
    int64_t _tickMembershipSends = 0;     // 멤버십 변경 복사(BroadcastAroundSector 밖 경로) 송신 횟수
    int64_t _tickMembershipUs = 0;        // 멤버십 송신(ProcessSectorChange 구간) 시간 — 틱 끝 _membershipCostUs로 반영

#if USE_DB_WORKER
    // DB 저장 파이프라인 (dirty flag 기반 비동기 위치 저장)
    std::unique_ptr<CDBWorker> _dbWorker;
    int _dbSavePeriodFrames = 250;   // SavePeriodSec × 25fps (InitDB에서 설정)
    int _dbSaveFrameCounter  = 0;
#endif
};
