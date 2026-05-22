#pragma once
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <string>
#include "DummyClient.h"
#include "Config.h"
#include "Stats.h"

class DummyManager
{
public:
    explicit DummyManager(const Config& config);
    ~DummyManager();

    void Start();
    void Stop();

    const Stats&  GetStats()  const { return _stats;  }
    const Config& GetConfig() const { return _config; }
    int GetTotalCount() const { return _config.clientCount; }

private:
    void NetworkLoop(int begin, int end);
    void DisplayLoop();

    Config  _config;
    Stats   _stats;

    std::vector<std::unique_ptr<DummyClient>> _clients;
    std::vector<std::thread> _threads;
    std::thread _displayThread;
    std::atomic<bool> _running{false};

    // Ramp-up: 점진 접속 제어 (전체 스레드 공유)
    std::atomic<int>  _rampUpCount{0};
    int64_t           _lastRampUpMs{0};
};
