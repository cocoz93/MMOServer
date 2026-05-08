#pragma once

#include "IOCPServer.h"
#include "RoomManager.h"
#include "Protocol.h"
#include "Player.h"
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>
#include <unordered_map>

// 중앙 집중형 게임 로직 레이어 - 별도 스레드에서 동작
class CCentralizedServer
{
public:
    explicit CCentralizedServer(int port, int maxClients, int mainlogicTickMs = -1);
    virtual ~CCentralizedServer();

    bool Start();
    void Stop();

private:
    void GameLogicThread();

    // 네트워크 이벤트 처리 //////////////////////////////////////////////////////////
    void DispatchClientConnected(int64_t sessionId);
    void DispatchClientDisconnected(int64_t sessionId);
    void DispatchDataReceived(int64_t sessionId, const char* data, size_t length);
    ////////////////////////////////////////////////////////////////////////////////

    // 패킷 핸들러 (CPlayer 기반)
    void HandleRequestRoomList(std::shared_ptr<CPlayer> player);
    void HandleCreateRoom(std::shared_ptr<CPlayer> player, const MSG_C2S_CREATE_ROOM* msg);
    void HandleJoinRoom(std::shared_ptr<CPlayer> player, const MSG_C2S_JOIN_ROOM* msg);
    void HandleLeaveRoom(std::shared_ptr<CPlayer> player);

    // 패킷 전송 헬퍼
    void SendRoomList(std::shared_ptr<CPlayer> player);
    void SendRoomCreated(std::shared_ptr<CPlayer> player, int32_t roomId, bool success);
    void SendRoomJoined(std::shared_ptr<CPlayer> player, int32_t roomId, bool success);
    void SendRoomLeft(std::shared_ptr<CPlayer> player, bool success);
    void SendError(std::shared_ptr<CPlayer> player, const std::string& message);

    void ProcessGameLogic();

    // 플레이어 관리
    std::shared_ptr<CPlayer> GetPlayer(int64_t sessionId);
    void AddPlayer(std::shared_ptr<CPlayer> player);
    void RemovePlayer(int64_t sessionId);

private:
    std::shared_ptr<CIOCPServer> _networkServer;
    std::shared_ptr<CRoomManager> _roomManager;
    std::thread _gameThread;
    std::atomic<bool> _running;
    int _mainlogicTickMs;

    // 플레이어 관리 (sessionId -> Player)
    std::unordered_map<int64_t, std::shared_ptr<CPlayer>> _players;
};
