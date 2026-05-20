#include "Player.h"

CPlayer::CPlayer()
    : _sessionId(-1)
    , _accountId(0)
    , _playerId(0)
    , _displayChar('A')
    , _colorIndex(0)
    , _x(0.0f)
    , _y(0.0f)
    , _sectorX(0)
    , _sectorY(0)
    , _direction(Direction::NONE)
    , _moveState(MoveState::IDLE)
    , _speed(50)
    , _zoneId(-1)
    , _isAdmin(false)
    , _lastSyncX(0.0f)
    , _lastSyncY(0.0f)
{
    // 25 ~ 50 (5 단위) 랜덤 속도 부여
    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(5, 10);
    _speed = dist(rng) * 5;
}

CPlayer::~CPlayer()
{
}

int64_t CPlayer::GetAccountId() const
{
    return _accountId;
}

void CPlayer::SetAccountId(int64_t accountId)
{
    _accountId = accountId;
}

