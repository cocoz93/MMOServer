//
#include "ClientNetwork.h"
#include "GameInstance.h"
#include "../Shared/Common/ErrorLog.h"
#include <cstring>
#include <iostream>

CClientNetwork::CClientNetwork()
    : _socket(INVALID_SOCKET)
    , _connected(false)
    , _running(false)
    , _gameInstance(nullptr)
    , _recvBufferUsed(0)
{
}

CClientNetwork::~CClientNetwork()
{
    Disconnect();
}

bool CClientNetwork::Connect(const std::string& serverIp, int port)
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        WLOG_ERROR_STREAM(L"WSAStartup failed");
        return false;
    }

    _socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (_socket == INVALID_SOCKET)
    {
        const int wsaErr = WSAGetLastError();
        WLOG_WSA_ERROR_STREAM(L"socket creation failed: ", wsaErr);
        WSACleanup();
        return false;
    }

    SOCKADDR_IN serverAddr;
    ZeroMemory(&serverAddr, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    inet_pton(AF_INET, serverIp.c_str(), &serverAddr.sin_addr);

    if (connect(_socket, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
    {
        const int wsaErr = WSAGetLastError();
        WLOG_WSA_ERROR_STREAM(L"connect failed: ", wsaErr);
        closesocket(_socket);
        WSACleanup();
        return false;
    }

    _connected = true;
    _running = true;
    _recvBufferUsed = 0;

    // 수신 스레드 시작
    _recvThread = std::thread(&CClientNetwork::RecvThread, this);

    std::wcout << L"==================================" << std::endl;
    std::wcout << L"Connected to server: " << serverIp.c_str() << L":" << port << std::endl;
    std::wcout << L"==================================" << std::endl;

    return true;
}

void CClientNetwork::Disconnect()
{
    if (!_connected)
    {
        return;
    }

    _running = false;
    _connected = false;

    if (_socket != INVALID_SOCKET)
    {
        closesocket(_socket);
        _socket = INVALID_SOCKET;
    }

    if (_recvThread.joinable())
    {
        _recvThread.join();
    }

    WSACleanup();

    std::wcout << L"\nDisconnected from server." << std::endl;
}

// ==========================================================================
// 수신 스레드 — TCP 스트림 누적 + 패킷 조립
// ==========================================================================

void CClientNetwork::RecvThread()
{
    while (_running && _connected)
    {
        int space = RECV_BUFFER_SIZE - _recvBufferUsed;
        if (space <= 0)
        {
            WLOG_ERROR_STREAM(L"Recv buffer overflow");
            _connected = false;
            _running = false;
            break;
        }

        int bytesReceived = recv(_socket, _recvBuffer + _recvBufferUsed, space, 0);

        if (bytesReceived <= 0)
        {
            if (_running)
            {
                WLOG_ERROR_STREAM(L"\nConnection lost.");
            }
            _connected = false;
            _running = false;
            break;
        }

        _recvBufferUsed += bytesReceived;

        // 완성된 패킷을 반복 추출
        while (_recvBufferUsed >= static_cast<int>(sizeof(MsgHeader)))
        {
            const MsgHeader* header = reinterpret_cast<const MsgHeader*>(_recvBuffer);
            uint16_t packetSize = header->size;

            // 유효성 검사
            if (packetSize < sizeof(MsgHeader) || packetSize > RECV_BUFFER_SIZE)
            {
                WLOG_ERROR_STREAM(L"Invalid packet size: " << packetSize);
                _connected = false;
                _running = false;
                return;
            }

            // 아직 패킷이 덜 도착했으면 대기
            if (_recvBufferUsed < static_cast<int>(packetSize))
                break;

            // 패킷 디스패치
            DispatchPacket(_recvBuffer, packetSize);

            // 처리한 패킷을 버퍼에서 제거
            int remaining = _recvBufferUsed - packetSize;
            if (remaining > 0)
                memmove(_recvBuffer, _recvBuffer + packetSize, remaining);
            _recvBufferUsed = remaining;
        }
    }
}

// ==========================================================================
// 패킷 전송
// ==========================================================================

bool CClientNetwork::SendPacket(const char* data, size_t length)
{
    if (!_connected)
    {
        return false;
    }

    int bytesSent = send(_socket, data, static_cast<int>(length), 0);
    if (bytesSent == SOCKET_ERROR)
    {
        const int wsaErr = WSAGetLastError();
        WLOG_WSA_ERROR_STREAM(L"send failed: ", wsaErr);
        Disconnect();
        return false;
    }

    if (bytesSent < static_cast<int>(length))
    {
        WLOG_ERROR_STREAM(L"Partial send: " << bytesSent << L"/" << length);
        Disconnect();
        return false;
    }

    return true;
}

void CClientNetwork::SendMoveStart(uint8_t direction)
{
    MSG_C2S_MOVE_START msg;
    msg.header.size = sizeof(MSG_C2S_MOVE_START);
    msg.header.type = MsgType::C2S_MOVE_START;
    msg.direction = direction;

    SendPacket(reinterpret_cast<const char*>(&msg), sizeof(msg));
}

void CClientNetwork::SendMoveStop(uint8_t direction, float x, float y)
{
    MSG_C2S_MOVE_STOP msg;
    msg.header.size = sizeof(MSG_C2S_MOVE_STOP);
    msg.header.type = MsgType::C2S_MOVE_STOP;
    msg.direction = direction;
    msg.x = x;
    msg.y = y;

    SendPacket(reinterpret_cast<const char*>(&msg), sizeof(msg));
}

void CClientNetwork::SendChat(const wchar_t* message)
{
    MSG_C2S_CHAT msg;
    msg.header.size = sizeof(MSG_C2S_CHAT);
    msg.header.type = MsgType::C2S_CHAT;
    wcsncpy_s(msg.message, message, CHAT_MSG_MAX_LEN - 1);
    msg.message[CHAT_MSG_MAX_LEN - 1] = L'\0';

    SendPacket(reinterpret_cast<const char*>(&msg), sizeof(msg));
}

void CClientNetwork::SendZoneChange(int32_t targetMapId)
{
    MSG_C2S_ZONE_CHANGE msg;
    msg.header.size = sizeof(MSG_C2S_ZONE_CHANGE);
    msg.header.type = MsgType::C2S_ZONE_CHANGE;
    msg.targetMapId = targetMapId;

    SendPacket(reinterpret_cast<const char*>(&msg), sizeof(msg));
}

// ==========================================================================
// 패킷 디스패치 — S2C 패킷 타입별 GameInstance 콜백 호출
// ==========================================================================

void CClientNetwork::DispatchPacket(const char* data, uint16_t size)
{
    if (!_gameInstance)
        return;

    const MsgHeader* header = reinterpret_cast<const MsgHeader*>(data);

    switch (header->type)
    {
    case MsgType::S2C_CREATE_MY_PLAYER:
        if (size >= sizeof(MSG_S2C_CREATE_MY_PLAYER))
            _gameInstance->OnCreateMyPlayer(reinterpret_cast<const MSG_S2C_CREATE_MY_PLAYER*>(data));
        break;

    case MsgType::S2C_CREATE_OTHER_PLAYER:
        if (size >= sizeof(MSG_S2C_CREATE_OTHER_PLAYER))
            _gameInstance->OnCreateOtherPlayer(reinterpret_cast<const MSG_S2C_CREATE_OTHER_PLAYER*>(data));
        break;

    case MsgType::S2C_DELETE_PLAYER:
        if (size >= sizeof(MSG_S2C_DELETE_PLAYER))
            _gameInstance->OnDeletePlayer(reinterpret_cast<const MSG_S2C_DELETE_PLAYER*>(data));
        break;

    case MsgType::S2C_MOVE_START:
        if (size >= sizeof(MSG_S2C_MOVE_START))
            _gameInstance->OnMoveStart(reinterpret_cast<const MSG_S2C_MOVE_START*>(data));
        break;

    case MsgType::S2C_MOVE_STOP:
        if (size >= sizeof(MSG_S2C_MOVE_STOP))
            _gameInstance->OnMoveStop(reinterpret_cast<const MSG_S2C_MOVE_STOP*>(data));
        break;

    case MsgType::S2C_CHAT:
        if (size >= sizeof(MSG_S2C_CHAT))
            _gameInstance->OnChat(reinterpret_cast<const MSG_S2C_CHAT*>(data));
        break;

    case MsgType::S2C_SYNC_POSITION:
        if (size >= sizeof(MSG_S2C_SYNC_POSITION))
            _gameInstance->OnSyncPosition(reinterpret_cast<const MSG_S2C_SYNC_POSITION*>(data));
        break;

    case MsgType::S2C_ZONE_CHANGE_OK:
        if (size >= sizeof(MSG_S2C_ZONE_CHANGE_OK))
            _gameInstance->OnZoneChangeOk(reinterpret_cast<const MSG_S2C_ZONE_CHANGE_OK*>(data));
        break;

    case MsgType::S2C_ZONE_CHANGE_FAIL:
        if (size >= sizeof(MSG_S2C_ZONE_CHANGE_FAIL))
            _gameInstance->OnZoneChangeFail(reinterpret_cast<const MSG_S2C_ZONE_CHANGE_FAIL*>(data));
        break;

    case MsgType::S2C_ERROR:
        if (size >= sizeof(MSG_S2C_ERROR))
            _gameInstance->OnError(reinterpret_cast<const MSG_S2C_ERROR*>(data));
        break;

    default:
        WLOG_ERROR_STREAM(L"Unknown message type: " << static_cast<int>(header->type));
        break;
    }
}
