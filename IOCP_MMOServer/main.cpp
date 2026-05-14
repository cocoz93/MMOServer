//
#include <iostream>
#include <memory>
#include <atomic>
#include <thread>
#include <chrono>
#include <condition_variable>
#include <mutex>

#include "GameServer.h"
#include "ZoneManager.h"
#include "MonitorManager.h"
#include "MonitorServer.h"
#include "ServerConfig.h"

std::atomic<bool> running{true};
std::mutex mtx;
std::condition_variable cv;

// 프로세스 전체 종료 컨트롤러이므로 메인문에 빼둔다
void SignalProcessShutdown()
{
    running = false;
    cv.notify_one();
}

int main()
{
    // INI 설정 파일 로드
    ServerConfig config;
    config.Load();

    std::cout << "=== IOCP MMO Server ===" << std::endl;
    std::cout << "Port: " << config.port << std::endl;
    std::cout << "Max Clients: " << config.maxClients << std::endl;

    CMonitorManager monitor;
    CMonitorServer monitorSvr(monitor, config.monitorPort);
    CGameServer server(monitor);

    if (!server.Init(config.mode, config.port, config.maxClients,
                     config.maps.data(), static_cast<int32_t>(config.maps.size())))
    {
        std::cerr << "[Error] Server Init failed" << std::endl;
        return 1;
    }

    if (!server.Start())
    {
        std::cerr << "[Error] Server Start failed" << std::endl;
        return 1;
    }

    monitorSvr.Start();

    // main 스레드는 condition_variable로 대기
    {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&] { return !running; });
    }

    monitorSvr.Stop();
    server.Stop();

    std::cout << "Server shutdown complete" << std::endl;
    return 0;
}
