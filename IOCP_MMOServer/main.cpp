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
    constexpr int PORT = 6000;
    constexpr int MAX_CLIENTS = 1000;

    std::cout << "=== IOCP MMO Server ===" << std::endl;
    std::cout << "Port: " << PORT << std::endl;
    std::cout << "Max Clients: " << MAX_CLIENTS << std::endl;

    CGameServer server;

    // 맵 설정
    MapConfig maps[] = {
        { 0, 200, 200, 20, 100 },  // 마을
        { 1, 400, 400, 40, 100 },  // 필드A
        { 2, 400, 400, 40, 100 },  // 필드B
    };

    // * 모드 전환: ServerMode::EchoTest / ServerMode::GameServer
    if (!server.Init(ServerMode::EchoTest, PORT, MAX_CLIENTS, maps, 3))
    {
        std::cerr << "[Error] Server Init failed" << std::endl;
        return 1;
    }

    if (!server.Start())
    {
        std::cerr << "[Error] Server Start failed" << std::endl;
        return 1;
    }

    // main 스레드는 condition_variable로 대기
    {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&] { return !running; });
    }

    server.Stop();

    std::cout << "Server shutdown complete" << std::endl;
    return 0;
}
