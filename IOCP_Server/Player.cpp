#include "Player.h"

CPlayer::CPlayer(int64_t sessionId)
    : _sessionId(sessionId)
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
    , _cheatCount(0)
{
    // 50 ~ 200 (10 단위) 랜덤 속도 부여
    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(5, 20);
    _speed = dist(rng) * 10;
}

CPlayer::~CPlayer()
{
}

int64_t CPlayer::GetSessionId() const
{
    return _sessionId;
}

int64_t CPlayer::GetAccountId() const
{
    return _accountId;
}

void CPlayer::SetAccountId(int64_t accountId)
{
    _accountId = accountId;
}

