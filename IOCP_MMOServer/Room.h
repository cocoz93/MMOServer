#pragma once

#include <string>
#include <atomic>
#include <vector>
#include <memory>
#include <cstdint>

// 전방 선언
class CPlayer;

// 방 상태
enum class RoomStatus
{
    WAITING,  // 대기 중
    PLAYING   // 게임 중
};

// 방 클래스
class CRoom
{
public:
    explicit CRoom(int32_t roomId, const std::string& title, int32_t maxPlayers = 10);
    ~CRoom();

    // Getter
    int32_t GetRoomId() const { return _roomId; }
    std::string GetTitle() const { return _title; }
    int32_t GetCurrentPlayerCount() const { return _currentPlayers; }
    int32_t GetMaxPlayers() const { return _maxPlayers; }
    RoomStatus GetStatus() const { return _status; }
    
    // 방장 관련
    std::shared_ptr<CPlayer> GetOwner() const { return _owner; }

    // 플레이어 관리 (CPlayer 기반)
    bool AddPlayer(std::shared_ptr<CPlayer> player);
    bool RemovePlayer(std::shared_ptr<CPlayer> player);
    bool IsPlayerInRoom(std::shared_ptr<CPlayer> player) const;
    const std::vector<std::shared_ptr<CPlayer>>& GetPlayers() const { return _players; }

    // 상태 관리
    void SetStatus(RoomStatus status) { _status = status; }
    
    // 방이 가득 찼는지 확인
    bool IsFull() const { return _currentPlayers >= _maxPlayers; }
    bool IsEmpty() const { return _currentPlayers == 0; }

private:
    void UpdateOwnerOnLeave(std::shared_ptr<CPlayer> leavingPlayer);

    int32_t _roomId;
    std::string _title;
    int32_t _currentPlayers;
    int32_t _maxPlayers;
    RoomStatus _status;
    std::shared_ptr<CPlayer> _owner; // 방장

    std::vector<std::shared_ptr<CPlayer>> _players; // 방에 있는 플레이어 객체 목록
    // 인자수가 1~200개 일때는 거의 대부분 vector가 빠름
};