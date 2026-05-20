#include "DummyClient.h"
#include <Windows.h>
#include <cstring>
#include <cstddef>  // offsetof
#include "MMOStats.h"
#include "MMOStressConfig.h"

// ─────────────────────────────────────────────────────────────────
// 유틸
// ─────────────────────────────────────────────────────────────────
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

void DummyClient::ResetState()
{
    _ready          = false;
    _playerId       = 0;
    _x              = 0.0f;
    _y              = 0.0f;
    _speed          = 0;
    _direction      = 0;
    _moving         = false;
    _lastTickMs      = 0;
    _lastHeartbeatMs = 0;
    _moveStartSentMs = 0;
    _lastRttMs       = -1;
    _recvBuf.Clear();
    _sendBuf.Clear();
}

void DummyClient::Disconnect(StatsLocal& stats, int reconnectDelayMs)
{
    if (_state == ClientState::CONNECTED)
    {
        stats.connectedDelta -= 1;
        if (_ready)
            stats.readyDelta -= 1;
    }

    CloseSocket();
    _state          = ClientState::DISCONNECTED;
    _connectReadyMs = NowMs() + reconnectDelayMs;
    ResetState();
}

bool DummyClient::IsReadyToConnect(int64_t nowMs) const
{
    return _state == ClientState::DISCONNECTED && nowMs >= _connectReadyMs;
}

// ─────────────────────────────────────────────────────────────────
// 연결
// ─────────────────────────────────────────────────────────────────
void DummyClient::StartConnect(const std::string& ip, int port,
                               StatsLocal& stats, int reconnectDelayMs)
{
    _sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (_sock == INVALID_SOCKET)
    {
        stats.connectFail += 1;
        _connectReadyMs = NowMs() + reconnectDelayMs;
        return;
    }

    // 논블로킹 모드
    u_long mode = 1;
    ioctlsocket(_sock, FIONBIO, &mode);

    // 소켓 옵션
    int bufSize = 65536;
    setsockopt(_sock, SOL_SOCKET, SO_SNDBUF,    (char*)&bufSize, static_cast<int>(sizeof(bufSize)));
    setsockopt(_sock, SOL_SOCKET, SO_RCVBUF,    (char*)&bufSize, static_cast<int>(sizeof(bufSize)));
    int noDelay = 1;
    setsockopt(_sock, IPPROTO_TCP, TCP_NODELAY, (char*)&noDelay, static_cast<int>(sizeof(noDelay)));

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
        stats.connectFail += 1;
        CloseSocket();
        _connectReadyMs = NowMs() + reconnectDelayMs;
    }
}

void DummyClient::OnConnected(StatsLocal& stats)
{
    // SO_ERROR 확인 (writable이어도 실패일 수 있음)
    int sockErr = 0;
    int optLen  = static_cast<int>(sizeof(sockErr));
    getsockopt(_sock, SOL_SOCKET, SO_ERROR, (char*)&sockErr, &optLen);

    if (sockErr != 0)
    {
        OnConnectFailed(stats, 1000);
        return;
    }

    _state = ClientState::CONNECTED;
    stats.connectedDelta += 1;
    stats.connectTotal += 1;
    ResetState();
}

void DummyClient::OnConnectFailed(StatsLocal& stats, int reconnectDelayMs)
{
    stats.connectFail += 1;
    CloseSocket();
    _state          = ClientState::DISCONNECTED;
    _connectReadyMs = NowMs() + reconnectDelayMs;
}

// ─────────────────────────────────────────────────────────────────
// 수신
// ─────────────────────────────────────────────────────────────────
void DummyClient::OnRecv(StatsLocal& stats, int reconnectDelayMs)
{
    char buf[4096];
    int bytes = recv(_sock, buf, static_cast<int>(sizeof(buf)), 0);

    if (bytes > 0)
    {
        stats.recvBytes += bytes;
        if (_recvBuf.Enqueue(buf, static_cast<size_t>(bytes)) == 0)
        {
            // 링버퍼 오버플로우
            Disconnect(stats, reconnectDelayMs);
        }
        return;
    }

    if (bytes == 0)
    {
        stats.disconnectFromServer += 1;
    }
    else
    {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) return;
        stats.disconnectFromServer += 1;
    }

    Disconnect(stats, reconnectDelayMs);
}

// ─────────────────────────────────────────────────────────────────
// 패킷 크기 테이블
// ─────────────────────────────────────────────────────────────────
uint16_t DummyClient::GetPacketSize(MsgType type)
{
    switch (type)
    {
    case MsgType::S2C_ZONE_INFO:           return sizeof(MSG_S2C_ZONE_INFO);
    case MsgType::S2C_CREATE_MY_PLAYER:    return sizeof(MSG_S2C_CREATE_MY_PLAYER);
    case MsgType::S2C_CREATE_OTHER_PLAYER: return sizeof(MSG_S2C_CREATE_OTHER_PLAYER);
    case MsgType::S2C_DELETE_PLAYER:       return sizeof(MSG_S2C_DELETE_PLAYER);
    case MsgType::S2C_MOVE_START:          return sizeof(MSG_S2C_MOVE_START);
    case MsgType::S2C_MOVE_STOP:           return sizeof(MSG_S2C_MOVE_STOP);
    case MsgType::S2C_CHAT:               return static_cast<uint16_t>(offsetof(MSG_S2C_CHAT, message) + sizeof(wchar_t)); // 가변 길이: 최소 크기
    case MsgType::S2C_SYNC_POSITION:      return sizeof(MSG_S2C_SYNC_POSITION);
    case MsgType::S2C_ZONE_CHANGE_OK:     return sizeof(MSG_S2C_ZONE_CHANGE_OK);
    case MsgType::S2C_ZONE_CHANGE_FAIL:   return sizeof(MSG_S2C_ZONE_CHANGE_FAIL);
    default: return 0;
    }
}

// ─────────────────────────────────────────────────────────────────
// 패킷 파싱
// ─────────────────────────────────────────────────────────────────
void DummyClient::ProcessPackets(StatsLocal& stats, const MMOStressConfig& config)
{
    while (true)
    {
        if (_recvBuf.GetDataSize() < sizeof(MsgHeader)) break;

        MsgHeader hdr;
        if (_recvBuf.Peek(&hdr, sizeof(MsgHeader)) == 0) break;

        uint16_t totalSize = hdr.size;

        // 알려진 패킷인지 확인 (최소 크기 검증 — 채팅 등 가변 길이 대응)
        uint16_t expected = GetPacketSize(hdr.type);
        if (expected == 0 || totalSize < expected)
        {
            // 알 수 없는 패킷 타입 또는 크기 부족 → 연결 종료
            Disconnect(stats, config.reconnectIntervalMs);
            return;
        }

        if (_recvBuf.GetDataSize() < totalSize) break;

        // 최대 패킷 크기 (MSG_S2C_CHAT 가변, 최대 ~1034B)
        char packet[2048];
        if (_recvBuf.Dequeue(packet, totalSize) == 0) break;

        stats.recvPackets += 1;

        switch (hdr.type)
        {
        case MsgType::S2C_CREATE_MY_PLAYER:
        {
            bool wasReady = _ready;
            HandleCreateMyPlayer(packet);
            if (_ready && !wasReady) stats.readyDelta += 1;
            break;
        }
        case MsgType::S2C_MOVE_START:
            HandleMoveStart(packet);
            if (_lastRttMs >= 0)
            {
                stats.RecordRtt(_lastRttMs);
                _lastRttMs = -1;
            }
            break;
        case MsgType::S2C_MOVE_STOP:           HandleMoveStop(packet);          break;
        case MsgType::S2C_SYNC_POSITION:       HandleSyncPosition(packet);      break;
        case MsgType::S2C_CREATE_OTHER_PLAYER: HandleCreateOtherPlayer(packet); break;
        case MsgType::S2C_DELETE_PLAYER:       HandleDeletePlayer(packet);      break;
        case MsgType::S2C_CHAT:                HandleChat(packet);              break;
        case MsgType::S2C_ZONE_CHANGE_OK:      HandleZoneChangeOk(packet);      break;
        case MsgType::S2C_ZONE_CHANGE_FAIL:
            HandleZoneChangeFail(packet);
            stats.zoneChangeFail += 1;
            break;
        case MsgType::S2C_ZONE_INFO:
            HandleZoneInfo(packet);
            break;
        default: break;
        }
    }
}

// ─────────────────────────────────────────────────────────────────
// 패킷 핸들러
// ─────────────────────────────────────────────────────────────────
void DummyClient::HandleZoneInfo(const char* packet)
{
    auto* msg = reinterpret_cast<const MSG_S2C_ZONE_INFO*>(packet);
    _mapWidth = msg->mapWidth;
    _mapHeight = msg->mapHeight;
}

void DummyClient::HandleCreateMyPlayer(const char* packet)
{
    auto* msg = reinterpret_cast<const MSG_S2C_CREATE_MY_PLAYER*>(packet);
    _playerId = msg->playerId;
    _x        = msg->x;
    _y        = msg->y;
    _speed    = msg->speed;
    _ready    = true;
}

void DummyClient::HandleMoveStart(const char* packet)
{
    auto* msg = reinterpret_cast<const MSG_S2C_MOVE_START*>(packet);

    // 자기 playerId와 매칭될 때만 RTT 측정
    if (msg->playerId == _playerId && _moveStartSentMs > 0)
    {
        int64_t rtt = NowMs() - _moveStartSentMs;
        if (rtt < 0) rtt = 0;
        _moveStartSentMs = 0;
        // stats는 ProcessPackets에서 접근 → 여기서는 임시 저장
        _lastRttMs = rtt;
    }
}

void DummyClient::HandleMoveStop(const char* /*packet*/)
{
    // 읽고 버림
}

void DummyClient::HandleSyncPosition(const char* packet)
{
    auto* msg = reinterpret_cast<const MSG_S2C_SYNC_POSITION*>(packet);
    _x = msg->x;
    _y = msg->y;
}

void DummyClient::HandleCreateOtherPlayer(const char* /*packet*/)
{
    // 읽고 버림
}

void DummyClient::HandleDeletePlayer(const char* /*packet*/)
{
    // 읽고 버림
}

void DummyClient::HandleChat(const char* /*packet*/)
{
    // 읽고 버림
}

void DummyClient::HandleZoneChangeOk(const char* packet)
{
    auto* msg = reinterpret_cast<const MSG_S2C_ZONE_CHANGE_OK*>(packet);
    _playerId = msg->playerId;
    _x        = msg->x;
    _y        = msg->y;
    _moving   = false;
}

void DummyClient::HandleZoneChangeFail(const char* packet)
{
    (void)packet;
    // zoneChangeFail 카운터는 ProcessPackets의 switch에서 증가
}

// ─────────────────────────────────────────────────────────────────
// 이동 로직
// ─────────────────────────────────────────────────────────────────
void DummyClient::UpdateLocalPosition(int mapWidth, int mapHeight)
{
    static constexpr float DELTA_TIME = 0.04f;  // 서버 FRAME_PER_SEC=25 → 1/25=0.04

    float dist = static_cast<float>(_speed) * DELTA_TIME;

    switch (_direction)
    {
    case 1: /* UP    */ _y -= dist; break;
    case 2: /* DOWN  */ _y += dist; break;
    case 3: /* LEFT  */ _x -= dist; break;
    case 4: /* RIGHT */ _x += dist; break;
    default: break;
    }

    // 맵 경계 클램핑 (서버 Zone::Tick과 동일)
    float maxX = static_cast<float>(mapWidth)  - 1.0f;
    float maxY = static_cast<float>(mapHeight) - 1.0f;
    if (_x < 0.0f) _x = 0.0f;
    if (_x >= static_cast<float>(mapWidth))  _x = maxX;
    if (_y < 0.0f) _y = 0.0f;
    if (_y >= static_cast<float>(mapHeight)) _y = maxY;
}

void DummyClient::SendMoveStart(StatsLocal& stats, int64_t nowMs)
{
    // 랜덤 방향 (1~4)
    std::uniform_int_distribution<int> dist(1, 4);
    _direction = static_cast<uint8_t>(dist(_rng));
    _moving = true;

    MSG_C2S_MOVE_START msg;
    msg.header.size = sizeof(msg);
    msg.header.type = MsgType::C2S_MOVE_START;
    msg.direction   = _direction;

    _sendBuf.Enqueue(&msg, sizeof(msg));
    _moveStartSentMs = nowMs;
    stats.sendPackets += 1;
    stats.moveStartSent += 1;
}

void DummyClient::SendMoveStop(StatsLocal& stats)
{
    _moving = false;

    MSG_C2S_MOVE_STOP msg;
    msg.header.size = sizeof(msg);
    msg.header.type = MsgType::C2S_MOVE_STOP;
    msg.direction   = _direction;
    msg.x           = _x;
    msg.y           = _y;

    _sendBuf.Enqueue(&msg, sizeof(msg));
    stats.sendPackets += 1;
    stats.moveStopSent += 1;
}

void DummyClient::SendHeartbeat(StatsLocal& stats)
{
    MSG_C2S_HEARTBEAT msg;
    msg.header.size = sizeof(msg);
    msg.header.type = MsgType::C2S_HEARTBEAT;

    _sendBuf.Enqueue(&msg, sizeof(msg));
    stats.sendPackets += 1;
    stats.heartbeatSent += 1;
}

void DummyClient::SendChat(StatsLocal& stats)
{
    // 랜덤 길이(1~511글자) 랜덤 문자열 생성
    std::uniform_int_distribution<int> lenDist(1, CHAT_MSG_MAX_LEN - 1);
    std::uniform_int_distribution<int> charDist(L'A', L'Z');

    int len = lenDist(_rng);

    MSG_C2S_CHAT msg{};
    msg.header.type = MsgType::C2S_CHAT;
    for (int i = 0; i < len; ++i)
        msg.message[i] = static_cast<wchar_t>(charDist(_rng));
    msg.message[len] = L'\0';

    uint16_t sendSize = static_cast<uint16_t>(
        sizeof(MsgHeader) + (len + 1) * sizeof(wchar_t));
    msg.header.size = sendSize;

    _sendBuf.Enqueue(&msg, sendSize);
    stats.sendPackets += 1;
    stats.chatSent += 1;
}

void DummyClient::SendZoneChange(StatsLocal& stats, int targetMapId)
{
    MSG_C2S_ZONE_CHANGE msg;
    msg.header.size  = sizeof(msg);
    msg.header.type  = MsgType::C2S_ZONE_CHANGE;
    msg.targetMapId  = targetMapId;

    _sendBuf.Enqueue(&msg, sizeof(msg));
    stats.sendPackets += 1;
    stats.zoneChangeSent += 1;
}

void DummyClient::Tick(StatsLocal& stats, const MMOStressConfig& config, int64_t nowMs)
{
    if (!_ready) return;

    // tickIntervalMs(40ms) 도달 체크
    if (nowMs - _lastTickMs < config.tickIntervalMs)
        return;
    _lastTickMs = nowMs;

    // 이동 중이면 좌표 갱신
    if (_moving)
        UpdateLocalPosition(_mapWidth, _mapHeight);

    // 랜덤 행동 결정
    std::uniform_int_distribution<int> roll(1, 100);
    int r = roll(_rng);

    if (_moving)
    {
        // 이동 중: stopProbability% 확률로 정지
        if (r <= config.stopProbability)
            SendMoveStop(stats);
    }
    else
    {
        // 정지 중: 확률 구간 분배
        int cursor = 0;

        cursor += config.moveProbability;
        if (r <= cursor)
        {
            SendMoveStart(stats, nowMs);
            return;
        }

        cursor += config.chatProbability;
        if (r <= cursor)
        {
            SendChat(stats);
            return;
        }

        cursor += config.zoneChangeProbability;
        if (r <= cursor)
        {
            SendZoneChange(stats, config.targetMapId);
            return;
        }

        // 나머지: idle
    }
}

void DummyClient::CheckHeartbeat(StatsLocal& stats, int heartbeatIntervalSec, int64_t nowMs)
{
    if (!_ready) return;

    if (nowMs - _lastHeartbeatMs >= static_cast<int64_t>(heartbeatIntervalSec) * 1000)
    {
        _lastHeartbeatMs = nowMs;
        SendHeartbeat(stats);
    }
}

// ─────────────────────────────────────────────────────────────────
// 송신 링버퍼 flush
// ─────────────────────────────────────────────────────────────────
void DummyClient::FlushSend(StatsLocal& stats, int reconnectDelayMs)
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
                return;
            Disconnect(stats, reconnectDelayMs);
            return;
        }

        if (sent == 0) return;

        stats.sendBytes += sent;
        _sendBuf.Consume(static_cast<size_t>(sent));
    }
}
