#pragma once

// select()에서 8192개 소켓까지 처리 가능하도록 FD_SETSIZE 재정의
// 반드시 WinSock2.h include 전에 위치해야 함
#ifndef FD_SETSIZE
#define FD_SETSIZE 8192
#endif

#include <WinSock2.h>
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <string>
#include "DummyClient.h"
#include "MMOStressConfig.h"
#include "MMOStats.h"

class DummyManager
{
public:
    explicit DummyManager(const MMOStressConfig& config, MMOStats& stats);
    ~DummyManager();

    void Start();
    void Stop();

private:
    void NetworkLoop();

    const MMOStressConfig& _config;
    MMOStats&          _stats;

    std::vector<std::unique_ptr<DummyClient>> _clients;
    std::thread       _networkThread;
    std::atomic<bool> _running{false};

    // RampUp: 점진적 접속 제어
    int     _rampUpCount    = 0;        // 현재까지 접속 허용된 클라이언트 수
    int64_t _lastRampUpMs   = 0;        // 마지막 rampUp 시각
};
