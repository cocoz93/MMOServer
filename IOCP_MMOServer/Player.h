#pragma once

#include <cstdint>
#include <string>

class CPlayer
{
public:
    explicit CPlayer(int64_t sessionId);
    ~CPlayer();

    // 네트워크 계층 식별자 (IOCPServer와 통신용)
    int64_t GetSessionId() const;

    // 게임 로직 계층 식별자 (CentralizedServer, RoomManager용)
    int64_t GetAccountId() const;
    void SetAccountId(int64_t accountId);

    int32_t GetScore() const;
    void SetScore(int32_t score);
    void AddScore(int32_t delta);

private:
    int64_t _sessionId;   // 네트워크 세션 ID (IOCPServer에서 관리)
    int64_t _accountId;   // 계정 ID (게임 로직에서 사용)
    int32_t _score;       // 플레이어 점수
};