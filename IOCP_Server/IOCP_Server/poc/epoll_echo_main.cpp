// ==========================================================================
// epoll 에코 PoC 진입점 — Phase 2 리눅스 툴체인/이벤트 루프 검증용
//   빌드: CMake if(UNIX) 분기 → epoll_echo_poc
//   실행: ./epoll_echo_poc [port]        (기본 9000)
//   확인: 다른 터미널에서  nc 127.0.0.1 9000  → 입력이 그대로 돌아오면 성공
// ==========================================================================
#ifdef __linux__

#include "EpollEchoServer.h"
#include <csignal>
#include <cstdlib>
#include <cstdio>
#include <cstdint>

static EpollEchoServer* g_server = nullptr;
static void OnSignal(int) { if (g_server) g_server->Stop(); }

int main(int argc, char** argv)
{
    uint16_t port = (argc > 1) ? static_cast<uint16_t>(std::atoi(argv[1])) : 9000;

    EpollEchoServer server;
    g_server = &server;
    std::signal(SIGINT,  OnSignal);
    std::signal(SIGTERM, OnSignal);

    if (!server.Init(port))
    {
        std::fprintf(stderr, "[epoll-poc] init failed\n");
        return 1;
    }
    std::printf("[epoll-poc] listening on :%u (Ctrl+C to stop)\n", port);
    server.Run();
    std::printf("[epoll-poc] shutdown\n");
    return 0;
}

#else
int main() { return 0; }   // 비리눅스: CMake가 빌드하지 않지만 안전망
#endif