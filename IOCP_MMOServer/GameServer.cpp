#include "GameServer.h"
#include "Player.h"
#include "Protocol.h"
#include "SerialBuffer.h"
#include <iostream>
#include <chrono>
#include <cmath>

CGameServer::CGameServer()
    : _mode(ServerMode::GameServer)
    , _running(false)
{
}

CGameServer::~CGameServer()
{
    Stop();
}

bool CGameServer::Init(ServerMode mode, int port, int maxClients)
{
    return Init(mode, port, maxClients, nullptr, 0);
}

bool CGameServer::Init(ServerMode mode, int port, int maxClients,
                        const MapConfig* maps, int32_t mapCount)
{
    _mode = mode;

    // 모드에 따라 네트워크 아키텍처 타입 결정
    ServerArchitectureType archType = (_mode == ServerMode::EchoTest)
        ? ServerArchitectureType::GameCodiEchoTest
        : ServerArchitectureType::Centralized;

    _network = std::make_unique<CIOCPServer>(port, maxClients, archType);

    // 게임 서버 모드일 때만 맵 등록
    if (_mode == ServerMode::GameServer && maps != nullptr)
    {
        for (int32_t i = 0; i < mapCount; ++i)
        {
            if (!_zoneManager.RegisterMap(maps[i]))
                return false;
        }

        // 첫 번째 맵을 기본 접속 맵으로 설정
        _defaultMapId = maps[0].mapId;
    }

    return true;
}

bool CGameServer::Start()
{
    if (!_network->Start())
        return false;

    // 게임 서버 모드일 때만 게임 루프 스레드 시작
    // 에코 테스트는 CIOCPServer 내부에서 자체 처리
    if (_mode == ServerMode::GameServer)
    {
        _running = true;
        _gameThread = std::thread(&CGameServer::GameLoopThread, this);
    }

    const char* modeName = (_mode == ServerMode::EchoTest) ? "EchoTest" : "GameServer";
    std::cout << "[GameServer] Started - Mode: " << modeName << std::endl;

    return true;
}

void CGameServer::Stop()
{
    _running = false;

    if (_gameThread.joinable())
        _gameThread.join();

    if (_network)
    {
        _network->ShutdownServer();

        // 미처리 이벤트의 CSerialBuffer 정리
        NetworkEvent event(NetworkEvent::Type::CONNECTED, 0);
        while (_network->PopNetworkEvent(event))
        {
            if (event.pMsg != nullptr)
                CSerialBuffer::Free(event.pMsg);
        }
    }
}

// ==========================================================================
// 게임 루프
// ==========================================================================

void CGameServer::GameLoopThread()
{
    using Clock = std::chrono::steady_clock;

    auto prevTime = Clock::now();

    while (_running)
    {
        auto frameStart = Clock::now();

        // deltaTime 계산
        float deltaTime = std::chrono::duration<float>(frameStart - prevTime).count();
        prevTime = frameStart;

        // 1) 네트워크 이벤트 전부 소비
        ProcessNetworkEvents();

        // 2) 게임 로직 갱신 (좌표 이동 + 섹터 변경 감지 + 경계 클램핑)
        std::vector<SectorChangeInfo> sectorChanges;
        std::vector<CPlayer*> clampedPlayers;
        _zoneManager.TickAll(deltaTime, sectorChanges, clampedPlayers);

        // 3) 섹터 변경 브로드캐스트
        for (const auto& change : sectorChanges)
        {
            CZone* zone = _zoneManager.FindZoneBySession(change.player->_sessionId);
            if (zone != nullptr)
            {
                ProcessSectorChange(zone, change.player,
                                    change.oldSectorX, change.oldSectorY);
            }
        }

        // 3-1) 맵 경계 클램핑으로 정지된 플레이어에게 MOVE_STOP 브로드캐스트
        for (CPlayer* player : clampedPlayers)
        {
            CZone* zone = _zoneManager.FindZoneBySession(player->_sessionId);
            if (zone != nullptr)
            {
                MSG_S2C_MOVE_STOP msg;
                msg.playerId = player->_playerId;
                msg.direction = static_cast<uint8_t>(player->_direction);
                msg.x = player->_x;
                msg.y = player->_y;
                BroadcastAround(zone, player, msg, false);  // 본인 포함
            }
        }

        // 4) 이동 중 플레이어에게 주기적 좌표 동기화
        ++_frameCount;
        if (_frameCount >= SYNC_INTERVAL_FRAMES)
        {
            _frameCount = 0;
            for (const auto& zonePair : _zoneManager.GetZones())
            {
                for (const auto& playerPair : zonePair.second->GetPlayers())
                {
                    CPlayer* p = playerPair.second;
                    if (p->_moveState == MoveState::MOVING)
                    {
                        SendSyncPosition(p->_sessionId, p);
                    }
                }
            }
        }

        // 5) 빈 동적 채널 정리
        ++_cleanupFrameCount;
        if (_cleanupFrameCount >= CLEANUP_INTERVAL_FRAMES)
        {
            _cleanupFrameCount = 0;
            _zoneManager.CleanupEmptyChannels();
        }

        // 6) 프레임 제한
        auto frameEnd = Clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(frameEnd - frameStart);
        int sleepMs = FRAME_INTERVAL_MS - static_cast<int>(elapsed.count());
        if (sleepMs > 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
        }
    }
}

// ==========================================================================
// 이벤트 디스패치
// ==========================================================================

void CGameServer::ProcessNetworkEvents()
{
    NetworkEvent event(NetworkEvent::Type::CONNECTED, 0);

    while (_network->PopNetworkEvent(event))
    {
        switch (event.type)
        {
        case NetworkEvent::Type::CONNECTED:
            OnConnected(event.sessionId);
            break;

        case NetworkEvent::Type::DISCONNECTED:
            OnDisconnected(event.sessionId);
            break;

        case NetworkEvent::Type::RECEIVED:
            OnReceived(event.sessionId, event.pMsg);
            break;
        }
    }
}

void CGameServer::OnConnected(int64_t sessionId)
{
    // 이미 등록된 세션이면 무시 (중복 접속 방어)
    if (_zoneManager.FindZoneBySession(sessionId) != nullptr)
    {
        _network->RequestDisconnectSession(sessionId);
        return;
    }

    // 기본 맵의 여유 채널에 입장
    CZone* zone = _zoneManager.FindOrCreateChannel(_defaultMapId);
    if (zone == nullptr)
    {
        _network->RequestDisconnectSession(sessionId);
        return;
    }

    CPlayer* player = zone->EnterZone(sessionId);
    if (player == nullptr)
    {
        _network->RequestDisconnectSession(sessionId);
        return;
    }

    // 세션 → 존 매핑 등록
    _zoneManager.RegisterSession(sessionId, zone->GetZoneId());

    // 1) 본인에게 내 캐릭터 생성
    SendCreateMyPlayer(sessionId, player);

    // 2) 주변 플레이어 수집
    std::vector<CPlayer*> aroundPlayers;
    zone->GetSectorManager().GetAroundPlayers(
        player->_sectorX, player->_sectorY, aroundPlayers, player);

    // 3) 주변 플레이어에게 새 캐릭터 등장
    for (CPlayer* other : aroundPlayers)
    {
        SendCreateOtherPlayer(other->_sessionId, player);
    }

    // 4) 본인에게 주변 기존 플레이어들 정보
    for (CPlayer* other : aroundPlayers)
    {
        SendCreateOtherPlayer(sessionId, other);
    }
}

void CGameServer::OnDisconnected(int64_t sessionId)
{
    CZone* zone = _zoneManager.FindZoneBySession(sessionId);
    if (zone == nullptr)
        return;

    CPlayer* player = zone->FindPlayer(sessionId);
    if (player == nullptr)
        return;

    // 주변에 DELETE 브로드캐스트
    std::vector<CPlayer*> aroundPlayers;
    zone->GetSectorManager().GetAroundPlayers(
        player->_sectorX, player->_sectorY, aroundPlayers, player);

    for (CPlayer* other : aroundPlayers)
    {
        SendDeletePlayer(other->_sessionId, player);
    }

    // 섹터 해제 + 플레이어 삭제
    zone->LeaveZone(sessionId);

    // 세션 → 존 매핑 해제
    _zoneManager.UnregisterSession(sessionId);
}

void CGameServer::OnReceived(int64_t sessionId, CSerialBuffer* pMsg)
{
    if (pMsg == nullptr)
        return;

    // 헤더에서 패킷 타입 읽기
    MsgHeader header;
    pMsg->PeekData(reinterpret_cast<char*>(&header), sizeof(header));

    // 타입별 최소 패킷 크기 검증
    uint16_t expectedSize = GetExpectedSize(header.type);
    if (expectedSize == 0 || header.size < expectedSize)
    {
        CSerialBuffer::Free(pMsg);
        return;
    }

    switch (header.type)
    {
    case MsgType::C2S_MOVE_START:
        RecvMoveStart(sessionId, pMsg);
        break;

    case MsgType::C2S_MOVE_STOP:
        RecvMoveStop(sessionId, pMsg);
        break;

    case MsgType::C2S_CHAT:
        RecvChat(sessionId, pMsg);
        break;

    case MsgType::C2S_ZONE_CHANGE:
        RecvZoneChange(sessionId, pMsg);
        break;

    default:
        break;
    }

    // SerialBuffer 해제
    CSerialBuffer::Free(pMsg);
}

uint16_t CGameServer::GetExpectedSize(MsgType type)
{
    switch (type)
    {
    case MsgType::C2S_MOVE_START:   return sizeof(MSG_C2S_MOVE_START);
    case MsgType::C2S_MOVE_STOP:    return sizeof(MSG_C2S_MOVE_STOP);
    case MsgType::C2S_CHAT:         return sizeof(MSG_C2S_CHAT);
    case MsgType::C2S_ZONE_CHANGE:  return sizeof(MSG_C2S_ZONE_CHANGE);
    default:                        return 0;
    }
}

// ==========================================================================
// 패킷 핸들러
// ==========================================================================

void CGameServer::RecvMoveStart(int64_t sessionId, CSerialBuffer* pMsg)
{
    CZone* zone = _zoneManager.FindZoneBySession(sessionId);
    if (zone == nullptr)
        return;

    CPlayer* player = zone->FindPlayer(sessionId);
    if (player == nullptr)
        return;

    MSG_C2S_MOVE_START recvMsg;
    pMsg->GetData(reinterpret_cast<char*>(&recvMsg), sizeof(recvMsg));

    // 이미 이동 중이면 무시 (중복 MOVE_START 방지)
    if (player->_moveState == MoveState::MOVING)
        return;

    // Direction 범위 검증
    if (recvMsg.direction < static_cast<uint8_t>(Direction::UP) ||
        recvMsg.direction > static_cast<uint8_t>(Direction::RIGHT))
        return;

    // 플레이어 상태 갱신
    player->_direction = static_cast<Direction>(recvMsg.direction);
    player->_moveState = MoveState::MOVING;

    // 주변에 MOVE_START 브로드캐스트
    MSG_S2C_MOVE_START msg;
    msg.playerId = player->_playerId;
    msg.direction = static_cast<uint8_t>(player->_direction);
    msg.x = player->_x;
    msg.y = player->_y;
    BroadcastAround(zone, player, msg);
}

void CGameServer::RecvMoveStop(int64_t sessionId, CSerialBuffer* pMsg)
{
    CZone* zone = _zoneManager.FindZoneBySession(sessionId);
    if (zone == nullptr)
        return;

    CPlayer* player = zone->FindPlayer(sessionId);
    if (player == nullptr)
        return;

    MSG_C2S_MOVE_STOP recvMsg;
    pMsg->GetData(reinterpret_cast<char*>(&recvMsg), sizeof(recvMsg));

    // 이동 중이 아니면 무시 (IDLE 상태에서 STOP 방지)
    if (player->_moveState != MoveState::MOVING)
        return;

    // Direction 범위 검증
    if (recvMsg.direction < static_cast<uint8_t>(Direction::UP) ||
        recvMsg.direction > static_cast<uint8_t>(Direction::RIGHT))
        return;

    // 방향 갱신
    player->_direction = static_cast<Direction>(recvMsg.direction);
    player->_moveState = MoveState::IDLE;

    // 이동 검증 — 통과 시 클라이언트 좌표 수용, 실패 시 서버 좌표 유지
    if (ValidateMove(zone, player, recvMsg.x, recvMsg.y))
    {
        player->_x = recvMsg.x;
        player->_y = recvMsg.y;
    }

    // 섹터 이동 판정
    int32_t newSectorX = zone->GetSectorManager().CalcSectorX(player->_x);
    int32_t newSectorY = zone->GetSectorManager().CalcSectorY(player->_y);

    if (newSectorX != player->_sectorX || newSectorY != player->_sectorY)
    {
        int32_t oldSectorX = player->_sectorX;
        int32_t oldSectorY = player->_sectorY;

        // 섹터 데이터 갱신
        zone->GetSectorManager().RemovePlayer(player, oldSectorX, oldSectorY);
        player->_sectorX = newSectorX;
        player->_sectorY = newSectorY;
        zone->GetSectorManager().AddPlayer(player, newSectorX, newSectorY);

        // 시야 진입/이탈 브로드캐스트
        ProcessSectorChange(zone, player, oldSectorX, oldSectorY);
    }

    // 주변에 MOVE_STOP 브로드캐스트
    MSG_S2C_MOVE_STOP msg;
    msg.playerId = player->_playerId;
    msg.direction = static_cast<uint8_t>(player->_direction);
    msg.x = player->_x;
    msg.y = player->_y;
    BroadcastAround(zone, player, msg);
}

void CGameServer::RecvChat(int64_t sessionId, CSerialBuffer* pMsg)
{
    CZone* zone = _zoneManager.FindZoneBySession(sessionId);
    if (zone == nullptr)
        return;

    CPlayer* player = zone->FindPlayer(sessionId);
    if (player == nullptr)
        return;

    MSG_C2S_CHAT recvMsg;
    pMsg->GetData(reinterpret_cast<char*>(&recvMsg), sizeof(recvMsg));

    // 주변에 채팅 브로드캐스트 (본인 포함)
    MSG_S2C_CHAT msg;
    msg.playerId = player->_playerId;
    memcpy(msg.message, recvMsg.message, sizeof(recvMsg.message));
    BroadcastAround(zone, player, msg, false);
}

// ==========================================================================
// 패킷 전송 추상화
// ==========================================================================

void CGameServer::SendCreateMyPlayer(int64_t sessionId, CPlayer* player)
{
    MSG_S2C_CREATE_MY_PLAYER msg;
    msg.playerId = player->_playerId;
    msg.direction = static_cast<uint8_t>(player->_direction);
    msg.x = player->_x;
    msg.y = player->_y;
    SendPacket(sessionId, msg);
}

void CGameServer::SendCreateOtherPlayer(int64_t sessionId, CPlayer* player)
{
    MSG_S2C_CREATE_OTHER_PLAYER msg;
    msg.playerId = player->_playerId;
    msg.direction = static_cast<uint8_t>(player->_direction);
    msg.moveState = static_cast<uint8_t>(player->_moveState);
    msg.x = player->_x;
    msg.y = player->_y;
    SendPacket(sessionId, msg);
}

void CGameServer::SendDeletePlayer(int64_t sessionId, CPlayer* player)
{
    MSG_S2C_DELETE_PLAYER msg;
    msg.playerId = player->_playerId;
    SendPacket(sessionId, msg);
}

void CGameServer::SendMoveStart(int64_t sessionId, CPlayer* player)
{
    MSG_S2C_MOVE_START msg;
    msg.playerId = player->_playerId;
    msg.direction = static_cast<uint8_t>(player->_direction);
    msg.x = player->_x;
    msg.y = player->_y;
    SendPacket(sessionId, msg);
}

void CGameServer::SendMoveStop(int64_t sessionId, CPlayer* player)
{
    MSG_S2C_MOVE_STOP msg;
    msg.playerId = player->_playerId;
    msg.direction = static_cast<uint8_t>(player->_direction);
    msg.x = player->_x;
    msg.y = player->_y;
    SendPacket(sessionId, msg);
}

void CGameServer::SendChat(int64_t sessionId, CPlayer* player, const wchar_t* message)
{
    MSG_S2C_CHAT msg;
    msg.playerId = player->_playerId;
    memcpy(msg.message, message, sizeof(msg.message));
    SendPacket(sessionId, msg);
}

void CGameServer::SendSyncPosition(int64_t sessionId, CPlayer* player)
{
    MSG_S2C_SYNC_POSITION msg;
    msg.x = player->_x;
    msg.y = player->_y;
    SendPacket(sessionId, msg);
}

void CGameServer::RecvZoneChange(int64_t sessionId, CSerialBuffer* pMsg)
{
    CZone* oldZone = _zoneManager.FindZoneBySession(sessionId);
    if (oldZone == nullptr)
        return;

    CPlayer* oldPlayer = oldZone->FindPlayer(sessionId);
    if (oldPlayer == nullptr)
        return;

    MSG_C2S_ZONE_CHANGE recvMsg;
    pMsg->GetData(reinterpret_cast<char*>(&recvMsg), sizeof(recvMsg));

    int32_t targetMapId = recvMsg.targetMapId;

    // 랜덤 맵 이동 요청 처리
    if (targetMapId == -1)
    {
        int32_t currentMapId = CZoneManager::GetMapIdFromZoneId(oldZone->GetZoneId());
        targetMapId = _zoneManager.GetRandomMapId(currentMapId);
        if (targetMapId == -1)
        {
            SendZoneChangeFail(sessionId, 0);
            return;
        }
    }

    // 대상 맵의 여유 채널 찾기
    CZone* newZone = _zoneManager.FindOrCreateChannel(targetMapId);
    if (newZone == nullptr)
    {
        // 존재하지 않는 맵
        SendZoneChangeFail(sessionId, 0);
        return;
    }

    // ── 현재 존에서 퇴장 (OnDisconnected와 동일한 흐름) ──

    // 주변에 DELETE 브로드캐스트
    std::vector<CPlayer*> aroundPlayers;
    oldZone->GetSectorManager().GetAroundPlayers(
        oldPlayer->_sectorX, oldPlayer->_sectorY, aroundPlayers, oldPlayer);

    for (CPlayer* other : aroundPlayers)
    {
        SendDeletePlayer(other->_sessionId, oldPlayer);
    }

    // 섹터 해제 + 플레이어 삭제
    oldZone->LeaveZone(sessionId);

    // 세션 → 존 매핑 해제
    _zoneManager.UnregisterSession(sessionId);

    // ── 새 존에 입장 (OnConnected와 동일한 흐름) ──

    CPlayer* newPlayer = newZone->EnterZone(sessionId);
    if (newPlayer == nullptr)
    {
        // 입장 실패 시 원래 맵의 여유 채널로 복귀 시도
        // 참고: FindOrCreateChannel이므로 원래 채널이 아닌 같은 맵의 다른 채널에 배정될 수 있음
        CZone* fallback = _zoneManager.FindOrCreateChannel(CZoneManager::GetMapIdFromZoneId(oldZone->GetZoneId()));
        if (fallback != nullptr)
        {
            CPlayer* fbPlayer = fallback->EnterZone(sessionId);
            if (fbPlayer != nullptr)
            {
                _zoneManager.RegisterSession(sessionId, fallback->GetZoneId());
                SendZoneChangeFail(sessionId, 1);

                // 복귀한 존에서 본인 + 주변 상호 통보 (OnConnected와 동일)
                SendCreateMyPlayer(sessionId, fbPlayer);

                aroundPlayers.clear();
                fallback->GetSectorManager().GetAroundPlayers(
                    fbPlayer->_sectorX, fbPlayer->_sectorY, aroundPlayers, fbPlayer);

                for (CPlayer* other : aroundPlayers)
                {
                    SendCreateOtherPlayer(other->_sessionId, fbPlayer);
                }
                for (CPlayer* other : aroundPlayers)
                {
                    SendCreateOtherPlayer(sessionId, other);
                }

                return;
            }
        }

        // 복귀도 실패 → 좀비 방지를 위해 연결 해제
        SendZoneChangeFail(sessionId, 1);
        _network->RequestDisconnectSession(sessionId);
        return;
    }

    // 세션 → 존 매핑 등록
    _zoneManager.RegisterSession(sessionId, newZone->GetZoneId());

    // 본인에게 존 이동 성공 통보
    int32_t channelIndex = CZoneManager::GetChannelIndexFromZoneId(newZone->GetZoneId());
    SendZoneChangeOk(sessionId, targetMapId, channelIndex, newPlayer);

    // 주변 플레이어에게 새 캐릭터 등장
    aroundPlayers.clear();
    newZone->GetSectorManager().GetAroundPlayers(
        newPlayer->_sectorX, newPlayer->_sectorY, aroundPlayers, newPlayer);

    for (CPlayer* other : aroundPlayers)
    {
        SendCreateOtherPlayer(other->_sessionId, newPlayer);
    }

    // 본인에게 주변 기존 플레이어들 정보
    for (CPlayer* other : aroundPlayers)
    {
        SendCreateOtherPlayer(sessionId, other);
    }
}

void CGameServer::SendZoneChangeOk(int64_t sessionId, int32_t mapId, int32_t channelIndex, CPlayer* player)
{
    MSG_S2C_ZONE_CHANGE_OK msg;
    msg.mapId = mapId;
    msg.channelIndex = channelIndex;
    msg.playerId = player->_playerId;
    msg.x = player->_x;
    msg.y = player->_y;
    SendPacket(sessionId, msg);
}

void CGameServer::SendZoneChangeFail(int64_t sessionId, uint8_t reason)
{
    MSG_S2C_ZONE_CHANGE_FAIL msg;
    msg.reason = reason;
    SendPacket(sessionId, msg);
}

bool CGameServer::ValidateMove(CZone* zone, CPlayer* player, float clientX, float clientY)
{
    // A. NaN/INF 검증 — IEEE 754에서 NaN은 모든 비교가 false이므로 경계 검사를 우회함
    if (!std::isfinite(clientX) || !std::isfinite(clientY))
    {
        ++player->_cheatCount;
        SendSyncPosition(player->_sessionId, player);

        if (player->_cheatCount >= CHEAT_KICK_THRESHOLD)
            _network->RequestDisconnectSession(player->_sessionId);

        return false;
    }

    // B. 맵 경계 검증 (텔레포트핵)
    int32_t mapW = zone->GetMapWidth();
    int32_t mapH = zone->GetMapHeight();
    if (clientX < 0.0f || clientX >= mapW || clientY < 0.0f || clientY >= mapH)
    {
        ++player->_cheatCount;
        SendSyncPosition(player->_sessionId, player);

        if (player->_cheatCount >= CHEAT_KICK_THRESHOLD)
            _network->RequestDisconnectSession(player->_sessionId);

        return false;
    }

    // B. 이동 거리 검증 (스피드핵)
    float dx = clientX - player->_x;
    float dy = clientY - player->_y;
    float distSq = dx * dx + dy * dy;

    if (distSq > MOVE_TOLERANCE_SQ)
    {
        ++player->_cheatCount;
        SendSyncPosition(player->_sessionId, player);

        if (player->_cheatCount >= CHEAT_KICK_THRESHOLD)
            _network->RequestDisconnectSession(player->_sessionId);

        return false;
    }

    return true;
}

// ==========================================================================
// 섹터 변경 브로드캐스트
// ==========================================================================

void CGameServer::ProcessSectorChange(CZone* zone, CPlayer* player,
                                      int32_t oldSectorX, int32_t oldSectorY)
{
    std::vector<CSectorManager::SectorPos> added;
    std::vector<CSectorManager::SectorPos> removed;
    zone->GetSectorManager().GetSectorDiff(
        oldSectorX, oldSectorY,
        player->_sectorX, player->_sectorY, added, removed);

    // 이탈 섹터 — 상호 DELETE
    for (const auto& pos : removed)
    {
        const auto& players = zone->GetSectorManager().GetSectorPlayers(pos.x, pos.y);
        for (CPlayer* other : players)
        {
            SendDeletePlayer(other->_sessionId, player);  // 상대에게 나를 삭제
            SendDeletePlayer(player->_sessionId, other);   // 나에게 상대를 삭제
        }
    }

    // 진입 섹터 — 상호 CREATE
    for (const auto& pos : added)
    {
        const auto& players = zone->GetSectorManager().GetSectorPlayers(pos.x, pos.y);
        for (CPlayer* other : players)
        {
            if (other == player)
                continue;  // 멀티섹터 점프 시 자기 자신 방지

            SendCreateOtherPlayer(other->_sessionId, player);  // 상대에게 나를 생성
            SendCreateOtherPlayer(player->_sessionId, other);   // 나에게 상대를 생성
        }
    }
}
