#pragma once

#include <cstdint>

// 8방향 (서버 Player.h와 동일)
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

// 이동 상태 (서버 Player.h와 동일)
enum class MoveState : uint8_t
{
    IDLE = 0,
    MOVING
};

// 클라이언트용 플레이어 데이터
struct ClientPlayer
{
    int32_t playerId = 0;
    uint8_t displayChar = 'A';  // 서버에서 할당받은 표시 문자
    uint8_t colorIndex = 0;     // 서버에서 할당받은 색상 인덱스
    float x = 0.0f;
    float y = 0.0f;
    Direction direction = Direction::NONE;
    MoveState moveState = MoveState::IDLE;
    int32_t speed = 50;
};
