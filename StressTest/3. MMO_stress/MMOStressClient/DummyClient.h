#pragma once
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <cstdint>
#include <string>
#include <random>
#include "RingBuffer.h"
#include "../../../Shared/Protocol/Protocol.h"

struct MMOStats;
struct DummyConfig;

enum class ClientState { DISCONNECTED, CONNECTING, CONNECTED };

class DummyClient
{
public:
    DummyClient()  = default;
    ~DummyClient() { CloseSocket(); }

    // DummyManager가 상태 전이를 트리거
    void StartConnect(const std::string& ip, int port, MMOStats& stats, int reconnectDelayMs);
    void OnConnected(MMOStats& stats);
    void OnConnectFailed(MMOStats& stats, int reconnectDelayMs);

    // select()가 읽기 가능 표시 시 호출
    void OnRecv(MMOStats& stats, int reconnectDelayMs);

    // OnRecv 후 RingBuffer에서 완성된 패킷 파싱
    void ProcessPackets(MMOStats& stats, const DummyConfig& config);

    // 40ms 도달 시 행동 (이동/정지)
    void Tick(MMOStats& stats, const DummyConfig& config);

    // 하트비트 (20초 주기)
    void CheckHeartbeat(MMOStats& stats, int heartbeatIntervalSec);

    // 송신 링버퍼 → 실제 send()
    void FlushSend(MMOStats& stats, int reconnectDelayMs);

    bool IsReadyToConnect() const;
    bool IsReady()        const { return _ready; }
    bool IsConnected()    const { return _state == ClientState::CONNECTED;    }
    bool IsConnecting()   const { return _state == ClientState::CONNECTING;   }
    bool IsDisconnected() const { return _state == ClientState::DISCONNECTED; }
    SOCKET GetSocket()    const { return _sock; }

private:
    void CloseSocket();
    void Disconnect(MMOStats& stats, int reconnectDelayMs);
    void ResetState();

    // 패킷 핸들러
    void HandleCreateMyPlayer(const char* packet);
    void HandleMoveStart(const char* packet);
    void HandleMoveStop(const char* packet);
    void HandleSyncPosition(const char* packet);
    void HandleCreateOtherPlayer(const char* packet);
    void HandleDeletePlayer(const char* packet);
    void HandleChat(const char* packet);
    void HandleZoneChangeOk(const char* packet);
    void HandleZoneChangeFail(const char* packet);

    // 이동 로직
    void UpdateLocalPosition(int mapWidth, int mapHeight);
    void SendMoveStart(MMOStats& stats);
    void SendMoveStop(MMOStats& stats);
    void SendHeartbeat(MMOStats& stats);
    void SendChat(MMOStats& stats);
    void SendZoneChange(MMOStats& stats, int targetMapId);

    // 패킷 크기 테이블
    static uint16_t GetPacketSize(MsgType type);

    static int64_t NowMs();

    SOCKET      _sock           = INVALID_SOCKET;
    ClientState _state          = ClientState::DISCONNECTED;
    int64_t     _connectReadyMs = 0;

    // 게임 상태
    bool        _ready          = false;
    int32_t     _playerId       = 0;
    float       _x              = 0.0f;
    float       _y              = 0.0f;
    int32_t     _speed          = 0;
    uint8_t     _direction      = 0;   // Direction enum (0=NONE, 1=UP ~ 8=DOWN_RIGHT)
    bool        _moving         = false;

    // 타이밍
    int64_t     _lastTickMs      = 0;
    int64_t     _lastHeartbeatMs = 0;
    int64_t     _moveStartSentMs = 0;  // RTT 측정용: SendMoveStart 전송 시각
    int64_t     _lastRttMs       = -1; // HandleMoveStart에서 측정한 RTT (-1 = 미측정)

    // 난수 생성기 (스레드 로컬이 아닌 인스턴스별)
    std::mt19937 _rng{std::random_device{}()};

    CRingBufferST  _recvBuf;
    CRingBufferST  _sendBuf;
};
