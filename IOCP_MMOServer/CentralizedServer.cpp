//
#include "CentralizedServer.h"
#include "RoomManager.h"
#include "../Shared/Common/ErrorLog.h"
#include <iostream>
#include <cstring>

CCentralizedServer::CCentralizedServer(int port, int maxClients, int mainlogicTickMs)
    : _networkServer(std::make_shared<CIOCPServer>(port, maxClients, ServerArchitectureType::Centralized))
    , _roomManager(std::make_shared<CRoomManager>())
    , _running(false)
    , _mainlogicTickMs(mainlogicTickMs)
{
}

CCentralizedServer::~CCentralizedServer()
{
    Stop();
    _networkServer->ShutdownServer();
}

bool CCentralizedServer::Start()
{
    _running = true;
    if (!_networkServer->Start())
        return false;

    _gameThread = std::thread(&CCentralizedServer::GameLogicThread, this);
    std::cout << "[CentralizedServer] Game logic thread started" << std::endl;
    return true;
}

void CCentralizedServer::Stop()
{
    if (!_running)
    {
        return;
    }

    _running = false;

    if (_gameThread.joinable())
    {
        _gameThread.join();
    }

    std::cout << "[CentralizedServer] Game logic thread stopped" << std::endl;
}

void CCentralizedServer::GameLogicThread()
{
    while (_running)
    {
        // 네트워크 이벤트 처리
        NetworkEvent event(NetworkEvent::Type::CONNECTED, -1);
        while (_networkServer->PopNetworkEvent(event))
        {
            switch (event.type)
            {
            case NetworkEvent::Type::CONNECTED:
                DispatchClientConnected(event.sessionId);
                break;
            case NetworkEvent::Type::DISCONNECTED:
                DispatchClientDisconnected(event.sessionId);
                break;
            case NetworkEvent::Type::RECEIVED:
                DispatchDataReceived(event.sessionId, event.data.data(), event.data.size());
                break;
            }
        }

        // 게임 로직 처리
        ProcessGameLogic();

        // CPU 부하 방지
        if (_mainlogicTickMs >= 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(_mainlogicTickMs));
        }
    }
}

void CCentralizedServer::DispatchClientConnected(int64_t sessionId)
{
    // 게임 컨텐츠 레이어의 플레이어 객체 생성
    auto player = std::make_shared<CPlayer>(sessionId);
    AddPlayer(player);

    // 클라이언트가 접속하면 즉시 방 목록 전송
    SendRoomList(player);
}

void CCentralizedServer::DispatchClientDisconnected(int64_t sessionId)
{
    // 플레이어 객체 조회
    auto player = GetPlayer(sessionId);
    if (!player)
    {
        LOG_ERROR_STREAM("[CentralizedServer] Player not found for SessionId: " << sessionId);
        return;
    }

    // 플레이어가 속한 방에서 퇴장 처리 (CPlayer 기반)
    _roomManager->LeaveRoom(player);

    // <SessionId, _players> 맵에서 플레이어 제거
    RemovePlayer(sessionId);
}

void CCentralizedServer::DispatchDataReceived(int64_t sessionId, const char* data, size_t length)
{
    // 플레이어 객체 조회
    auto player = GetPlayer(sessionId);
    if (!player)
    {
        LOG_ERROR_STREAM("[CentralizedServer] Player not found for SessionId: " << sessionId);
        return;
    }

    if (length < sizeof(MsgHeader))
    {
        LOG_ERROR_STREAM("[CentralizedServer] Invalid msg size from SessionId: " << sessionId);
        return;
    }

    const MsgHeader* header = reinterpret_cast<const MsgHeader*>(data);

    // 패킷 크기 검증
    if (header->size != length)
    {
        LOG_ERROR_STREAM("[CentralizedServer] Msg size mismatch from SessionId: " << sessionId);
        return;
    }

    // 패킷 타입별 처리 (플레이어 기반)
    switch (header->type)
    {
    case MsgType::C2S_REQUEST_ROOM_LIST:
        if (length >= sizeof(MsgHeader))
        {
            HandleRequestRoomList(player);
        }
        break;

    case MsgType::C2S_CREATE_ROOM:
        if (length >= sizeof(MSG_C2S_CREATE_ROOM))
        {
            HandleCreateRoom(player, reinterpret_cast<const MSG_C2S_CREATE_ROOM*>(data));
        }
        break;

    case MsgType::C2S_JOIN_ROOM:
        if (length >= sizeof(MSG_C2S_JOIN_ROOM))
        {
            HandleJoinRoom(player, reinterpret_cast<const MSG_C2S_JOIN_ROOM*>(data));
        }
        break;

    case MsgType::C2S_LEAVE_ROOM:
        HandleLeaveRoom(player);
        break;

    default:
        LOG_ERROR_STREAM("[CentralizedServer] Unknown msg type: " << static_cast<int>(header->type));
        break;
    }
}

void CCentralizedServer::HandleRequestRoomList(std::shared_ptr<CPlayer> player)
{
    SendRoomList(player);
}

void CCentralizedServer::HandleCreateRoom(std::shared_ptr<CPlayer> player, const MSG_C2S_CREATE_ROOM* msg)
{
    std::string title(msg->title);
    int32_t maxPlayers = msg->maxPlayers;

    // 유효성 검증
    if (title.empty() || maxPlayers < 2 || maxPlayers > 10)
    {
        SendRoomCreated(player, -1, false);
        SendError(player, "Invalid room parameters");
        return;
    }

    // 방 이름 중복 체크
    // 차후 스트링을 던질게 아니라 메시지로 빼는 방식으로 변경 하면 좋을 듯
    if (_roomManager->FindRoomByTitle(title))
    {
        SendRoomCreated(player, -1, false);
        SendError(player, "Room title already exists");
        return;
    }

    auto room = _roomManager->CreateRoom(title, maxPlayers);
    if (room)
    {
        // 방 생성 성공 시, 즉시 입장 처리 (CPlayer 기반)
        bool joinSuccess = _roomManager->JoinRoom(room->GetRoomId(), player);
        SendRoomCreated(player, room->GetRoomId(), joinSuccess);

        if (!joinSuccess)
        {
            SendError(player, "Failed to join created room");
        }
    }
    else // 방 생성 실패  
    {
        SendRoomCreated(player, -1, false);
    }
}

void CCentralizedServer::HandleJoinRoom(std::shared_ptr<CPlayer> player, const MSG_C2S_JOIN_ROOM* msg)
{
    int32_t roomId = msg->roomId;

    // CPlayer 기반으로 RoomManager에 전달
    bool success = _roomManager->JoinRoom(roomId, player);
    SendRoomJoined(player, roomId, success);

    if (!success)
    {
        SendError(player, "Failed to join room");
    }
}

void CCentralizedServer::HandleLeaveRoom(std::shared_ptr<CPlayer> player)
{
    // CPlayer 기반으로 RoomManager에 전달
    bool success = _roomManager->LeaveRoom(player);
    SendRoomLeft(player, success);
}

void CCentralizedServer::SendRoomList(std::shared_ptr<CPlayer> player)
{
    auto roomList = _roomManager->GetRoomList();
    int32_t roomCount = static_cast<int32_t>(roomList.size());

    // 가변 길이 패킷 생성
    size_t msgSize = sizeof(MSG_S2C_ROOM_LIST) + sizeof(RoomInfo) * roomCount;
    std::vector<char> buffer(msgSize);

    MSG_S2C_ROOM_LIST* msg = reinterpret_cast<MSG_S2C_ROOM_LIST*>(buffer.data());
    msg->header.size = static_cast<uint16_t>(msgSize);
    msg->header.type = MsgType::S2C_ROOM_LIST;
    msg->roomCount = roomCount;

    // 방 정보 채우기
    RoomInfo* roomInfoArray = reinterpret_cast<RoomInfo*>(buffer.data() + sizeof(MSG_S2C_ROOM_LIST));
    int index = 0;
    for (const auto& room : roomList)
    {
        RoomInfo& info = roomInfoArray[index++];
        info.roomId = room->GetRoomId();
        strncpy_s(info.title, room->GetTitle().c_str(), sizeof(info.title) - 1);
        info.title[sizeof(info.title) - 1] = '\0';
        info.currentPlayers = room->GetCurrentPlayerCount();
        info.maxPlayers = room->GetMaxPlayers();
        info.status = static_cast<uint8_t>(room->GetStatus());
    }

    _networkServer->RequestSendMsg(player->GetSessionId(), buffer.data(), static_cast<int>(msgSize));
}

void CCentralizedServer::SendRoomCreated(std::shared_ptr<CPlayer> player, int32_t roomId, bool success)
{
    MSG_S2C_ROOM_CREATED msg;
    msg.header.size = sizeof(MSG_S2C_ROOM_CREATED);
    msg.header.type = MsgType::S2C_ROOM_CREATED;
    msg.roomId = roomId;
    msg.success = success ? 1 : 0;

    _networkServer->RequestSendMsg(player->GetSessionId(), reinterpret_cast<const char*>(&msg), sizeof(msg));
}

void CCentralizedServer::SendRoomJoined(std::shared_ptr<CPlayer> player, int32_t roomId, bool success)
{
    MSG_S2C_ROOM_JOINED msg;
    msg.header.size = sizeof(MSG_S2C_ROOM_JOINED);
    msg.header.type = MsgType::S2C_ROOM_JOINED;
    msg.roomId = roomId;
    msg.success = success ? 1 : 0;

    _networkServer->RequestSendMsg(player->GetSessionId(), reinterpret_cast<const char*>(&msg), sizeof(msg));
}

void CCentralizedServer::SendRoomLeft(std::shared_ptr<CPlayer> player, bool success)
{
    MSG_S2C_ROOM_LEFT msg;
    msg.header.size = sizeof(MSG_S2C_ROOM_LEFT);
    msg.header.type = MsgType::S2C_ROOM_LEFT;
    msg.success = success ? 1 : 0;

    _networkServer->RequestSendMsg(player->GetSessionId(), reinterpret_cast<const char*>(&msg), sizeof(msg));
}

void CCentralizedServer::SendError(std::shared_ptr<CPlayer> player, const std::string& message)
{
    MSG_S2C_ERROR msg;
    msg.header.size = sizeof(MSG_S2C_ERROR);
    msg.header.type = MsgType::S2C_ERROR;
    strncpy_s(msg.message, message.c_str(), sizeof(msg.message) - 1);
    msg.message[sizeof(msg.message) - 1] = '\0';

    _networkServer->RequestSendMsg(player->GetSessionId(), reinterpret_cast<const char*>(&msg), sizeof(msg));
}

void CCentralizedServer::ProcessGameLogic()
{
    // 주기적인 게임 로직 처리
    // 예: 게임 타이머, 상태 업데이트 등
}

std::shared_ptr<CPlayer> CCentralizedServer::GetPlayer(int64_t sessionId)
{
    auto it = _players.find(sessionId);
    return (it != _players.end()) ? it->second : nullptr;
}

void CCentralizedServer::AddPlayer(std::shared_ptr<CPlayer> player)
{
    _players[player->GetSessionId()] = player;
}

void CCentralizedServer::RemovePlayer(int64_t sessionId)
{
    _players.erase(sessionId);
}
