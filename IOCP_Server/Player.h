#pragma once

#include <cstdint>
#include <string>
#include <random>

// 8방향
enum class Direction : uint8_t
{
    NONE = 0,
    UP,          // ↑
    DOWN,        // ↓
    LEFT,        // ←
    RIGHT,       // →
    UP_LEFT,     // ↖
    UP_RIGHT,    // ↗
    DOWN_LEFT,   // ↙
    DOWN_RIGHT   // ↘
};

// 이동 상태
enum class MoveState : uint8_t
{
    IDLE = 0,
    MOVING
};

class CPlayer
{
    friend class CGameServer;  // 경계 계층만 _sessionId 접근 허용

public:
    CPlayer();
    ~CPlayer();

    // 게임 로직 계층 식별자 (CentralizedServer용)
    int64_t GetAccountId() const;
    void SetAccountId(int64_t accountId);

    int64_t _accountId;   // 계정 ID (로그인/DB용)
    int32_t _playerId;    // 게임 월드 내 식별자 (클라이언트에 노출)
    uint8_t _displayChar; // 표시 문자 (ASCII: A-Z, a-z, 0-9)
    uint8_t _colorIndex;  // 색상 인덱스 (0-6)

    // 월드 좌표
    float _x;
    float _y;

    // 섹터 좌표
    int32_t _sectorX;
    int32_t _sectorY;

    Direction _direction;
    MoveState _moveState;
    int32_t _speed;

    int32_t _zoneId;      // 소속 존 ID (-1: 미배정)

    // 이동 검증용
    uint32_t _cheatCount; // 위반 누적 카운터

private:
    int64_t _sessionId;   // 네트워크 세션 ID (CGameServer만 접근)
};