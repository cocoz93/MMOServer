// ==========================================================================
// CGameServer — 서버 메인 컨트롤러
//
// [책임]
//  - 서버 모드에 따라 에코 테스트 / MMO 게임 서버 동작 결정
//  - CIOCPServer로부터 NetworkEvent를 Pop하여 게임 로직 처리
//  - 패킷 파싱 및 타입별 핸들러 호출
//  - 게임 루프 스레드 소유 (고정 프레임 Tick)
//  - CZoneManager를 통해 다중 존 관리
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
#include <unordered_set>

#include "IOCPServer.h"
#include "ZoneManager.h"
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

    ServerMode GetMode() const { return _mode; }

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

    // 섹터 변경 시 시야 진입/이탈 브로드캐스트
    void ProcessSectorChange(CZone* zone, CPlayer* player,
                             int32_t oldSectorX, int32_t oldSectorY);

    // 섹터 변경 대기열에 추가 (중복 플레이어는 최초 출발 섹터만 유지)
    void PushSectorChange(CPlayer* player, int32_t oldSectorX, int32_t oldSectorY);

    // ── 플레이어 ID 발급 + 표시 속성 ──

    int32_t AllocPlayerId();
    static uint8_t CalcDisplayChar(int32_t playerId);
    static uint8_t CalcColorIndex(int32_t playerId);

    // ── 계층 경계 헬퍼 (playerId → sessionId 변환) ──

    // playerId → sessionId 변환 (없으면 -1)
    int64_t GetSessionId(CPlayer* player) const;

    // 컨텐츠 레이어에서 연결 해제 요청
    void DisconnectPlayer(CPlayer* player);

    // ── 패킷 전송 추상화 ──

    // 단일 플레이어에게 패킷 전송 (템플릿)
    template <typename T>
    void SendPacket(CPlayer* target, const T& msg)
    {
        int64_t sid = GetSessionId(target);
        if (sid != -1)
            _network->RequestSendMsg(sid, reinterpret_cast<const char*>(&msg), sizeof(T));
    }

    // 주변 브로드캐스트 (excludeSelf=true: 본인 제외)
    template <typename T>
    void BroadcastAround(CZone* zone, CPlayer* player, const T& msg, bool excludeSelf = true)
    {
        _broadcastBuffer.clear();
        CPlayer* exclude = excludeSelf ? player : nullptr;
        zone->GetSectorManager().GetAroundPlayers(
            player->_sectorX, player->_sectorY, _broadcastBuffer, exclude);

        for (CPlayer* other : _broadcastBuffer)
        {
            SendPacket(other, msg);
        }
    }

    // 패킷별 전송 함수 (Fill + Send)
    void SendZoneInfo(CPlayer* target, CZone* zone);
    void SendCreateMyPlayer(CPlayer* target);
    void SendCreateOtherPlayer(CPlayer* target, CPlayer* player);
    void SendDeletePlayer(CPlayer* target, CPlayer* player);
    void SendMoveStart(CPlayer* target, CPlayer* player);
    void SendMoveStop(CPlayer* target, CPlayer* player);
    void SendChat(CPlayer* target, CPlayer* player, const wchar_t* message);
    void SendSyncPosition(CPlayer* target);
    void SendZoneChangeOk(CPlayer* target, int32_t mapId, int32_t channelIndex);
    void SendZoneChangeFail(CPlayer* target, uint8_t reason);

    // 이동 검증
    bool ValidateMove(CZone* zone, CPlayer* player, float clientX, float clientY);

    // 패킷 타입별 최소 크기 반환 (0이면 알 수 없는 타입)
    static uint16_t GetExpectedSize(MsgType type);

private:
    ServerMode _mode;

    CMonitorManager& _monitor;
    std::unique_ptr<CIOCPServer> _network;
    CZoneManager _zoneManager;

    std::thread _gameThread;
    std::atomic<bool> _running;
    // 프레임 설정
    static constexpr int FRAME_PER_SEC = 25;
    static constexpr int FRAME_INTERVAL_MS = 1000 / FRAME_PER_SEC;  // 40ms

    // 이동 검증 상수
    static constexpr float MOVE_TOLERANCE_BASE = 2.0f;  // 고정 여유값
    static constexpr uint32_t CHEAT_KICK_THRESHOLD = 5;
    static constexpr int CLEANUP_INTERVAL_FRAMES = 25 * 30;  // 30초마다 빈 채널 정리

    int32_t _defaultMapId = 0;  // 최초 접속 시 입장할 맵
    int32_t _nextPlayerId = 1;  // 전역 playerId 카운터 (싱글스레드 게임 루프)

    // 경계 계층: 네트워크(sessionId) ↔ 컨텐츠(playerId) 양방향 매핑
    std::unordered_map<int64_t, CPlayer*> _sessionToPlayer;   // sessionId → CPlayer*
    std::unordered_map<int32_t, int64_t> _playerToSession;    // playerId → sessionId
    int _cleanupFrameCount = 0;

    // 섹터 변경 배치 처리용 대기열 (프레임 내 수집 → 틱 후 일괄 처리)
    std::vector<SectorChangeInfo> _pendingSectorChanges;
    std::unordered_set<CPlayer*> _sectorChangedSet;  // 이미 기록된 플레이어 필터

    // 프레임 재사용 버퍼 (힙 할당 방지 — clear()로 capacity 유지)
    std::vector<CPlayer*> _broadcastBuffer;
    std::vector<SectorChangeInfo> _tickSectorChanges;
    std::vector<CPlayer*> _tickClampedPlayers;
};
