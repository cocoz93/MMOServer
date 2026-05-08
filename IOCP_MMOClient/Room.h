#pragma once

#include "Protocol.h"
#include "Tetris.h"
#include <string>
#include <vector>
#include <mutex>
#include <iostream>

class CClientNetwork; // 전방 선언

struct RoomPlayer
{
    bool isMe;
    bool isPresent;
    std::string name;
};

class CRoom
{
public:
    CRoom();
    ~CRoom();

    // 네트워크 설정
    void SetNetwork(CClientNetwork* network) { _network = network; }

    // 방 요청 (네트워크를 통해 전송)
    void RequestCreateRoom(const std::string& title, int32_t maxPlayers);
    void RequestJoinRoom(int32_t roomId);
    void RequestLeaveRoom();

    // 서버 응답 처리
    void OnRoomCreated(int32_t roomId);
    void OnRoomJoined(int32_t roomId);
    void OnRoomLeft();

    // 방 상태
    bool IsInRoom() const { return _inRoom; }
    int32_t GetRoomId() const { return _roomId; }

    // 플레이어 관리
    void InitPlayers();
    void AddPlayer(int index, const std::string& name);
    void RemovePlayer(int index);

    // 렌더링
    void DisplayRoomInfo();
    void DisplayRoomView();
    void DisplayRoomList(const std::vector<RoomInfo>& rooms);

    // Tetris 접근
    CTetris& GetTetris() { return _tetris; }

private:
    CClientNetwork* _network;
    CTetris _tetris;

    bool _inRoom;
    int32_t _roomId;
    std::string _title;
    int32_t _maxPlayers;

    std::vector<RoomPlayer> _players;
    std::mutex _mutex;

    // 방 생성 시 임시 저장용
    std::string _pendingTitle;
    int32_t _pendingMaxPlayers;
};