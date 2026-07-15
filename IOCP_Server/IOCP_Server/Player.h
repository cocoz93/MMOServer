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

    int32_t _playerId;    // S2C 패킷 전용 엔티티 식별자 (서버 내부는 CPlayer*로 식별)
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

    int32_t _listIndex = -1;  // Zone::_playerList 내 인덱스 (O(1) 삭제용)
    int32_t _sectorIndex = -1;  // 현재 소속 섹터 벡터 내 인덱스 (O(1) 삭제용 — _listIndex와 동일 패턴)

    // USE_SECTOR_AGGREGATION: 이번 틱 섹터 묶음 dirty 등록 여부 (중복 방지, 틱 끝 리셋)
    bool _moveDirty = false;

    // USE_DB_WORKER: 계정 식별자 (1단계는 스폰 시 서버가 임시 부여, 2단계는 접속 핸드셰이크로 수신)
    int64_t _accountId = 0;
    // USE_DB_WORKER: 마지막 저장 이후 위치가 바뀌었는지 (주기 저장 대상 선별, 저장 후 리셋)
    bool _dbDirty = false;

private:
    int64_t _sessionId;   // 네트워크 세션 ID (CGameServer만 접근)
};