#pragma once

#include <cstdint>

// 4방향 (서버 Player.h와 동일)
enum class Direction : uint8_t
{
    NONE = 0,
    UP,          // ↑
    DOWN,        // ↓
    LEFT,        // ←
    RIGHT        // →
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
    static constexpr float MOVE_SPEED = 5.0f;  // 초당 5타일 (서버 동일)

    int32_t playerId = 0;
    float x = 0.0f;
    float y = 0.0f;
    Direction direction = Direction::NONE;
    MoveState moveState = MoveState::IDLE;
};
