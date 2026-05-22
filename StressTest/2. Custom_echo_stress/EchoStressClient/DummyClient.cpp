#include "DummyClient.h"
#include <Windows.h>
#include "Stats.h"
#include <cstring>
#include <random>

// ─────────────────────────────────────────────────────────────────
// 유틸
// ─────────────────────────────────────────────────────────────────
static void FillDeterministicPadding(char* dst, size_t len, uint64_t seed)
{
    std::mt19937 rng(static_cast<unsigned>(seed));
    for (size_t i = 0; i < len; ++i)
        dst[i] = static_cast<char>(rng() & 0xFF);
}

int64_t DummyClient::NowMs()
{
    return static_cast<int64_t>(GetTickCount64());
}

void DummyClient::CloseSocket()
{
    if (_sock != INVALID_SOCKET)
    {
        closesocket(_sock);
        _sock = INVALID_SOCKET;
    }
}

void DummyClient::ResetEchoState()
{
    _sentValue    = 0;
    _expectedRecv = 1;
    _pendingCount = 0;
    _sendTimes.clear();
    _recvBuf.Clear();
    _sendBuf.Clear();
}

void DummyClient::Disconnect(int reconnectDelayMs, Stats& stats)
{
    if (_state == ClientState::CONNECTED)
        stats.connectedCount.fetch_sub(1);

    CloseSocket();
    _state               = ClientState::DISCONNECTED;
    _connectReadyMs      = NowMs() + reconnectDelayMs;
    _disconnectScheduled = false;
    if (_pendingCount > 0)
        stats.pendingPackets.fetch_sub(_pendingCount);
    ResetEchoState();
}

bool DummyClient::IsReadyToConnect() const
{
    return _state == ClientState::DISCONNECTED && NowMs() >= _connectReadyMs;
}

// ─────────────────────────────────────────────────────────────────
// 연결
// ─────────────────────────────────────────────────────────────────
void DummyClient::StartConnect(const std::string& ip, int port,
                               Stats& stats, int reconnectDelayMs)
{
    _sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (_sock == INVALID_SOCKET)
    {
        stats.connectFail.fetch_add(1);
        _connectReadyMs = NowMs() + reconnectDelayMs;
        return;
    }

    // 논블로킹 모드
    u_long mode = 1;
    ioctlsocket(_sock, FIONBIO, &mode);

    // 소켓 옵션
    int bufSize = 65536;
    setsockopt(_sock, SOL_SOCKET, SO_SNDBUF,    (char*)&bufSize,  static_cast<int>(sizeof(bufSize)));
    setsockopt(_sock, SOL_SOCKET, SO_RCVBUF,    (char*)&bufSize,  static_cast<int>(sizeof(bufSize)));
    int noDelay = 1;
    setsockopt(_sock, IPPROTO_TCP, TCP_NODELAY, (char*)&noDelay,  static_cast<int>(sizeof(noDelay)));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(static_cast<u_short>(port));
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

    int result = connect(_sock, reinterpret_cast<sockaddr*>(&addr),
                         static_cast<int>(sizeof(addr)));
    int err    = WSAGetLastError();

    if (result == 0 || err == WSAEWOULDBLOCK || err == WSAEINPROGRESS)
    {
        _state = ClientState::CONNECTING;
    }
    else
    {
        stats.connectFail.fetch_add(1);
        CloseSocket();
        _connectReadyMs = NowMs() + reconnectDelayMs;
    }
}

void DummyClient::OnConnected(Stats& stats, int reconnectDelayMs)
{
    // SO_ERROR 확인 (writable이어도 실패일 수 있음)
    int sockErr = 0;
    int optLen  = static_cast<int>(sizeof(sockErr));
    getsockopt(_sock, SOL_SOCKET, SO_ERROR, (char*)&sockErr, &optLen);

    if (sockErr != 0)
    {
        OnConnectFailed(stats, reconnectDelayMs);
        return;
    }

    _state = ClientState::CONNECTED;
    stats.connectedCount.fetch_add(1);
    stats.connectTotal.fetch_add(1);
    ResetEchoState();
}

void DummyClient::OnConnectFailed(Stats& stats, int reconnectDelayMs)
{
    stats.connectFail.fetch_add(1);
    CloseSocket();
    _state          = ClientState::DISCONNECTED;
    _connectReadyMs = NowMs() + reconnectDelayMs;
}

// ─────────────────────────────────────────────────────────────────
// 수신
// ─────────────────────────────────────────────────────────────────
void DummyClient::OnRecv(Stats& stats, int reconnectDelayMs)
{
    char buf[4096];
    int bytes = recv(_sock, buf, static_cast<int>(sizeof(buf)), 0);

    if (bytes > 0)
    {
        if (_recvBuf.Enqueue(buf, static_cast<size_t>(bytes)) == 0)
        {
            // 링버퍼 오버플로우 - 비정상 상황
            Disconnect(reconnectDelayMs, stats);
        }
        return;
    }

    if (bytes == 0)
    {
        // 서버가 먼저 연결을 끊음
        stats.disconnectFromServer.fetch_add(1);
    }
    else
    {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) return; // select 오탐, 정상
        stats.disconnectFromServer.fetch_add(1);
    }

    Disconnect(reconnectDelayMs, stats);
}

// ─────────────────────────────────────────────────────────────────
// 패킷 파싱
// ─────────────────────────────────────────────────────────────────
void DummyClient::ProcessPackets(Stats& stats, int reconnectDelayMs, int maxPacketSize)
{
    while (true)
    {
        if (_recvBuf.GetDataSize() < sizeof(MsgHeader)) break;

        MsgHeader hdr;
        if (_recvBuf.Peek(&hdr, sizeof(MsgHeader)) == 0) break;

        // hdr.size = 전체 크기 (헤더 포함)
        size_t totalSize = hdr.size;
        if (totalSize < ECHO_TOTAL_SIZE || totalSize > static_cast<size_t>(maxPacketSize))
        {
            // 예상치 못한 패킷 크기 - 즉시 연결 종료 (ResetEchoState에서 링버퍼 Clear)
            Disconnect(reconnectDelayMs, stats);
            return;
        }

        if (_recvBuf.GetDataSize() < totalSize) break;

        char packet[4096];
        if (_recvBuf.Dequeue(packet, totalSize) == 0) break;

        uint64_t recvVal;
        std::memcpy(&recvVal, packet + sizeof(MsgHeader), sizeof(uint64_t));

        int64_t now = NowMs();

        if (recvVal == _expectedRecv)
        {
            // RTT 계산 (deque는 송신 순서대로 유지됨, TCP 순서 보장)
            if (!_sendTimes.empty())
            {
                int64_t rtt = now - _sendTimes.front();
                _sendTimes.pop_front();
                if (rtt < 0) rtt = 0;

                stats.RecordRtt(rtt);
            }
            // payload 무결성 검증
            if (totalSize > ECHO_TOTAL_SIZE)
            {
                char expected[4096];
                size_t padLen = totalSize - ECHO_TOTAL_SIZE;
                FillDeterministicPadding(expected, padLen, recvVal);
                if (std::memcmp(packet + ECHO_TOTAL_SIZE, expected, padLen) != 0)
                    stats.packetError.fetch_add(1);
            }

            _expectedRecv++;
            if (_pendingCount > 0)
            {
                _pendingCount--;
                stats.pendingPackets.fetch_sub(1);
            }
        }
        else if (recvVal < _expectedRecv)
        {
            // 타임아웃 후 뒤늦게 도착한 패킷
            // sendTimes는 CheckTimeout에서 이미 pop됨 → 여기서 pop하면 안 됨
            // pendingCount도 CheckTimeout에서 이미 차감됨 → 건드리지 않음
            stats.lateArrival.fetch_add(1);
        }
        else
        {
            // recvVal > _expectedRecv: 예상 밖 값 → 진짜 패킷 에러
            stats.packetError.fetch_add(1);
            _expectedRecv = recvVal + 1;
            if (_pendingCount > 0)
            {
                _pendingCount--;
                stats.pendingPackets.fetch_sub(1);
            }
            if (!_sendTimes.empty()) _sendTimes.pop_front();
        }

        stats.recvCount.fetch_add(1);
    }
}

// ─────────────────────────────────────────────────────────────────
// 에코 송신 (송신 링버퍼에 enqueue)
// ─────────────────────────────────────────────────────────────────
void DummyClient::TrySend(int overSendCount, int minPacketSize, int maxPacketSize, int reconnectDelayMs, Stats& stats)
{
    if (!IsConnected()) return;

    static thread_local std::mt19937 rng{ std::random_device{}() };
    std::uniform_int_distribution<int> sizeDist(minPacketSize, maxPacketSize);

    while (_pendingCount < overSendCount)
    {
        _sentValue++;

        const int packetSize = sizeDist(rng);
        char packet[4096];
        MsgHeader hdr;
        hdr.size = static_cast<uint16_t>(packetSize);
        hdr.type = MsgType::ECHO;
        std::memcpy(packet,                     &hdr,        sizeof(MsgHeader));
        std::memcpy(packet + sizeof(MsgHeader), &_sentValue, sizeof(uint64_t));
        if (packetSize > ECHO_TOTAL_SIZE)
            FillDeterministicPadding(packet + ECHO_TOTAL_SIZE, packetSize - ECHO_TOTAL_SIZE, _sentValue);

        if (_sendBuf.Enqueue(packet, packetSize) == 0)
        {
            // 송신 버퍼 가득참 - 다음 루프에서 재시도
            _sentValue--;
            stats.sendBufferFull.fetch_add(1);
            return;
        }

        _pendingCount++;
        stats.pendingPackets.fetch_add(1);
        _sendTimes.push_back(NowMs());
        stats.sendCount.fetch_add(1);
    }
}

// ─────────────────────────────────────────────────────────────────
// 송신 링버퍼 flush (partial send 안전 처리)
// ─────────────────────────────────────────────────────────────────
void DummyClient::FlushSend(int reconnectDelayMs, Stats& stats)
{
    if (!IsConnected()) return;

    while (_sendBuf.GetDataSize() > 0)
    {
        size_t directSize = _sendBuf.GetDirectReadSize();
        if (directSize == 0) break;

        int sent = send(_sock, _sendBuf.GetReadPtr(), static_cast<int>(directSize), 0);

        if (sent == SOCKET_ERROR)
        {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK)
                return; // 다음 루프에서 재시도
            Disconnect(reconnectDelayMs, stats);
            return;
        }

        if (sent == 0) return;

        // partial이든 full이든 실제 전송된 만큼만 consume
        _sendBuf.Consume(static_cast<size_t>(sent));
    }
}

// ─────────────────────────────────────────────────────────────────
// 타임아웃 검사
// ─────────────────────────────────────────────────────────────────
void DummyClient::CheckTimeout(int echoTimeoutMs, Stats& stats)
{
    if (_sendTimes.empty()) return;

    int64_t now = NowMs();
    while (!_sendTimes.empty() && (now - _sendTimes.front()) >= echoTimeoutMs)
    {
        stats.echoNotRecv.fetch_add(1);
        _sendTimes.pop_front();
        _expectedRecv++;
        if (_pendingCount > 0)
        {
            _pendingCount--;
            stats.pendingPackets.fetch_sub(1);
        }
    }
}

// ─────────────────────────────────────────────────────────────────
// DisconnectTest
// ─────────────────────────────────────────────────────────────────
void DummyClient::ScheduleDisconnect(int reconnectIntervalMs)
{
    if (_disconnectScheduled) return;

    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(reconnectIntervalMs, reconnectIntervalMs * 5);
    _nextDisconMs        = NowMs() + dist(rng);
    _disconnectScheduled = true;
}

void DummyClient::CheckForcedDisconnect(int reconnectDelayMs, Stats& stats)
{
    if (!IsConnected() || !_disconnectScheduled) return;
    if (NowMs() < _nextDisconMs) return;

    // 강제 해제 (서버가 끊은 게 아니므로 DisconnectFromServer 증가 X)
    Disconnect(reconnectDelayMs, stats);
}
