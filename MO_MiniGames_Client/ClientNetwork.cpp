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

void CClientNetwork::RecvThread()
{
    char buffer[4096];

    while (_running && _connected)
    {
        int bytesReceived = recv(_socket, buffer, sizeof(buffer), 0);

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

        HandleServerMessage(buffer, bytesReceived);
    }
}

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
        return false;
    }

    return true;
}

void CClientNetwork::RequestRoomList()
{
    MsgHeader header;
    header.size = sizeof(MsgHeader);
    header.type = MsgType::C2S_REQUEST_ROOM_LIST;

    SendPacket(reinterpret_cast<const char*>(&header), sizeof(header));
    std::wcout << L"Requesting room list..." << std::endl;
}

void CClientNetwork::RequestCreateRoom(const std::string& title, int32_t maxPlayers)
{
    MSG_C2S_CREATE_ROOM msg;
    msg.header.size = sizeof(MSG_C2S_CREATE_ROOM);
    msg.header.type = MsgType::C2S_CREATE_ROOM;
    strncpy_s(msg.title, title.c_str(), sizeof(msg.title) - 1);
    msg.title[sizeof(msg.title) - 1] = '\0';
    msg.maxPlayers = maxPlayers;

    SendPacket(reinterpret_cast<const char*>(&msg), sizeof(msg));
    std::wcout << L"Requesting to create room: " << title.c_str() << L" (Max: " << maxPlayers << L")" << std::endl;
}

void CClientNetwork::RequestJoinRoom(int32_t roomId)
{
    MSG_C2S_JOIN_ROOM msg;
    msg.header.size = sizeof(MSG_C2S_JOIN_ROOM);
    msg.header.type = MsgType::C2S_JOIN_ROOM;
    msg.roomId = roomId;

    SendPacket(reinterpret_cast<const char*>(&msg), sizeof(msg));
    std::wcout << L"Requesting to join room: " << roomId << std::endl;
}

void CClientNetwork::RequestLeaveRoom()
{
    MSG_C2S_LEAVE_ROOM msg;
    msg.header.size = sizeof(MSG_C2S_LEAVE_ROOM);
    msg.header.type = MsgType::C2S_LEAVE_ROOM;

    SendPacket(reinterpret_cast<const char*>(&msg), sizeof(msg));
    std::wcout << L"Requesting to leave room..." << std::endl;
}

void CClientNetwork::HandleServerMessage(const char* data, size_t length)
{
    if (!_gameInstance)
    {
        return;
    }

    if (length < sizeof(MsgHeader))
    {
        WLOG_ERROR_STREAM(L"Invalid message size");
        return;
    }

    const MsgHeader* header = reinterpret_cast<const MsgHeader*>(data);

    if (header->size > length)
    {
        WLOG_ERROR_STREAM(L"Message size mismatch");
        return;
    }

    switch (header->type)
    {
    case MsgType::S2C_ROOM_LIST:
        if (length >= sizeof(MSG_S2C_ROOM_LIST))
        {
            _gameInstance->OnRoomListReceived(reinterpret_cast<const MSG_S2C_ROOM_LIST*>(data), length);
        }
        break;

    case MsgType::S2C_ROOM_CREATED:
        if (length >= sizeof(MSG_S2C_ROOM_CREATED))
        {
            _gameInstance->OnRoomCreated(reinterpret_cast<const MSG_S2C_ROOM_CREATED*>(data));
        }
        break;

    case MsgType::S2C_ROOM_JOINED:
        if (length >= sizeof(MSG_S2C_ROOM_JOINED))
        {
            _gameInstance->OnRoomJoined(reinterpret_cast<const MSG_S2C_ROOM_JOINED*>(data));
        }
        break;

    case MsgType::S2C_ROOM_LEFT:
        if (length >= sizeof(MSG_S2C_ROOM_LEFT))
        {
            _gameInstance->OnRoomLeft(reinterpret_cast<const MSG_S2C_ROOM_LEFT*>(data));
        }
        break;

    case MsgType::S2C_ERROR:
        if (length >= sizeof(MSG_S2C_ERROR))
        {
            _gameInstance->OnError(reinterpret_cast<const MSG_S2C_ERROR*>(data));
        }
        break;

    default:
        WLOG_ERROR_STREAM(L"Unknown message type: " << static_cast<int>(header->type));
        break;
    }
}