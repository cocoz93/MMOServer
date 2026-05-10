#pragma once

#include <cstdint>
#include <string>

// 4방향
enum class Direction : uint8_t
{
    NONE = 0,
    UP,          // ↑
    DOWN,        // ↓
    LEFT,        // ←
    RIGHT        // →
};

// 이동 상태
enum class MoveState : uint8_t
{
    IDLE = 0,
    MOVING
};

class CPlayer
{
public:
    explicit CPlayer(int64_t sessionId);
    ~CPlayer();

    // 네트워크 세션 식별자 (IOCPServer와 통신용)
    int64_t GetSessionId() const;

    // 게임 로직 계층 식별자 (CentralizedServer용)
    int64_t GetAccountId() const;
    void SetAccountId(int64_t accountId);

    int64_t _sessionId;   // 네트워크 세션 ID (IOCPServer에서 부여, 서버 내부용)
    int64_t _accountId;   // 계정 ID (로그인/DB용)
    int32_t _playerId;    // 게임 월드 내 식별자 (클라이언트에 노출)

    // 월드 좌표
    float _x;
    float _y;

    // 섹터 좌표
    int32_t _sectorX;
    int32_t _sectorY;

    Direction _direction;
    MoveState _moveState;

    int32_t _zoneId;      // 소속 존 ID (-1: 미배정)
};