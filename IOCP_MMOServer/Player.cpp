#include "Player.h"

CPlayer::CPlayer(int64_t sessionId)
    : _sessionId(sessionId)
    , _accountId(0)
    , _playerId(0)
    , _x(0.0f)
    , _y(0.0f)
    , _sectorX(0)
    , _sectorY(0)
    , _direction(Direction::NONE)
    , _moveState(MoveState::IDLE)
    , _zoneId(-1)
{
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

