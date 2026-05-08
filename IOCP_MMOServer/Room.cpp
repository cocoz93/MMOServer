#include "Room.h"
#include "Player.h"
#include <algorithm>

CRoom::CRoom(int32_t roomId, const std::string& title, int32_t maxPlayers)
    : _roomId(roomId)
    , _title(title)
    , _currentPlayers(0)
    , _maxPlayers(maxPlayers)
    , _status(RoomStatus::WAITING)
    , _owner(nullptr)
{
    _players.reserve(maxPlayers);
}

CRoom::~CRoom()
{
}

bool CRoom::AddPlayer(std::shared_ptr<CPlayer> player)
{
    if (!player)
    {
        return false;
    }

    if (IsFull())
    {
        return false;
    }

    // 중복 체크
    if (IsPlayerInRoom(player))
    {
        return false;
    }

    _players.push_back(player);
    _currentPlayers = static_cast<int32_t>(_players.size());

    // 방에 첫 입장한 플레이어를 방장으로 지정
    if (_owner == nullptr)
    {
        _owner = player;
    }

    return true;
}

bool CRoom::RemovePlayer(std::shared_ptr<CPlayer> player)
{
    if (!player)
    {
        return false;
    }

    auto it = std::find(_players.begin(), _players.end(), player);
    if (it == _players.end())
    {
        return false;
    }

    // 방장이 나가는 경우 방장 변경 처리
    UpdateOwnerOnLeave(player);

    _players.erase(it);
    _currentPlayers = static_cast<int32_t>(_players.size());
    return true;
}

void CRoom::UpdateOwnerOnLeave(std::shared_ptr<CPlayer> leavingPlayer)
{
    // 방장이 나가는 경우
    if (_owner == leavingPlayer)
    {
        // 나가는 플레이어를 제외하고 다음 플레이어를 찾음
        for (auto& player : _players)
        {
            if (player != leavingPlayer)
            {
                _owner = player;
                return;
            }
        }
        
        // 방에 아무도 없으면 nullptr
        _owner = nullptr;
    }
}

bool CRoom::IsPlayerInRoom(std::shared_ptr<CPlayer> player) const
{
    if (!player)
    {
        return false;
    }

    return std::find(_players.begin(), _players.end(), player) != _players.end();
}