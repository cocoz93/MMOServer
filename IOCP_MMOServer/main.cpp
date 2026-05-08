//
#include <iostream>
#include <memory>
#include <atomic>
#include <thread>
#include <chrono>
#include <condition_variable>
#include <mutex>

#include "CentralizedServer.h"

std::atomic<bool> running{true};
std::mutex mtx;
std::condition_variable cv;

// TODO : 이걸 이렇게 빼두는게 맞을까
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

    std::cout << "=== IOCP Mini Game Server ===" << std::endl;
    std::cout << "Port: " << PORT << std::endl; 
    std::cout << "Max Clients: " << MAX_CLIENTS << std::endl;

    // 중앙 집중형 게임 서버만 생성 (내부에서 네트워크 레이어 자동 생성)
    auto gameServer = std::make_unique<CIOCPServer>(PORT, MAX_CLIENTS, ServerArchitectureType::GameCodiEchoTest);

    // 서버 시작
    gameServer->Start();

    // main 스레드는 condition_variable로 대기
    {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&] { return !running; });
    }

    // 서버 종료
    gameServer->ShutdownServer();

    std::cout << "Server shutdown complete" << std::endl;
    return 0;
}
