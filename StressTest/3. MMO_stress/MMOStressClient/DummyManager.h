#pragma once
#include "WinSockDef.h"
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
    DummyManager(const MMOStressConfig& config, MMOStats& stats, int clientCount);
    ~DummyManager();

    void Start();
    void Stop();

private:
    void NetworkLoop();

    const MMOStressConfig& _config;
    MMOStats&          _stats;
    const int          _clientCount;

    std::vector<std::unique_ptr<DummyClient>> _clients;
    std::thread       _networkThread;
    std::atomic<bool> _running{false};

    // RampUp: 점진적 접속 제어
    int     _rampUpCount    = 0;        // 현재까지 접속 허용된 클라이언트 수
    int64_t _lastRampUpMs   = 0;        // 마지막 rampUp 시각
};
