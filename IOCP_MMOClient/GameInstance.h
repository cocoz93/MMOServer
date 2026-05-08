#pragma once

#include "ClientNetwork.h"
#include "Room.h"
#include <memory>
#include <string>

class CGameInstance
{
public:
    CGameInstance();
    ~CGameInstance();

    // 초기화 및 실행
    bool Initialize();
    void Run();
    void Shutdown();

    // 네트워크 연결
    bool ConnectToServer(const std::string& serverIp, int port);

    // 패킷 핸들러 (CClientNetwork로부터 호출됨)
    void OnRoomListReceived(const MSG_S2C_ROOM_LIST* msg, size_t msgSize);
    void OnRoomCreated(const MSG_S2C_ROOM_CREATED* msg);
    void OnRoomJoined(const MSG_S2C_ROOM_JOINED* msg);
    void OnRoomLeft(const MSG_S2C_ROOM_LEFT* msg);
    void OnError(const MSG_S2C_ERROR* msg);

private:
    int ShowMainMenuWithSelection(); // 방향키로 선택하는 메뉴
    void ShowLobbyMenu();
    void ProcessLobbyInput();
    void ProcessRoomInput();

private:
    CClientNetwork _network;
    CRoom _room;
    bool _running;
};