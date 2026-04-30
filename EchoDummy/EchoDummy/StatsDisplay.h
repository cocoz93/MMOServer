#pragma once
#include <thread>
#include <atomic>
#include "Stats.h"
#include "Config.h"

class StatsDisplay
{
public:
    StatsDisplay(const Stats& stats, const Config& config, int totalClients);
    ~StatsDisplay();

    void Start();
    void Stop();

private:
    void StatsLoop();
    void PrintStats(int64_t elapsedSec, int64_t sendPPS, int64_t recvPPS) const;

    const Stats&  _stats;
    const Config& _config;
    int           _totalClients;

    std::thread       _statsThread;
    std::atomic<bool> _running{false};
    int64_t           _startTimeMs;
};
