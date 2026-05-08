#include "Player.h"

CPlayer::CPlayer(int64_t sessionId)
    : _sessionId(sessionId)
    , _accountId(0)  // 초기값 0 (미인증 상태)
    , _score(0)
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

int32_t CPlayer::GetScore() const
{
    return _score;
}

void CPlayer::SetScore(int32_t score)
{
    _score = score;
}

void CPlayer::AddScore(int32_t delta)
{
    _score += delta;
}

