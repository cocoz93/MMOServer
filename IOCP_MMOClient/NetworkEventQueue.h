#pragma once

#include "ClientPlayer.h"
#include <cstdint>
#include <queue>
#include <mutex>
#include <string>

// 수신 스레드 → 게임 루프 간 이벤트 전달용 구조체
struct ClientNetworkEvent
{
    enum class Type
    {
        CREATE_MY_PLAYER,
        CREATE_OTHER_PLAYER,
        DELETE_PLAYER,
        MOVE_START,
        MOVE_STOP,
        CHAT,
        SYNC_POSITION,
        ZONE_CHANGE_OK,
        ZONE_CHANGE_FAIL,
        ERROR_MSG
    };

    Type type;

    // 공용 필드 (패킷 타입에 따라 사용)
    int32_t playerId;
    float x;
    float y;
    uint8_t direction;
    uint8_t moveState;

    // 존 이동
    int32_t mapId;
    int32_t channelIndex;
    uint8_t reason;

    // 채팅 / 에러
    wchar_t chatMessage[64];
    char errorMessage[256];
};

// 스레드 안전 이벤트 큐
class CNetworkEventQueue
{
public:
    void Push(ClientNetworkEvent&& event)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _queue.push(std::move(event));
    }

    bool Pop(ClientNetworkEvent& out)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_queue.empty())
            return false;
        out = _queue.front();
        _queue.pop();
        return true;
    }

private:
    std::queue<ClientNetworkEvent> _queue;
    std::mutex _mutex;
};
