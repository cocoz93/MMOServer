#pragma once

#include <cstdint>
#include <string>
#include <random>

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
    friend class CGameServer;  // 경계 계층만 _sessionId 접근 허용

public:
    CPlayer();
    ~CPlayer();

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
    bool _isAdmin;        // 운영자 여부

    // 델타 동기화: 마지막 위치 동기화 시점의 좌표
    float _lastSyncX;
    float _lastSyncY;

private:
    int64_t _sessionId;   // 네트워크 세션 ID (CGameServer만 접근)
};