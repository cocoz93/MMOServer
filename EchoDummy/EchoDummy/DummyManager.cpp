#include "DummyManager.h"
#include <Windows.h>
#include <algorithm>

#pragma comment(lib, "ws2_32.lib")

DummyManager::DummyManager(const Config& config)
    : _config(config)
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
    const int   BATCH           = 64;  // FD_SETSIZE 기본값
    const int   total           = _config.clientCount;
    const auto& ip              = _config.serverIp;
    const int   port            = _config.port;
    const int   overSendCount   = _config.overSendCount;
    const int   loopDelayMs     = _config.loopDelayMs;
    const int   echoTimeoutMs   = _config.echoTimeoutMs;
    const int   reconnectDelay  = _config.reconnectIntervalMs;
    const bool  disconnectTest  = _config.disconnectTest;

    while (_running)
    {
        // ── 1. DISCONNECTED 클라이언트 접속 시도 ──────────────────
        for (int i = 0; i < total; ++i)
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

                        if (disconnectTest && c.IsConnected())
                            c.ScheduleDisconnect(reconnectDelay);
                    }
                }

                if (c.IsConnected() && FD_ISSET(s, &readSet))
                {
                    c.OnRecv(_stats, reconnectDelay);
                    if (c.IsConnected())
                        c.ProcessPackets(_stats, reconnectDelay);
                }
            }
        }

        // ── 3. 에코 송신 / flush / 타임아웃 / 강제해제 ──────────
        for (int i = 0; i < total; ++i)
        {
            auto& c = *_clients[i];
            if (!c.IsConnected()) continue;

            c.TrySend(overSendCount, reconnectDelay, _stats);
            c.FlushSend(reconnectDelay, _stats);
            c.CheckTimeout(echoTimeoutMs, _stats);

            if (disconnectTest)
                c.CheckForcedDisconnect(reconnectDelay, _stats);
        }

        // ── 4. 루프 딜레이 ────────────────────────────────────────
        Sleep(static_cast<DWORD>(loopDelayMs));
    }
}
