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

    MergedStats   GetMergedStats()   const { return MergeThreadStats(_threadStats, _threadStatCount); }
    const Config& GetConfig()        const { return _config; }
    int GetTotalCount() const { return _config.clientCount; }

private:
    void NetworkLoop(int begin, int end, int threadIdx);
    void DisplayLoop();

    Config  _config;
    ThreadStats _threadStats[MergedStats::MAX_THREADS];
    int         _threadStatCount = 0;

    std::vector<std::unique_ptr<DummyClient>> _clients;
    std::vector<std::thread> _threads;
    std::thread _displayThread;
    std::atomic<bool> _running{false};

    // Ramp-up: 점진 접속 제어 (전체 스레드 공유)
    std::atomic<int>  _rampUpCount{0};
    int64_t           _lastRampUpMs{0};
};
