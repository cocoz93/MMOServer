#pragma once

#include "ClientPlayer.h"
#include <unordered_map>

// 내 캐릭터 + 주변 플레이어 목록 관리
class CPlayerManager
{
public:
    // 내 캐릭터 설정
    void SetMyPlayer(int32_t playerId, float x, float y, Direction direction, int32_t speed)
    {
        _myPlayer.playerId = playerId;
        _myPlayer.x = x;
        _myPlayer.y = y;
        _myPlayer.direction = direction;
        _myPlayer.moveState = MoveState::IDLE;
        _myPlayer.speed = speed;
        _hasMyPlayer = true;
    }

    // 다른 플레이어 추가 (시야 진입)
    void AddOtherPlayer(int32_t playerId, float x, float y,
                        Direction direction, MoveState moveState, int32_t speed)
    {
        ClientPlayer player;
        player.playerId = playerId;
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
        }

        for (auto& pair : _otherPlayers)
        {
            if (pair.second.moveState == MoveState::MOVING)
            {
                ApplyMovement(pair.second, deltaTime);
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

private:
    // 방향별 좌표 이동 적용
    static void ApplyMovement(ClientPlayer& player, float deltaTime)
    {
        float dist = player.speed * deltaTime;
        switch (player.direction)
        {
        case Direction::UP:    player.y -= dist; break;
        case Direction::DOWN:  player.y += dist; break;
        case Direction::LEFT:  player.x -= dist; break;
        case Direction::RIGHT: player.x += dist; break;
        default: break;
        }
    }

private:
    ClientPlayer _myPlayer;
    bool _hasMyPlayer = false;
    std::unordered_map<int32_t, ClientPlayer> _otherPlayers;
};
