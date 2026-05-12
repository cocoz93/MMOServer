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

#include "IOCPServer.h"
#include "ZoneManager.h"
#include "Player.h"

class CSerialBuffer;

// 서버 동작 모드
enum class ServerMode
{
    EchoTest,       // 에코 더미 클라이언트 테스트용 (네트워크만)
    GameServer      // MMO 게임 서버 (네트워크 + 게임 로직)
};

class CGameServer
{
public:
    CGameServer();
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
    void RecvMoveStart(int64_t sessionId, CSerialBuffer* pMsg);
    void RecvMoveStop(int64_t sessionId, CSerialBuffer* pMsg);
    void RecvChat(int64_t sessionId, CSerialBuffer* pMsg);
    void RecvZoneChange(int64_t sessionId, CSerialBuffer* pMsg);

    // 섹터 변경 시 시야 진입/이탈 브로드캐스트
    void ProcessSectorChange(CZone* zone, CPlayer* player,
                             int32_t oldSectorX, int32_t oldSectorY);

    // ── 패킷 전송 추상화 ──

    // 단일 세션에 패킷 전송 (템플릿)
    template <typename T>
    void SendPacket(int64_t sessionId, const T& msg)
    {
        _network->RequestSendMsg(sessionId, reinterpret_cast<const char*>(&msg), sizeof(T));
    }

    // 주변 브로드캐스트 (excludeSelf=true: 본인 제외)
    template <typename T>
    void BroadcastAround(CZone* zone, CPlayer* player, const T& msg, bool excludeSelf = true)
    {
        std::vector<CPlayer*> aroundPlayers;
        CPlayer* exclude = excludeSelf ? player : nullptr;
        zone->GetSectorManager().GetAroundPlayers(
            player->_sectorX, player->_sectorY, aroundPlayers, exclude);

        for (CPlayer* other : aroundPlayers)
        {
            SendPacket(other->_sessionId, msg);
        }
    }

    // 패킷별 전송 함수 (Fill + Send)
    void SendCreateMyPlayer(int64_t sessionId, CPlayer* player);
    void SendCreateOtherPlayer(int64_t sessionId, CPlayer* player);
    void SendDeletePlayer(int64_t sessionId, CPlayer* player);
    void SendMoveStart(int64_t sessionId, CPlayer* player);
    void SendMoveStop(int64_t sessionId, CPlayer* player);
    void SendChat(int64_t sessionId, CPlayer* player, const wchar_t* message);
    void SendSyncPosition(int64_t sessionId, CPlayer* player);
    void SendZoneChangeOk(int64_t sessionId, int32_t mapId, int32_t channelIndex, CPlayer* player);
    void SendZoneChangeFail(int64_t sessionId, uint8_t reason);

    // 이동 검증
    bool ValidateMove(CZone* zone, CPlayer* player, float clientX, float clientY);

    // 패킷 타입별 최소 크기 반환 (0이면 알 수 없는 타입)
    static uint16_t GetExpectedSize(MsgType type);

private:
    ServerMode _mode;

    std::unique_ptr<CIOCPServer> _network;
    CZoneManager _zoneManager;

    std::thread _gameThread;
    std::atomic<bool> _running;
    int _frameCount = 0;

    // 프레임 설정
    static constexpr int FRAME_PER_SEC = 25;
    static constexpr int FRAME_INTERVAL_MS = 1000 / FRAME_PER_SEC;  // 40ms

    // 이동 검증 상수
    static constexpr float MOVE_TOLERANCE = 2.0f;
    static constexpr float MOVE_TOLERANCE_SQ = MOVE_TOLERANCE * MOVE_TOLERANCE;
    static constexpr uint32_t CHEAT_KICK_THRESHOLD = 5;
    static constexpr int SYNC_INTERVAL_FRAMES = 25;  // 1초마다 동기화
    static constexpr int CLEANUP_INTERVAL_FRAMES = 25 * 30;  // 30초마다 빈 채널 정리

    int32_t _defaultMapId = 0;  // 최초 접속 시 입장할 맵
    int _cleanupFrameCount = 0;
};
