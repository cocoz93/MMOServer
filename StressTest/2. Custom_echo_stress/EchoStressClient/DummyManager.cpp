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
    const int rampUpMs = _config.rampUpIntervalMs;
    _rampUpCount.store((rampUpMs <= 0) ? total : 1, std::memory_order_relaxed);
    _lastRampUpMs = static_cast<int64_t>(GetTickCount64());

    int threadCount = (std::max)(1, (std::min)(4, total / 250));

    _threadStatCount = threadCount;

    for (int t = 0; t < threadCount; ++t)
    {
        int begin = t * (total / threadCount);
        int end   = (t + 1 == threadCount) ? total : (t + 1) * (total / threadCount);
        _threads.emplace_back(&DummyManager::NetworkLoop, this, begin, end, t);
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

    // 스레드 종료 후 모든 소켓 명시적 정리 (WSACleanup 이전에 수행되도록)
    for (auto& c : _clients)
        c->CloseSocket();
}

// ─────────────────────────────────────────────────────────────────
// Display Thread — 1초마다 콘솔에 상태 한 줄 출력
// ─────────────────────────────────────────────────────────────────
void DummyManager::DisplayLoop()
{
    const int total = _config.clientCount;
    int64_t prevSend       = 0;
    int64_t prevRecv       = 0;
    int64_t prevRttSum     = 0;
    int64_t prevRttSamples = 0;
    int     elapsed        = 0;

    while (_running)
    {
        // 1초 대기 (100ms 단위로 _running 체크)
        for (int i = 0; i < 10 && _running; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

        if (!_running) break;

        ++elapsed;

        MergedStats merged = MergeThreadStats(_threadStats, _threadStatCount);

        int64_t curSend = merged.sendCount;
        int64_t curRecv = merged.recvCount;
        int64_t sendTps = curSend - prevSend;
        int64_t recvTps = curRecv - prevRecv;
        prevSend = curSend;
        prevRecv = curRecv;

        int     conn          = merged.connectedCount;
        int64_t curRttSum     = merged.rttSumMs;
        int64_t curRttSamples = merged.rttSamples;
        int64_t deltaSum      = curRttSum - prevRttSum;
        int64_t deltaSamples  = curRttSamples - prevRttSamples;
        int64_t avgRtt        = (deltaSamples > 0) ? (deltaSum / deltaSamples) : 0;
        prevRttSum     = curRttSum;
        prevRttSamples = curRttSamples;
        int64_t errors   = merged.connectFail
                         + merged.disconnectFromServer
                         + merged.echoNotRecv
                         + merged.packetError;
        int64_t loopMs   = 0;
        for (int i = 0; i < merged.threadCount && i < MergedStats::MAX_THREADS; ++i)
            loopMs = (std::max)(loopMs, merged.loopDurationMs[i]);
        int     pending  = merged.pendingPackets;
        int64_t bufFull  = merged.sendBufferFull;

        int mm = elapsed / 60;
        int ss = elapsed % 60;

        wprintf(L"[%02d:%02d] Conn: %d/%d | Send: %lld/s | Recv: %lld/s | RTT: %lldms | Loop: %lldms | Pend: %d | BufFull: %lld | Err: %lld\n",
                mm, ss, conn, total, sendTps, recvTps, avgRtt, loopMs, pending, bufFull, errors);

        if (_config.attackMode > 0)
        {
            int64_t atkSent  = merged.attackPacketsSent;
            int64_t svrDisc  = merged.disconnectFromServer;
            wprintf(L"  [Attack] Mode: %d | AtkSent: %lld | SvrDisconnect: %lld\n",
                    _config.attackMode, atkSent, svrDisc);
        }
    }
}

// ─────────────────────────────────────────────────────────────────
// Network Thread
// ─────────────────────────────────────────────────────────────────
void DummyManager::NetworkLoop(int begin, int end, int threadIdx)
{
    const int   BATCH           = 64;  // FD_SETSIZE 기본값
    const auto& ip              = _config.serverIp;
    const int   port            = _config.port;
    const int   overSendCount   = _config.overSendCount;
    const int   loopDelayMs     = _config.loopDelayMs;
    const int   echoTimeoutMs   = _config.echoTimeoutMs;
    const int   reconnectDelay  = _config.reconnectIntervalMs;
    const bool  disconnectTest  = _config.disconnectTest;
    const int   minPacketSize      = _config.minPacketSize;
    const int   maxPacketSize      = _config.maxPacketSize;
    const int   attackMode         = _config.attackMode;
    const int   attackClientCount  = _config.attackClientCount;

    // Sleep(1)이 실제 1ms에 가깝게 동작하도록 타이머 해상도 설정
    timeBeginPeriod(1);

    const int   rampUpMs        = _config.rampUpIntervalMs;
    const int   total           = _config.clientCount;

    ThreadStats& myStats = _threadStats[threadIdx];

    while (_running)
    {
        int64_t loopStart = static_cast<int64_t>(GetTickCount64());

        // ── 0. Ramp-up 갱신 (첫 번째 스레드만 수행) ────────────────
        if (rampUpMs > 0 && begin == 0)
        {
            int64_t now = loopStart;
            int64_t elapsed = now - _lastRampUpMs;
            if (elapsed >= rampUpMs)
            {
                int add = static_cast<int>(elapsed / rampUpMs);
                int cur = _rampUpCount.load(std::memory_order_relaxed);
                int next = (std::min)(cur + add, total);
                _rampUpCount.store(next, std::memory_order_relaxed);
                _lastRampUpMs += static_cast<int64_t>(add) * rampUpMs;
            }
        }

        // ── 1. DISCONNECTED 클라이언트 접속 시도 ──────────────────
        int rampLimit = _rampUpCount.load(std::memory_order_relaxed);
        for (int i = begin; i < end; ++i)
        {
            if (i >= rampLimit)
                break;
            auto& c = *_clients[i];
            if (c.IsReadyToConnect())
                c.StartConnect(ip, port, myStats, reconnectDelay);
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
                    bool isAttacker = (attackMode > 0) &&
                                      (attackClientCount == 0 || i < attackClientCount);
                    if (!(attackMode == 4 && isAttacker))
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
                        c.OnConnectFailed(myStats, reconnectDelay);
                    }
                    else if (FD_ISSET(s, &writeSet))
                    {
                        c.OnConnected(myStats, reconnectDelay);

                        if (disconnectTest && c.IsConnected())
                            c.ScheduleDisconnect(reconnectDelay);
                    }
                }

                if (c.IsConnected() && FD_ISSET(s, &readSet))
                {
                    c.OnRecv(myStats, reconnectDelay);
                    if (c.IsConnected())
                        c.ProcessPackets(myStats, reconnectDelay, maxPacketSize);
                }
            }
        }

        // ── 3. 에코 송신 / flush / 타임아웃 / 강제해제 ──────────
        for (int i = begin; i < end; ++i)
        {
            auto& c = *_clients[i];
            if (!c.IsConnected()) continue;

            bool isAttacker = (attackMode > 0) &&
                              (attackClientCount == 0 || i < attackClientCount);

            if (!isAttacker)
            {
                // 기존 정상 에코 (변경 없음)
                c.TrySend(overSendCount, minPacketSize, maxPacketSize, reconnectDelay, myStats);
                c.FlushSend(reconnectDelay, myStats);
                c.CheckTimeout(echoTimeoutMs, myStats);
                if (disconnectTest)
                    c.CheckForcedDisconnect(reconnectDelay, myStats);
            }
            else
            {
                switch (attackMode)
                {
                case 1:  // 비정상 패킷 크기
                    c.SendAttackInvalidSize(myStats);
                    c.FlushSend(reconnectDelay, myStats);
                    break;

                case 2:  // 패킷 폭주 (유효 패킷, 제한 해제)
                    c.TrySend(INT_MAX, minPacketSize, maxPacketSize, reconnectDelay, myStats);
                    c.FlushSend(reconnectDelay, myStats);
                    c.CheckTimeout(echoTimeoutMs, myStats);
                    break;

                case 3:  // idle — 모두 스킵, 서버 타임아웃 대기
                    break;

                case 4:  // sendQ 압박 — 대량 송신, recv 안 함
                    c.TrySend(INT_MAX, minPacketSize, maxPacketSize, reconnectDelay, myStats);
                    c.FlushSend(reconnectDelay, myStats);
                    break;
                }
            }
        }

        // ── 4. 루프 시간 기록 + 딜레이 ──────────────────────────────
        int64_t loopDur = static_cast<int64_t>(GetTickCount64()) - loopStart;
        myStats.loopDurationMs = loopDur;
        Sleep(static_cast<DWORD>(loopDelayMs));
    }

    timeEndPeriod(1);
}
