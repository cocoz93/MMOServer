#pragma once

#include "Protocol.h"
#include <WinSock2.h>
#include <WS2tcpip.h>
#define NOMINMAX
#include <Windows.h>
#include <string>
#include <thread>
#include <atomic>
#include <functional>

#pragma comment(lib, "ws2_32.lib")

class CGameInstance; // 전방 선언

class CClientNetwork
{
public:
    CClientNetwork();
    ~CClientNetwork();

    bool Connect(const std::string& serverIp, int port);
    void Disconnect();
    bool IsConnected() const { return _connected; }

    // 패킷 전송
    bool SendPacket(const char* data, size_t length);

    // 서버로 요청 전송
    void RequestRoomList();
    void RequestCreateRoom(const std::string& title, int32_t maxPlayers);
    void RequestJoinRoom(int32_t roomId);
    void RequestLeaveRoom();

    // GameInstance 설정 (패킷 핸들러 콜백용)
    void SetGameInstance(CGameInstance* instance) { _gameInstance = instance; }

private:
    // 수신 스레드
    void RecvThread();

    // 서버 응답 처리
    void HandleServerMessage(const char* data, size_t length);

private:
    SOCKET _socket;
    std::atomic<bool> _connected;
    std::atomic<bool> _running;
    std::thread _recvThread;

    CGameInstance* _gameInstance; // 패킷 수신 시 콜백용
};