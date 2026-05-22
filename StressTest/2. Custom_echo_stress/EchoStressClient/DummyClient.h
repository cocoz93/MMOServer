#pragma once
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <cstdint>
#include <deque>
#include <string>
#include "RingBuffer.h"
#include "../../../Shared/Protocol/Protocol.h"

struct Stats;

enum class ClientState { DISCONNECTED, CONNECTING, CONNECTED };

class DummyClient
{
public:
    DummyClient()  = default;
    ~DummyClient() { CloseSocket(); }

    // DummyManager가 상태 전이를 트리거
    void StartConnect(const std::string& ip, int port, Stats& stats, int reconnectDelayMs);
    void OnConnected(Stats& stats, int reconnectDelayMs);
    void OnConnectFailed(Stats& stats, int reconnectDelayMs);

    // select()가 읽기 가능 표시 시 호출
    void OnRecv(Stats& stats, int reconnectDelayMs);

    // OnRecv 후 RingBuffer에서 완성된 패킷 파싱
    void ProcessPackets(Stats& stats, int reconnectDelayMs, int maxPacketSize);

    // 에코 송신 시도 (pendingCount < overSendCount 이면 송신버퍼에 enqueue)
    void TrySend(int overSendCount, int minPacketSize, int maxPacketSize, int reconnectDelayMs, Stats& stats);

    // 송신 링버퍼 → 실제 send() (partial send 안전 처리)
    void FlushSend(int reconnectDelayMs, Stats& stats);

    // EchoTimeoutMs 초과 여부 확인
    void CheckTimeout(int echoTimeoutMs, Stats& stats);

    // 공격 테스트: 비정상 패킷 크기 송신 (mode 1)
    void SendAttackInvalidSize(Stats& stats);

    // DisconnectTest 전용: 랜덤 타이밍으로 강제 연결 해제
    void ScheduleDisconnect(int reconnectIntervalMs);
    void CheckForcedDisconnect(int reconnectDelayMs, Stats& stats);

    bool IsReadyToConnect() const;
    bool IsConnected()    const { return _state == ClientState::CONNECTED;    }
    bool IsConnecting()   const { return _state == ClientState::CONNECTING;   }
    bool IsDisconnected() const { return _state == ClientState::DISCONNECTED; }
    SOCKET GetSocket()    const { return _sock; }
    void CloseSocket();

private:
    void Disconnect(int reconnectDelayMs, Stats& stats);
    void ResetEchoState();

    static int64_t NowMs();

    SOCKET      _sock           = INVALID_SOCKET;
    ClientState _state          = ClientState::DISCONNECTED;
    int64_t     _connectReadyMs = 0;

    // 에코 상태 (연결마다 초기화)
    uint64_t    _sentValue      = 0;   // 마지막 송신 echoValue
    uint64_t    _expectedRecv   = 1;   // 다음으로 받아야 할 echoValue
    int         _pendingCount   = 0;   // 미응답 패킷 수
    std::deque<int64_t> _sendTimes;    // 송신 시각 큐 (RTT 측정용)

    CRingBufferST  _recvBuf;
    CRingBufferST  _sendBuf;

    // DisconnectTest
    int64_t     _nextDisconMs       = 0;
    bool        _disconnectScheduled = false;
};
