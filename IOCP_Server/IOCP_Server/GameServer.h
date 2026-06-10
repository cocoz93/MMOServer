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

#include "IOCPServer.h"
#include "MapManager.h"
#include "Player.h"
#include "MonitorManager.h"
#include "Common.h"

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
              const MapConfig* maps, int32_t mapCount);

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
    void SendPacket(CPlayer* target, CSerialBuffer* pMsg)
    {
        if (target->_sessionId != -1)
        {
            pMsg->AddRef();   // 송신용 소유권 — RequestSendMsg(CSerialBuffer*)가 소비
            // 게임루프 송신은 묶어 보낼 수 있음을 표시(Deferred) — 실제 지연 여부는 USE_SEND_COALESCING이 결정
            _network->RequestSendMsg(target->_sessionId, pMsg, SendFlush::Deferred);
        }
        pMsg->SubRef();       // 빌더가 넘긴 소유권 1 회수 (세션 무효여도 안전 회수)
    }

    // 주변 브로드캐스트 (CSerialBuffer — 빌더 RefCount=1 버퍼를 소비)
    void BroadcastAroundSector(CZone* zone, CPlayer* player, CSerialBuffer* pMsg, bool excludeSelf = true)
    {
        _broadcastBuffer.clear();
        CPlayer* exclude = excludeSelf ? player : nullptr;
        zone->GetSectorManager().GetAroundPlayers(
            player->_sectorX, player->_sectorY, _broadcastBuffer, exclude);

        InterlockedIncrement64(&_monitor._gameLoop._broadcastCalls);
        InterlockedExchangeAdd64(&_monitor._gameLoop._broadcastTargets,
            static_cast<LONG64>(_broadcastBuffer.size()));

        // 유효 타겟(세션 보유) 수 선카운트 → 타겟별 AddRef를 1회 배치 AddRef로 압축 (원자연산 N→1)
        // 단일 게임루프 스레드 내 호출이라 두 패스 사이 _sessionId 변동 없음 → 카운트 정합 보장
        size_t validCount = 0;
        for (CPlayer* other : _broadcastBuffer)
        {
            if (other->_sessionId != -1)
                ++validCount;
        }

        if (validCount > 0)
            pMsg->AddRef(static_cast<LONG64>(validCount));   // 타겟별 소유권 일괄 확보

        for (CPlayer* other : _broadcastBuffer)
        {
            if (other->_sessionId == -1)
                continue;
            _network->RequestSendMsg(other->_sessionId, pMsg, SendFlush::Deferred);
        }
        pMsg->SubRef();   // 빌더가 넘긴 소유권 1 회수 (타겟 0명이어도 안전 회수)
    }

    // 패킷별 전송 함수 (Fill + Send)
    void SendZoneInfo(CPlayer* target, CZone* zone);
    void SendCreateMyPlayer(CPlayer* target);
    void SendCreateOtherPlayer(CPlayer* target, CPlayer* player, SpawnReason reason = SpawnReason::NORMAL);
    void SendDeletePlayer(CPlayer* target, CPlayer* player);
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
    std::unordered_map<int64_t, CPlayer*> _sessionToPlayer;

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
};
