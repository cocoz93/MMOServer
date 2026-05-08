#pragma once

#include "Room.h"
#include "Player.h"
#include <memory>
#include <list>
#include <unordered_map>
#include <mutex>
#include <cstdint>

// 방 관리자 클래스
class CRoomManager
{
public:
    CRoomManager();
    ~CRoomManager();

    // 방 생성 및 삭제
    std::shared_ptr<CRoom> CreateRoom(const std::string& title, int32_t maxPlayers = 10);
    bool DeleteRoom(int32_t roomId);

    // 방 검색
    std::shared_ptr<CRoom> FindRoom(int32_t roomId);
    std::shared_ptr<CRoom> FindRoomByPlayer(std::shared_ptr<CPlayer> player);

    // 방 이름으로 방 찾기 (중복 체크용)
    std::shared_ptr<CRoom> FindRoomByTitle(const std::string& title);

    // 방 목록 조회 (최근 생성 순)
    std::list<std::shared_ptr<CRoom>> GetRoomList() const;

    // 플레이어 입장/퇴장 (CPlayer 기반)
    bool JoinRoom(int32_t roomId, std::shared_ptr<CPlayer> player);
    bool LeaveRoom(std::shared_ptr<CPlayer> player);

    // 통계
    int32_t GetRoomCount() const;
    int32_t GetTotalPlayerCount() const;

private:
    std::atomic<int32_t> _roomIdCounter;
    
    // 최근 생성 순서 유지 (리스트)
    std::list<std::shared_ptr<CRoom>> _roomList;
    
    // 빠른 검색을 위한 맵 <roomId, CRoom>
    std::unordered_map<int32_t, std::shared_ptr<CRoom>> _roomMap;
    
    // 플레이어별 방 매핑 (플레이어가 현재 어느 방에 있는지) <player, roomId>
    std::unordered_map<std::shared_ptr<CPlayer>, int32_t> _playerToRoomMap;
    
    // const인 함수에서도 락을 잡을 수 있도록 mutable 
    mutable std::mutex _mutex;
};