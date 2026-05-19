#pragma once

#include "ClientPlayer.h"
#include <unordered_map>

// 내 캐릭터 + 주변 플레이어 목록 관리
class CPlayerManager
{
public:
    // 내 캐릭터 설정
    void SetMyPlayer(int32_t playerId, float x, float y, Direction direction, int32_t speed,
                     uint8_t displayChar, uint8_t colorIndex)
    {
        _myPlayer.playerId = playerId;
        _myPlayer.displayChar = displayChar;
        _myPlayer.colorIndex = colorIndex;
        _myPlayer.x = x;
        _myPlayer.y = y;
        _myPlayer.direction = direction;
        _myPlayer.moveState = MoveState::IDLE;
        _myPlayer.speed = speed;
        _hasMyPlayer = true;
    }

    // 다른 플레이어 추가 (시야 진입)
    void AddOtherPlayer(int32_t playerId, float x, float y,
                        Direction direction, MoveState moveState, int32_t speed,
                        uint8_t displayChar, uint8_t colorIndex)
    {
        ClientPlayer player;
        player.playerId = playerId;
        player.displayChar = displayChar;
        player.colorIndex = colorIndex;
        player.x = x;
        player.y = y;
        player.direction = direction;
        player.moveState = moveState;
        player.speed = speed;
        _otherPlayers[playerId] = player;
    }

    // 플레이어 제거 (시야 이탈 / 퇴장)
    void RemovePlayer(int32_t playerId)
    {
        _otherPlayers.erase(playerId);
    }

    // 전체 초기화 (존 이동 시)
    void Clear()
    {
        _hasMyPlayer = false;
        _myPlayer = ClientPlayer{};
        _otherPlayers.clear();
    }

    // MOVING 상태인 플레이어들의 좌표를 방향별로 갱신
    void UpdateMovingPlayers(float deltaTime)
    {
        if (_hasMyPlayer && _myPlayer.moveState == MoveState::MOVING)
        {
            ApplyMovement(_myPlayer, deltaTime);
            ClampToMap(_myPlayer);
        }

        for (auto& pair : _otherPlayers)
        {
            if (pair.second.moveState == MoveState::MOVING)
            {
                ApplyMovement(pair.second, deltaTime);
                ClampToMap(pair.second);
            }
        }
    }

    // 조회
    bool HasMyPlayer() const { return _hasMyPlayer; }
    ClientPlayer* GetMyPlayer() { return _hasMyPlayer ? &_myPlayer : nullptr; }

    ClientPlayer* FindPlayer(int32_t playerId)
    {
        auto it = _otherPlayers.find(playerId);
        if (it != _otherPlayers.end())
            return &it->second;
        return nullptr;
    }

    const std::unordered_map<int32_t, ClientPlayer>& GetOtherPlayers() const
    {
        return _otherPlayers;
    }

public:
    // 맵 크기 설정 (서버에서 S2C_ZONE_INFO로 수신)
    void SetMapSize(int width, int height)
    {
        _mapWidth = width;
        _mapHeight = height;
    }

private:
    // 맵 크기 (서버에서 S2C_ZONE_INFO로 수신)
    int _mapWidth = 120;
    int _mapHeight = 120;

    // 맵 경계 클램핑 (서버 Zone::Tick과 동일 조건)
    void ClampToMap(ClientPlayer& player)
    {
        if (player.x < 0.0f)                          player.x = 0.0f;
        if (player.x >= static_cast<float>(_mapWidth))  player.x = static_cast<float>(_mapWidth) - 1.0f;
        if (player.y < 0.0f)                          player.y = 0.0f;
        if (player.y >= static_cast<float>(_mapHeight)) player.y = static_cast<float>(_mapHeight) - 1.0f;
    }

    // 방향별 좌표 이동 적용
    static void ApplyMovement(ClientPlayer& player, float deltaTime)
    {
        static constexpr float DIAGONAL_FACTOR = 0.7071f; // 1/√2
        float dist = player.speed * deltaTime;
        switch (player.direction)
        {
        case Direction::UP:         player.y -= dist; break;
        case Direction::DOWN:       player.y += dist; break;
        case Direction::LEFT:       player.x -= dist; break;
        case Direction::RIGHT:      player.x += dist; break;
        case Direction::UP_LEFT:    player.x -= dist * DIAGONAL_FACTOR; player.y -= dist * DIAGONAL_FACTOR; break;
        case Direction::UP_RIGHT:   player.x += dist * DIAGONAL_FACTOR; player.y -= dist * DIAGONAL_FACTOR; break;
        case Direction::DOWN_LEFT:  player.x -= dist * DIAGONAL_FACTOR; player.y += dist * DIAGONAL_FACTOR; break;
        case Direction::DOWN_RIGHT: player.x += dist * DIAGONAL_FACTOR; player.y += dist * DIAGONAL_FACTOR; break;
        default: break;
        }
    }

private:
    ClientPlayer _myPlayer;
    bool _hasMyPlayer = false;
    std::unordered_map<int32_t, ClientPlayer> _otherPlayers;
};
