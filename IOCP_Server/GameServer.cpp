#include "GameServer.h"
#include "Player.h"
#include "Protocol.h"
#include "SerialBuffer.h"
#include "../Shared/Common/ErrorLog.h"
#include <iostream>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <cstddef>  // offsetof

CGameServer::CGameServer(CMonitorManager& monitor)
    : _mode(ServerMode::GameServer)
    , _monitor(monitor)
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

    _network = std::make_unique<CIOCPServer>(port, maxClients, _mode, _monitor);

    // 게임 서버 모드일 때만 맵 등록
    if (_mode == ServerMode::GameServer)
    {
        if (maps == nullptr || mapCount <= 0)
            return false;

        for (int32_t i = 0; i < mapCount; ++i)
        {
            if (!_mapManager.RegisterMap(maps[i]))
                return false;
        }

        // 첫 번째 맵을 기본 접속 맵으로 설정
        _defaultMapId = maps[0].mapId;

        // 프레임 재사용 컨테이너 reserve (워밍업 realloc 방지)
        // 전체 서버 범위 버퍼도 maxPerChannel 기준 — 초과 시 자연 증가 후 capacity 유지
        int32_t maxPerChannel = maps[0].maxPlayersPerChannel;
        _sessionToPlayer.reserve(maxClients);
        _broadcastBuffer.reserve(maxPerChannel);       // 주변 9섹터 단위
        _eventAroundBuffer.reserve(maxPerChannel);     // 주변 9섹터 단위
        _pendingSectorChanges.reserve(maxPerChannel);  // 전체 서버 프레임 이벤트
        _tickSectorChanges.reserve(maxPerChannel);     // 전체 서버 TickAll 결과
        _tickClampedPlayers.reserve(maxPerChannel);    // 전체 서버 TickAll 결과
        _sectorChangedSet.reserve(maxPerChannel);      // 전체 서버 dedup
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

    const char* modeName = "Unknown";
    switch (_mode)
    {
    case ServerMode::GameCodiEchoTest:    modeName = "GameCodiEchoTest";    break;
    case ServerMode::NetWorkLib_EchoTest: modeName = "NetWorkLib_EchoTest"; break;
    case ServerMode::GameServer:          modeName = "GameServer";          break;
    }
    SLOG_INFO("[GameServer] Started - Mode: {}", modeName);

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

    // 잔여 플레이어 정리 (CGameServer가 생명주기 소유)
    for (auto& pair : _sessionToPlayer)
    {
        delete pair.second;
    }
    _sessionToPlayer.clear();
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
        _tickSectorChanges.clear();
        _tickClampedPlayers.clear();
        _mapManager.TickAll(deltaTime, _tickSectorChanges, _tickClampedPlayers);

        // 3) 섹터 변경 배치 처리 (RecvMoveStop + TickAll 병합 → 삽입 시 중복 차단)
        for (const auto& change : _tickSectorChanges)
        {
            PushSectorChange(change.player, change.oldSectorX, change.oldSectorY);
        }

        for (const auto& change : _pendingSectorChanges)
        {
            // 출발 섹터 == 현재 섹터이면 원위치 복귀 → 브로드캐스트 불필요
            if (change.oldSectorX == change.player->_sectorX &&
                change.oldSectorY == change.player->_sectorY)
                continue;

            CZone* zone = _mapManager.GetZone(change.player->_zoneId);
            if (zone != nullptr)
            {
                ProcessSectorChange(zone, change.player,
                                    change.oldSectorX, change.oldSectorY);
            }
        }
        _pendingSectorChanges.clear();
        _sectorChangedSet.clear();

        // 3-1) 맵 경계 클램핑으로 정지된 플레이어에게 MOVE_STOP 브로드캐스트
        for (CPlayer* player : _tickClampedPlayers)
        {
            CZone* zone = _mapManager.GetZone(player->_zoneId);
            if (zone != nullptr)
            {
                MSG_S2C_MOVE_STOP msg;
                msg.playerId = player->_playerId;
                msg.direction = static_cast<uint8_t>(player->_direction);
                msg.x = player->_x;
                msg.y = player->_y;
                BroadcastAroundSector(zone, player, msg, false);  // 본인 포함
            }
        }

        // 4) 주기적 위치 동기화 (MOVING 플레이어 → 주변 브로드캐스트)
        ++_syncFrameCount;
        if (_syncFrameCount >= SYNC_INTERVAL_FRAMES)
        {
            _syncFrameCount = 0;
            _mapManager.ForEachZone([&](CZone* zone)
            {
                for (CPlayer* player : zone->GetPlayers())
                {
                    if (player->_moveState != MoveState::MOVING)
                        continue;

                    // 델타 동기화: 마지막 동기화 이후 충분히 이동한 경우만 전송
                    float dx = player->_x - player->_lastSyncX;
                    float dy = player->_y - player->_lastSyncY;
                    if (dx * dx + dy * dy < SYNC_DISTANCE_THRESHOLD_SQ)
                        continue;

                    player->_lastSyncX = player->_x;
                    player->_lastSyncY = player->_y;

                    MSG_S2C_SYNC_POSITION msg;
                    msg.playerId = player->_playerId;
                    msg.x = player->_x;
                    msg.y = player->_y;
                    BroadcastAroundSector(zone, player, msg, false);  // 본인 포함 (이동 중 클라-서버 좌표 드리프트 보정)
                }
            });
        }

        // 5) 빈 동적 채널 정리
        ++_cleanupFrameCount;
        if (_cleanupFrameCount >= CLEANUP_INTERVAL_FRAMES)
        {
            _cleanupFrameCount = 0;
            _mapManager.CleanupEmptyChannels();
        }

        // 6) Tick 시간 기록 + 프레임 제한
        auto frameEnd = Clock::now();
        double tickMs = std::chrono::duration<double, std::milli>(frameEnd - frameStart).count();
        _monitor._gameLoop.RecordTickTime(tickMs);

        int sleepMs = FRAME_INTERVAL_MS - static_cast<int>(tickMs);
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
    if (_sessionToPlayer.find(sessionId) != _sessionToPlayer.end())
    {
        _network->RequestDisconnectSession(sessionId);
        return;
    }

    // 기본 맵의 여유 채널에 입장
    CZone* zone = _mapManager.FindOrCreateChannel(_defaultMapId);
    if (zone == nullptr)
    {
        _network->RequestDisconnectSession(sessionId);
        return;
    }

    // 플레이어 생성 + ID/표시 속성 부여 (CGameServer가 생명주기 소유)
    CPlayer* player = new CPlayer();
    player->_playerId = AllocPlayerId();
    player->_displayChar = CalcDisplayChar(player->_playerId);
    player->_colorIndex = CalcColorIndex(player->_playerId);
    if (!zone->EnterZone(player))
    {
        delete player;
        _network->RequestDisconnectSession(sessionId);
        return;
    }

    // 경계 매핑 등록
    player->_sessionId = sessionId;
    _sessionToPlayer[sessionId] = player;

    // 델타 동기화 기준 좌표 초기화 (스폰 직후 불필요한 동기화 방지)
    player->_lastSyncX = player->_x;
    player->_lastSyncY = player->_y;

    // 1) 존 메타 정보 전송
    SendZoneInfo(player, zone);

    // 2) 본인에게 내 캐릭터 생성
    SendCreateMyPlayer(player);

    // 3) 주변 상호 CREATE 브로드캐스트
    BroadcastEnterZone(zone, player, SpawnReason::CONNECT);
}

void CGameServer::OnDisconnected(int64_t sessionId)
{
    auto it = _sessionToPlayer.find(sessionId);
    if (it == _sessionToPlayer.end())
        return;

    CPlayer* player = it->second;
    CZone* zone = _mapManager.GetZone(player->_zoneId);
    if (zone != nullptr)
    {
        // 주변 DELETE 브로드캐스트 + 섹터/존 해제
        BroadcastLeaveZone(zone, player);
    }

    // 경계 매핑 해제 (zone 유무와 무관하게 반드시 수행)
    _sessionToPlayer.erase(it);

    // 플레이어 삭제 (CGameServer가 생명주기 소유)
    delete player;
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
        InterlockedIncrement64(&_monitor._packetErrors);
        CSerialBuffer::Free(pMsg);
        return;
    }

    // 경계 계층: sessionId → CPlayer* 변환 (이후 컨텐츠 로직은 CPlayer*만 사용)
    auto it = _sessionToPlayer.find(sessionId);
    if (it == _sessionToPlayer.end())
    {
        CSerialBuffer::Free(pMsg);
        return;
    }
    CPlayer* player = it->second;

    switch (header.type)
    {
    case MsgType::C2S_MOVE_START:
        RecvMoveStart(player, pMsg);
        break;

    case MsgType::C2S_MOVE_STOP:
        RecvMoveStop(player, pMsg);
        break;

    case MsgType::C2S_CHAT:
        RecvChat(player, pMsg);
        break;

    case MsgType::C2S_ZONE_CHANGE:
        RecvZoneChange(player, pMsg);
        break;

    case MsgType::C2S_ADMIN_LOGIN:
        RecvAdminLogin(player, pMsg);
        break;

    default:
        // C2S_HEARTBEAT 등 — 게임 로직 처리 불필요 (타임아웃 갱신은 IOCPServer 수신 시점에서 처리)
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
    case MsgType::C2S_CHAT:         return sizeof(MsgHeader) + sizeof(wchar_t); // 가변 길이: 최소 1글자
    case MsgType::C2S_ZONE_CHANGE:  return sizeof(MSG_C2S_ZONE_CHANGE);
    case MsgType::C2S_HEARTBEAT:    return sizeof(MSG_C2S_HEARTBEAT);
    case MsgType::C2S_ADMIN_LOGIN:  return sizeof(MSG_C2S_ADMIN_LOGIN);
    default:                        return 0;
    }
}

// ==========================================================================
// 패킷 핸들러
// ==========================================================================

void CGameServer::RecvMoveStart(CPlayer* player, CSerialBuffer* pMsg)
{
    CZone* zone = _mapManager.GetZone(player->_zoneId);
    if (zone == nullptr)
        return;

    MSG_C2S_MOVE_START recvMsg;
    pMsg->GetData(reinterpret_cast<char*>(&recvMsg), sizeof(recvMsg));

    // Direction 범위 검증 (4방향)
    if (recvMsg.direction < static_cast<uint8_t>(Direction::UP) ||
        recvMsg.direction > static_cast<uint8_t>(Direction::RIGHT))
        return;

    Direction dir = static_cast<Direction>(recvMsg.direction);

    // 이동 중 방향 전환: 같은 방향이면 무시, 다른 방향이면 갱신 + 재브로드캐스트
    // ※ 좌표 수용보다 먼저 검사 — MOVING 중 반복 전송으로 서버 좌표를 밀어내는 치트 방지
    if (player->_moveState == MoveState::MOVING)
    {
        if (dir == player->_direction)
            return;

        // 벽 방향 검증: 벽 위치에서 벽 쪽으로 방향 전환 시 정지 처리
        if (IsBlockedByWall(zone, player, dir))
        {
            player->_moveState = MoveState::IDLE;
            player->_direction = dir;

            MSG_S2C_MOVE_STOP msg;
            msg.playerId = player->_playerId;
            msg.direction = static_cast<uint8_t>(dir);
            msg.x = player->_x;
            msg.y = player->_y;
            BroadcastAroundSector(zone, player, msg, false);  // 본인 포함
            return;
        }

        player->_direction = dir;
        player->_lastSyncX = player->_x;
        player->_lastSyncY = player->_y;

        MSG_S2C_MOVE_START msg;
        msg.playerId = player->_playerId;
        msg.direction = static_cast<uint8_t>(dir);
        msg.x = player->_x;
        msg.y = player->_y;
        BroadcastAroundSector(zone, player, msg);
        return;
    }

    // 클라이언트 예측 좌표 수용 (IDLE → MOVING 전환 시에만):
    // 서버 좌표와의 오차가 허용 범위 내이면 채택
    {
        float dx = recvMsg.x - player->_x;
        float dy = recvMsg.y - player->_y;
        if (dx * dx + dy * dy <= MOVE_START_ACCEPT_DIST_SQ)
        {
            // 맵 경계 클램핑 후 수용
            float acceptX = recvMsg.x;
            float acceptY = recvMsg.y;
            float mapW = static_cast<float>(zone->GetMapWidth());
            float mapH = static_cast<float>(zone->GetMapHeight());
            if (acceptX < 0.0f)    acceptX = 0.0f;
            if (acceptX >= mapW)   acceptX = mapW - 1.0f;
            if (acceptY < 0.0f)    acceptY = 0.0f;
            if (acceptY >= mapH)   acceptY = mapH - 1.0f;
            player->_x = acceptX;
            player->_y = acceptY;

            // 좌표 수용으로 섹터가 변경되었을 수 있으므로 재계산
            int32_t newSectorX = zone->GetSectorManager().CalcSectorX(player->_x);
            int32_t newSectorY = zone->GetSectorManager().CalcSectorY(player->_y);
            if (newSectorX != player->_sectorX || newSectorY != player->_sectorY)
            {
                int32_t oldSectorX = player->_sectorX;
                int32_t oldSectorY = player->_sectorY;

                zone->GetSectorManager().RemovePlayer(player, oldSectorX, oldSectorY);
                player->_sectorX = newSectorX;
                player->_sectorY = newSectorY;
                zone->GetSectorManager().AddPlayer(player, newSectorX, newSectorY);

                PushSectorChange(player, oldSectorX, oldSectorY);
            }
        }
        // 범위 초과 시 서버 좌표 유지 (치트 방지)
    }

    // 벽 방향 검증: 벽 위치에서 벽 쪽으로 이동 시도 시 차단
    if (IsBlockedByWall(zone, player, dir))
    {
        SendSyncPosition(player);
        return;
    }

    // 플레이어 상태 갱신
    player->_direction = dir;
    player->_moveState = MoveState::MOVING;

    // 델타 동기화 기준 좌표 갱신 (이동 시작 직후 즉시 동기화 방지)
    player->_lastSyncX = player->_x;
    player->_lastSyncY = player->_y;

    // 주변에 MOVE_START 브로드캐스트
    MSG_S2C_MOVE_START msg;
    msg.playerId = player->_playerId;
    msg.direction = static_cast<uint8_t>(dir);
    msg.x = player->_x;
    msg.y = player->_y;
    BroadcastAroundSector(zone, player, msg);
}

void CGameServer::RecvMoveStop(CPlayer* player, CSerialBuffer* pMsg)
{
    CZone* zone = _mapManager.GetZone(player->_zoneId);
    if (zone == nullptr)
        return;

    MSG_C2S_MOVE_STOP recvMsg;
    pMsg->GetData(reinterpret_cast<char*>(&recvMsg), sizeof(recvMsg));

    // 이동 중이 아니면 무시 (서버 벽 클램핑으로 이미 IDLE 처리됨)
    if (player->_moveState != MoveState::MOVING)
        return;

    // Direction 범위 검증 (4방향)
    if (recvMsg.direction < static_cast<uint8_t>(Direction::UP) ||
        recvMsg.direction > static_cast<uint8_t>(Direction::RIGHT))
        return;

    // 서버 권위 모델: 서버 좌표 유지, 상태만 변경
    player->_direction = static_cast<Direction>(recvMsg.direction);
    player->_moveState = MoveState::IDLE;

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

        // 시야 진입/이탈 — 대기열에 추가 (틱 끝에 배치 처리)
        PushSectorChange(player, oldSectorX, oldSectorY);
    }

    // 주변에 MOVE_STOP 브로드캐스트
    MSG_S2C_MOVE_STOP msg;
    msg.playerId = player->_playerId;
    msg.direction = static_cast<uint8_t>(player->_direction);
    msg.x = player->_x;
    msg.y = player->_y;
    BroadcastAroundSector(zone, player, msg);
}

void CGameServer::RecvChat(CPlayer* player, CSerialBuffer* pMsg)
{
    CZone* zone = _mapManager.GetZone(player->_zoneId);
    if (zone == nullptr)
        return;

    // 가변 길이 수신: header.size 기반으로 실제 메시지 길이 역산
    MsgHeader header;
    pMsg->PeekData(reinterpret_cast<char*>(&header), sizeof(header));

    uint16_t recvSize = header.size;
    if (recvSize > sizeof(MSG_C2S_CHAT))
        recvSize = sizeof(MSG_C2S_CHAT);  // 최대 크기 제한

    MSG_C2S_CHAT recvMsg{};
    pMsg->GetData(reinterpret_cast<char*>(&recvMsg), recvSize);

    // 메시지 길이 계산 (바이트 → wchar_t 글자 수)
    uint16_t msgBytes = recvSize - sizeof(MsgHeader);
    msgBytes -= msgBytes % sizeof(wchar_t);  // wchar_t 경계 정렬 (홀수 바이트 방어)
    uint16_t msgLen = msgBytes / sizeof(wchar_t);
    if (msgLen > CHAT_MSG_MAX_LEN - 1)
        msgLen = CHAT_MSG_MAX_LEN - 1;
    recvMsg.message[msgLen] = L'\0';  // null 종단 보장

    // 가변 길이 S2C 패킷 조립
    MSG_S2C_CHAT msg;
    msg.playerId = player->_playerId;
    msg.displayChar = player->_displayChar;
    msg.colorIndex = player->_colorIndex;
    memcpy(msg.message, recvMsg.message, (msgLen + 1) * sizeof(wchar_t));

    // 실제 전송 크기 설정 (가변)
    uint16_t sendSize = static_cast<uint16_t>(
        offsetof(MSG_S2C_CHAT, message) + (msgLen + 1) * sizeof(wchar_t));
    msg.header.size = sendSize;

    BroadcastAroundSector(zone, player, msg, sendSize, false);
}

// ==========================================================================
// 패킷 전송 추상화
// ==========================================================================

// ==========================================================================
// 계층 경계 헬퍼
// ==========================================================================

int32_t CGameServer::AllocPlayerId()
{
    return _nextPlayerId++;
}

// playerId → 고유 표시 문자 (A-Z, a-z, 0-9 = 62종)
uint8_t CGameServer::CalcDisplayChar(int32_t playerId)
{
    static constexpr char CHARS[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    return static_cast<uint8_t>(CHARS[static_cast<uint32_t>(playerId) % 62]);
}

// playerId → 고유 색상 인덱스 (0-6, 7종)
uint8_t CGameServer::CalcColorIndex(int32_t playerId)
{
    return static_cast<uint8_t>((static_cast<uint32_t>(playerId) / 62) % 7);
}

void CGameServer::SendZoneInfo(CPlayer* target, CZone* zone)
{
    MSG_S2C_ZONE_INFO msg;
    msg.mapId = zone->GetMapId();
    msg.channelIndex = CMapManager::GetChannelIndexFromZoneId(zone->GetZoneId());
    msg.mapWidth = zone->GetMapWidth();
    msg.mapHeight = zone->GetMapHeight();
    msg.sectorSize = zone->GetSectorManager().GetSectorSize();
    SendPacket(target, msg);
}

void CGameServer::SendCreateMyPlayer(CPlayer* target)
{
    MSG_S2C_CREATE_MY_PLAYER msg;
    msg.playerId = target->_playerId;
    msg.direction = static_cast<uint8_t>(target->_direction);
    msg.displayChar = target->_displayChar;
    msg.colorIndex = target->_colorIndex;
    msg.x = target->_x;
    msg.y = target->_y;
    msg.speed = target->_speed;
    SendPacket(target, msg);
}

void CGameServer::SendCreateOtherPlayer(CPlayer* target, CPlayer* player, SpawnReason reason)
{
    MSG_S2C_CREATE_OTHER_PLAYER msg;
    msg.playerId = player->_playerId;
    msg.direction = static_cast<uint8_t>(player->_direction);
    msg.moveState = static_cast<uint8_t>(player->_moveState);
    msg.displayChar = player->_displayChar;
    msg.colorIndex = player->_colorIndex;
    msg.spawnReason = static_cast<uint8_t>(reason);
    msg.x = player->_x;
    msg.y = player->_y;
    msg.speed = player->_speed;
    SendPacket(target, msg);
}

void CGameServer::SendDeletePlayer(CPlayer* target, CPlayer* player)
{
    MSG_S2C_DELETE_PLAYER msg;
    msg.playerId = player->_playerId;
    SendPacket(target, msg);
}

void CGameServer::SendSyncPosition(CPlayer* target)
{
    MSG_S2C_SYNC_POSITION msg;
    msg.playerId = target->_playerId;
    msg.x = target->_x;
    msg.y = target->_y;
    SendPacket(target, msg);
}

void CGameServer::RecvZoneChange(CPlayer* player, CSerialBuffer* pMsg)
{
    int64_t sessionId = player->_sessionId;
    if (sessionId == -1)
        return;

    CZone* oldZone = _mapManager.GetZone(player->_zoneId);
    if (oldZone == nullptr)
        return;

    MSG_C2S_ZONE_CHANGE recvMsg;
    pMsg->GetData(reinterpret_cast<char*>(&recvMsg), sizeof(recvMsg));

    int32_t targetMapId = recvMsg.targetMapId;
    int32_t targetChannelIndex = recvMsg.targetChannelIndex;

    CZone* newZone = nullptr;

    if (targetChannelIndex >= 0)
    {
        // ── 채널 지정 이동 (같은 맵 내) ──
        int32_t currentMapId = CMapManager::GetMapIdFromZoneId(oldZone->GetZoneId());

        newZone = _mapManager.FindChannel(currentMapId, targetChannelIndex);
        if (newZone == nullptr)
        {
            SendZoneChangeFail(player, 0);  // 채널 없음
            return;
        }
        if (newZone->GetZoneId() == oldZone->GetZoneId())
        {
            SendZoneChangeFail(player, 2);  // 이미 해당 채널
            return;
        }

        // 인원 제한 체크 (admin은 스킵)
        if (!player->_isAdmin)
        {
            int32_t maxPlayers = _mapManager.GetMaxPlayersPerChannel(currentMapId);
            if (maxPlayers > 0 && newZone->GetPlayerCount() >= maxPlayers)
            {
                SendZoneChangeFail(player, 1);  // 채널 가득 참
                return;
            }
        }

        targetMapId = currentMapId;
    }
    else
    {
        // ── 기존 맵 이동 (자동 채널 배정) ──

        // 랜덤 맵 이동 요청 처리
        if (targetMapId == -1)
        {
            int32_t currentMapId = CMapManager::GetMapIdFromZoneId(oldZone->GetZoneId());
            targetMapId = _mapManager.GetRandomMapId(currentMapId);
            if (targetMapId == -1)
            {
                SendZoneChangeFail(player, 0);
                return;
            }
        }

        newZone = _mapManager.FindOrCreateChannel(targetMapId, player->_isAdmin);
        if (newZone == nullptr)
        {
            SendZoneChangeFail(player, 0);
            return;
        }
    }

    // ── 현재 존에서 퇴장 ──
    BroadcastLeaveZone(oldZone, player);

    // ── 새 존에 입장 (player 객체 재활용, playerId 유지) ──

    if (!newZone->EnterZone(player))
    {
        // 입장 실패 시 원래 맵의 여유 채널로 복귀 시도
        CZone* fallback = _mapManager.FindOrCreateChannel(CMapManager::GetMapIdFromZoneId(oldZone->GetZoneId()));
        if (fallback != nullptr && fallback->EnterZone(player))
        {
            _sessionToPlayer[sessionId] = player;
            SendZoneChangeFail(player, 1);

            // 델타 동기화 기준 좌표 초기화
            player->_lastSyncX = player->_x;
            player->_lastSyncY = player->_y;

            // 복귀한 존에서 본인 + 주변 상호 통보
            SendZoneInfo(player, fallback);
            SendCreateMyPlayer(player);
            BroadcastEnterZone(fallback, player, SpawnReason::NORMAL);

            return;
        }

        // 복귀도 실패 → 좀비 방지를 위해 연결 해제
        _sessionToPlayer.erase(sessionId);
        delete player;
        _network->RequestDisconnectSession(sessionId);
        return;
    }

    // 경계 매핑 갱신 (player 객체·playerId 동일, zoneId만 변경됨)
    _sessionToPlayer[sessionId] = player;
    InterlockedIncrement64(&_monitor._gameLoop._zoneChangeCount);

    // 델타 동기화 기준 좌표 초기화
    player->_lastSyncX = player->_x;
    player->_lastSyncY = player->_y;

    // 존 메타 정보 + 존 이동 성공 통보
    SendZoneInfo(player, newZone);
    int32_t channelIndex = CMapManager::GetChannelIndexFromZoneId(newZone->GetZoneId());
    SendZoneChangeOk(player, targetMapId, channelIndex);

    // 주변 상호 CREATE 브로드캐스트
    BroadcastEnterZone(newZone, player, SpawnReason::ZONE_TRANSFER);
}

// ── 운영자 인증 ──

static constexpr char ADMIN_KEY[] = "admin1234";  // 운영자 인증 키

void CGameServer::RecvAdminLogin(CPlayer* player, CSerialBuffer* pMsg)
{
    MSG_C2S_ADMIN_LOGIN recvMsg;
    pMsg->GetData(reinterpret_cast<char*>(&recvMsg), sizeof(recvMsg));

    // null-terminate 보장
    recvMsg.key[ADMIN_KEY_MAX_LEN - 1] = '\0';

    if (strcmp(recvMsg.key, ADMIN_KEY) == 0)
    {
        player->_isAdmin = true;

        MSG_S2C_ADMIN_LOGIN_OK msg;
        SendPacket(player, msg);
    }
    else
    {
        MSG_S2C_ADMIN_LOGIN_FAIL msg;
        SendPacket(player, msg);
    }
}

void CGameServer::SendZoneChangeOk(CPlayer* target, int32_t mapId, int32_t channelIndex)
{
    MSG_S2C_ZONE_CHANGE_OK msg;
    msg.mapId = mapId;
    msg.channelIndex = channelIndex;
    msg.playerId = target->_playerId;
    msg.displayChar = target->_displayChar;
    msg.colorIndex = target->_colorIndex;
    msg.direction = static_cast<uint8_t>(target->_direction);
    msg.x = target->_x;
    msg.y = target->_y;
    SendPacket(target, msg);
}

void CGameServer::SendZoneChangeFail(CPlayer* target, uint8_t reason)
{
    MSG_S2C_ZONE_CHANGE_FAIL msg;
    msg.reason = reason;
    SendPacket(target, msg);
}

// ==========================================================================
// 벽 방향 검증 — 경계 위치에서 벽 쪽 이동 차단 (클라이언트 IsBlockedByWall과 동일)
// ==========================================================================

bool CGameServer::IsBlockedByWall(CZone* zone, CPlayer* player, Direction dir)
{
    float maxX = static_cast<float>(zone->GetMapWidth()) - 1.0f;
    float maxY = static_cast<float>(zone->GetMapHeight()) - 1.0f;

    bool atLeft   = (player->_x <= 0.0f);
    bool atRight  = (player->_x >= maxX);
    bool atTop    = (player->_y <= 0.0f);
    bool atBottom = (player->_y >= maxY);

    switch (dir)
    {
    case Direction::LEFT:  return atLeft;
    case Direction::RIGHT: return atRight;
    case Direction::UP:    return atTop;
    case Direction::DOWN:  return atBottom;
    default: return false;
    }
}

// ==========================================================================
// 존 입장/퇴장 브로드캐스트
// ==========================================================================

void CGameServer::BroadcastEnterZone(CZone* zone, CPlayer* player, SpawnReason reason)
{
    _eventAroundBuffer.clear();
    zone->GetSectorManager().GetAroundPlayers(
        player->_sectorX, player->_sectorY, _eventAroundBuffer, player);

    for (CPlayer* other : _eventAroundBuffer)
    {
        SendCreateOtherPlayer(other, player, reason);  // 기존 플레이어에게 신규 플레이어 생성
        SendCreateOtherPlayer(player, other);           // 신규 플레이어에게 기존 플레이어 생성
    }
}

void CGameServer::BroadcastLeaveZone(CZone* zone, CPlayer* player)
{
    // 현재 섹터 기준 주변 DELETE
    _eventAroundBuffer.clear();
    zone->GetSectorManager().GetAroundPlayers(
        player->_sectorX, player->_sectorY, _eventAroundBuffer, player);

    for (CPlayer* other : _eventAroundBuffer)
    {
        SendDeletePlayer(other, player);
    }

    // 미처리 섹터 변경이 있으면 이전 섹터 전용 뷰어에게도 DELETE
    // (같은 프레임에 섹터 변경 + 존 이동/접속해제 시 고스트 방지)
    if (_sectorChangedSet.count(player))
    {
        for (const auto& change : _pendingSectorChanges)
        {
            if (change.player != player)
                continue;

            // 이전 섹터 전용 = old 주변에만 있고 현재 주변에는 없는 섹터
            CSectorManager::SectorPos added[CSectorManager::MAX_AROUND_SECTORS];
            CSectorManager::SectorPos removed[CSectorManager::MAX_AROUND_SECTORS];
            int32_t addedCount = 0;
            int32_t removedCount = 0;

            zone->GetSectorManager().GetSectorDiff(
                player->_sectorX, player->_sectorY,    // 현재 섹터 (이미 갱신된 값)
                change.oldSectorX, change.oldSectorY,   // 이전 섹터
                added, addedCount,                       // old 전용 섹터
                removed, removedCount);

            for (int32_t i = 0; i < addedCount; ++i)
            {
                const auto& players = zone->GetSectorManager().GetSectorPlayers(
                    added[i].x, added[i].y);
                for (CPlayer* other : players)
                {
                    SendDeletePlayer(other, player);
                }
            }
            break;
        }
    }

    // 섹터 변경 대기열에서 제거
    _sectorChangedSet.erase(player);
    _pendingSectorChanges.erase(
        std::remove_if(_pendingSectorChanges.begin(), _pendingSectorChanges.end(),
            [player](const SectorChangeInfo& c) { return c.player == player; }),
        _pendingSectorChanges.end());

    zone->LeaveZone(player);
}

// ==========================================================================
// 섹터 변경 대기열 삽입 — 같은 플레이어는 최초 출발 섹터만 기록
// ==========================================================================

void CGameServer::PushSectorChange(CPlayer* player, int32_t oldSectorX, int32_t oldSectorY)
{
    if (_sectorChangedSet.find(player) != _sectorChangedSet.end())
        return;  // 이미 기록됨 → 최초 출발 섹터 유지

    _pendingSectorChanges.push_back({ player, oldSectorX, oldSectorY });
    _sectorChangedSet.insert(player);
}

// ==========================================================================
// 섹터 변경 브로드캐스트
// ==========================================================================

void CGameServer::ProcessSectorChange(CZone* zone, CPlayer* player,
                                      int32_t oldSectorX, int32_t oldSectorY)
{
    CSectorManager::SectorPos added[CSectorManager::MAX_AROUND_SECTORS];
    CSectorManager::SectorPos removed[CSectorManager::MAX_AROUND_SECTORS];
    int32_t addedCount = 0;
    int32_t removedCount = 0;
    zone->GetSectorManager().GetSectorDiff(
        oldSectorX, oldSectorY,
        player->_sectorX, player->_sectorY,
        added, addedCount, removed, removedCount);

    // 이탈 섹터 — 상호 DELETE
    for (int32_t i = 0; i < removedCount; ++i)
    {
        const auto& players = zone->GetSectorManager().GetSectorPlayers(removed[i].x, removed[i].y);
        for (CPlayer* other : players)
        {
            SendDeletePlayer(other, player);  // 상대에게 나를 삭제
            SendDeletePlayer(player, other);   // 나에게 상대를 삭제
        }
    }

    // 진입 섹터 — 상호 CREATE
    for (int32_t i = 0; i < addedCount; ++i)
    {
        const auto& players = zone->GetSectorManager().GetSectorPlayers(added[i].x, added[i].y);
        for (CPlayer* other : players)
        {
            if (other == player)
                continue;  // 멀티섹터 점프 시 자기 자신 방지

            SendCreateOtherPlayer(other, player);  // 상대에게 나를 생성
            SendCreateOtherPlayer(player, other);   // 나에게 상대를 생성
        }
    }
}
