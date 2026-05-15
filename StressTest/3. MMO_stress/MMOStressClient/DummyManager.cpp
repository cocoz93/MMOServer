#include "DummyManager.h"
#include <Windows.h>
#include <algorithm>
#include <timeapi.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winmm.lib")

DummyManager::DummyManager(const MMOStressConfig& config, MMOStats& stats)
    : _config(config)
    , _stats(stats)
{
    _clients.reserve(config.clientCount);
    for (int i = 0; i < config.clientCount; ++i)
        _clients.push_back(std::make_unique<DummyClient>());
}

DummyManager::~DummyManager()
{
    Stop();
}

void DummyManager::Start()
{
    _running = true;
    _networkThread = std::thread(&DummyManager::NetworkLoop, this);
}

void DummyManager::Stop()
{
    _running = false;
    if (_networkThread.joinable())
        _networkThread.join();
}

// ─────────────────────────────────────────────────────────────────
// Network Thread
// ─────────────────────────────────────────────────────────────────
void DummyManager::NetworkLoop()
{
    const int   BATCH          = 64;
    const int   total          = _config.clientCount;
    const auto& ip             = _config.serverIp;
    const int   port           = _config.port;
    const int   loopDelayMs    = _config.loopDelayMs;
    const int   reconnectDelay = _config.reconnectIntervalMs;

    // Sleep(1)이 실제 1ms에 가깝게 동작하도록 타이머 해상도 설정
    timeBeginPeriod(1);

    const int rampUpMs = _config.rampUpIntervalMs;

    // rampUp 비활성(0) → 전체 허용, 활성 → 1개부터 시작
    _rampUpCount  = (rampUpMs <= 0) ? total : 1;
    _lastRampUpMs = static_cast<int64_t>(GetTickCount64());

    while (_running)
    {
        // ── RampUp: 점진적 접속 허용 수 증가 ─────────────────────
        if (rampUpMs > 0 && _rampUpCount < total)
        {
            int64_t now = static_cast<int64_t>(GetTickCount64());
            if (now - _lastRampUpMs >= rampUpMs)
            {
                ++_rampUpCount;
                _lastRampUpMs = now;
            }
        }

        // ── 1. DISCONNECTED 클라이언트 접속 시도 ──────────────────
        for (int i = 0; i < _rampUpCount; ++i)
        {
            auto& c = *_clients[i];
            if (c.IsReadyToConnect())
                c.StartConnect(ip, port, _stats, reconnectDelay);
        }

        // ── 2. select() 배치 루프 (읽기/연결완료 감지) ───────────
        for (int base = 0; base < total; base += BATCH)
        {
            int end = (std::min)(base + BATCH, total);

            fd_set readSet, writeSet, exceptSet;
            FD_ZERO(&readSet);
            FD_ZERO(&writeSet);
            FD_ZERO(&exceptSet);
            bool hasAny = false;

            for (int i = base; i < end; ++i)
            {
                auto& c = *_clients[i];
                SOCKET s = c.GetSocket();
                if (s == INVALID_SOCKET) continue;

                if (c.IsConnected())
                {
                    FD_SET(s, &readSet);
                    hasAny = true;
                }
                else if (c.IsConnecting())
                {
                    FD_SET(s, &writeSet);
                    FD_SET(s, &exceptSet);
                    hasAny = true;
                }
            }

            if (!hasAny) continue;

            timeval tv;
            tv.tv_sec  = 0;
            tv.tv_usec = 0;
            select(0, &readSet, &writeSet, &exceptSet, &tv);

            for (int i = base; i < end; ++i)
            {
                auto& c = *_clients[i];
                SOCKET s = c.GetSocket();
                if (s == INVALID_SOCKET) continue;

                if (c.IsConnecting())
                {
                    if (FD_ISSET(s, &exceptSet))
                    {
                        c.OnConnectFailed(_stats, reconnectDelay);
                    }
                    else if (FD_ISSET(s, &writeSet))
                    {
                        c.OnConnected(_stats);
                    }
                }

                if (c.IsConnected() && FD_ISSET(s, &readSet))
                {
                    c.OnRecv(_stats, reconnectDelay);
                    if (c.IsConnected())
                        c.ProcessPackets(_stats, _config);
                }
            }
        }

        // ── 3. Tick + Heartbeat + FlushSend ──────────────────────
        for (int i = 0; i < total; ++i)
        {
            auto& c = *_clients[i];
            if (!c.IsConnected()) continue;

            c.Tick(_stats, _config);
            c.CheckHeartbeat(_stats, _config.heartbeatIntervalSec);
            c.FlushSend(_stats, reconnectDelay);
        }

        // ── 4. 루프 딜레이 ────────────────────────────────────────
        Sleep(static_cast<DWORD>(loopDelayMs));
    }

    timeEndPeriod(1);
}
