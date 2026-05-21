#include "DummyManager.h"
#include <Windows.h>
#include <algorithm>
#include <timeapi.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winmm.lib")

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

    const int total = _config.clientCount;
    int threadCount = (std::max)(1, (std::min)(4, total / 250));

    for (int t = 0; t < threadCount; ++t)
    {
        int begin = t * (total / threadCount);
        int end   = (t + 1 == threadCount) ? total : (t + 1) * (total / threadCount);
        _threads.emplace_back(&DummyManager::NetworkLoop, this, begin, end);
    }

    _displayThread = std::thread(&DummyManager::DisplayLoop, this);

    wprintf(L"[DummyManager] %d network threads started\n", threadCount);
}

void DummyManager::Stop()
{
    _running = false;
    if (_displayThread.joinable())
        _displayThread.join();
    for (auto& t : _threads)
    {
        if (t.joinable())
            t.join();
    }
    _threads.clear();
}

// ─────────────────────────────────────────────────────────────────
// Display Thread — 1초마다 콘솔에 상태 한 줄 출력
// ─────────────────────────────────────────────────────────────────
void DummyManager::DisplayLoop()
{
    const int total = _config.clientCount;
    int64_t prevSend = 0;
    int64_t prevRecv = 0;
    int     elapsed  = 0;

    while (_running)
    {
        // 1초 대기 (100ms 단위로 _running 체크)
        for (int i = 0; i < 10 && _running; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

        if (!_running) break;

        ++elapsed;

        int64_t curSend = _stats.sendCount.load();
        int64_t curRecv = _stats.recvCount.load();
        int64_t sendTps = curSend - prevSend;
        int64_t recvTps = curRecv - prevRecv;
        prevSend = curSend;
        prevRecv = curRecv;

        int     conn     = _stats.connectedCount.load();
        int64_t samples  = _stats.rttSamples.load();
        int64_t avgRtt   = (samples > 0) ? (_stats.rttSumMs.load() / samples) : 0;
        int64_t errors   = _stats.connectFail.load()
                         + _stats.disconnectFromServer.load()
                         + _stats.echoNotRecv.load()
                         + _stats.packetError.load();
        int64_t loopMs   = _stats.loopDurationMs.load();
        int     pending  = _stats.pendingPackets.load();
        int64_t bufFull  = _stats.sendBufferFull.load();

        int mm = elapsed / 60;
        int ss = elapsed % 60;

        wprintf(L"[%02d:%02d] Conn: %d/%d | Send: %lld/s | Recv: %lld/s | RTT: %lldms | Loop: %lldms | Pend: %d | BufFull: %lld | Err: %lld\n",
                mm, ss, conn, total, sendTps, recvTps, avgRtt, loopMs, pending, bufFull, errors);
    }
}

// ─────────────────────────────────────────────────────────────────
// Network Thread
// ─────────────────────────────────────────────────────────────────
void DummyManager::NetworkLoop(int begin, int end)
{
    const int   BATCH           = 64;  // FD_SETSIZE 기본값
    const auto& ip              = _config.serverIp;
    const int   port            = _config.port;
    const int   overSendCount   = _config.overSendCount;
    const int   loopDelayMs     = _config.loopDelayMs;
    const int   echoTimeoutMs   = _config.echoTimeoutMs;
    const int   reconnectDelay  = _config.reconnectIntervalMs;
    const bool  disconnectTest  = _config.disconnectTest;
    const int   minPacketSize  = _config.minPacketSize;
    const int   maxPacketSize  = _config.maxPacketSize;

    // Sleep(1)이 실제 1ms에 가깝게 동작하도록 타이머 해상도 설정
    timeBeginPeriod(1);

    while (_running)
    {
        int64_t loopStart = static_cast<int64_t>(GetTickCount64());

        // ── 1. DISCONNECTED 클라이언트 접속 시도 ──────────────────
        for (int i = begin; i < end; ++i)
        {
            auto& c = *_clients[i];
            if (c.IsReadyToConnect())
                c.StartConnect(ip, port, _stats, reconnectDelay);
        }

        // ── 2. select() 배치 루프 (읽기/연결완료 감지) ───────────
        for (int base = begin; base < end; base += BATCH)
        {
            int batchEnd = (std::min)(base + BATCH, end);

            fd_set readSet, writeSet, exceptSet;
            FD_ZERO(&readSet);
            FD_ZERO(&writeSet);
            FD_ZERO(&exceptSet);
            bool hasAny = false;

            for (int i = base; i < batchEnd; ++i)
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

            for (int i = base; i < batchEnd; ++i)
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
                        c.OnConnected(_stats, reconnectDelay);

                        if (disconnectTest && c.IsConnected())
                            c.ScheduleDisconnect(reconnectDelay);
                    }
                }

                if (c.IsConnected() && FD_ISSET(s, &readSet))
                {
                    c.OnRecv(_stats, reconnectDelay);
                    if (c.IsConnected())
                        c.ProcessPackets(_stats, reconnectDelay, maxPacketSize);
                }
            }
        }

        // ── 3. 에코 송신 / flush / 타임아웃 / 강제해제 ──────────
        for (int i = begin; i < end; ++i)
        {
            auto& c = *_clients[i];
            if (!c.IsConnected()) continue;

            c.TrySend(overSendCount, minPacketSize, maxPacketSize, reconnectDelay, _stats);
            c.FlushSend(reconnectDelay, _stats);
            c.CheckTimeout(echoTimeoutMs, _stats);

            if (disconnectTest)
                c.CheckForcedDisconnect(reconnectDelay, _stats);
        }

        // ── 4. 루프 시간 기록 + 딜레이 ──────────────────────────────
        int64_t loopDur = static_cast<int64_t>(GetTickCount64()) - loopStart;
        _stats.loopDurationMs.store(loopDur);
        Sleep(static_cast<DWORD>(loopDelayMs));
    }

    timeEndPeriod(1);
}
