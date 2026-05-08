//
#include "RoomManager.h"
#include <iostream>

CRoomManager::CRoomManager()
    : _roomIdCounter(1)
{
}

CRoomManager::~CRoomManager()
{
    _roomList.clear();
    _roomMap.clear();
    _playerToRoomMap.clear();
}

std::shared_ptr<CRoom> CRoomManager::CreateRoom(const std::string& title, int32_t maxPlayers)
{
    int32_t roomId = _roomIdCounter++;
    auto room = std::make_shared<CRoom>(roomId, title, maxPlayers);

    _roomList.push_front(room); // 리스트 앞에 추가 (최근 생성 순)
    _roomMap[roomId] = room; // 맵에 추가 (검색용)

    std::cout << "[RoomManager] Room created - ID: " << roomId 
              << ", Title: " << title << std::endl;

    return room;
}

bool CRoomManager::DeleteRoom(int32_t roomId)
{
    auto it = _roomMap.find(roomId);
    if (it == _roomMap.end())
    {
        return false;
    }

    auto room = it->second;

    // 방에 있는 모든 플레이어의 매핑 제거
    for (auto& player : room->GetPlayers())
    {
        _playerToRoomMap.erase(player);
    }

    // 리스트에서 제거
    _roomList.remove(room);
    _roomMap.erase(it);

    std::cout << "[RoomManager] Room deleted - ID: " << roomId << std::endl;

    return true;
}

std::shared_ptr<CRoom> CRoomManager::FindRoom(int32_t roomId)
{
    auto it = _roomMap.find(roomId);
    return (it != _roomMap.end()) ? it->second : nullptr;
}

std::shared_ptr<CRoom> CRoomManager::FindRoomByPlayer(std::shared_ptr<CPlayer> player)
{
    if (!player)
    {
        return nullptr;
    }

    auto it = _playerToRoomMap.find(player);
    if (it == _playerToRoomMap.end())
    {
        return nullptr;
    }

    return FindRoom(it->second);
}

std::shared_ptr<CRoom> CRoomManager::FindRoomByTitle(const std::string& title)
{
    for (const auto& room : _roomList)
    {
        if (room->GetTitle() == title)
            return room;
    }
    return nullptr;
}

std::list<std::shared_ptr<CRoom>> CRoomManager::GetRoomList() const
{
    return _roomList;
}

bool CRoomManager::JoinRoom(int32_t roomId, std::shared_ptr<CPlayer> player)
{
    if (!player)
    {
        return false;
    }

    // 이미 다른 방에 있는지 확인
    if (_playerToRoomMap.find(player) != _playerToRoomMap.end())
    {
        return false; 
    }

    auto room = FindRoom(roomId);
    if (!room)
    {
        return false;
    }

    if (!room->AddPlayer(player))
    {
        return false;
    }

    _playerToRoomMap[player] = roomId;
    return true;
}

bool CRoomManager::LeaveRoom(std::shared_ptr<CPlayer> player)
{
    if (!player)
    {
        return false;
    }

    auto it = _playerToRoomMap.find(player);
    if (it == _playerToRoomMap.end())
    {
        return false;
    }

    int32_t roomId = it->second;
    auto room = FindRoom(roomId);
    if (!room)
    {
        _playerToRoomMap.erase(it);
        return false;
    }

    room->RemovePlayer(player);
    _playerToRoomMap.erase(it);

    std::cout << "[RoomManager] Player (AccountId: " << player->GetAccountId() 
              << ", SessionId: " << player->GetSessionId()
              << ") left room " << roomId << std::endl;

    // 방이 비면 자동 삭제
    if (room->IsEmpty())
    {
        DeleteRoom(roomId);
    }

    return true;
}

int32_t CRoomManager::GetRoomCount() const
{
    return static_cast<int32_t>(_roomMap.size());
}

int32_t CRoomManager::GetTotalPlayerCount() const
{
    return static_cast<int32_t>(_playerToRoomMap.size());
}