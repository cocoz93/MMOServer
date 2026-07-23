#pragma once
// ==========================================================================
// EpollEchoServer — 리눅스 epoll 기반 에코 서버 (PoC, 헤더온리)
//
//   목적: 크로스플랫폼 포팅 Phase 2의 첫 관문 확인용 —
//         (1) CMake 리눅스 타깃/툴체인이 실제로 도는지,
//         (2) epoll 이벤트 루프(준비 통지 모델)를 직접 짤 수 있는지.
//   범위: 게임 로직·LockFree·SerialBuffer 미연결. 순수 소켓 에코만.
//         (IOCP=완료 통지 ↔ epoll=준비 통지 의 구조 대비 확인)
//   NOTE: 리눅스 전용. CMake if(UNIX) 분기에서만 빌드된다.
// ==========================================================================
#ifdef __linux__

#include <cstdint>
#include <vector>
#include <atomic>
#include <cstdio>

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>

class EpollEchoServer
{
public:
    bool Init(uint16_t port);          // 리슨 소켓 + epoll 준비
    void Run();                        // 이벤트 루프 (Stop 전까지 블록)
    void Stop() { _running.store(false); }
    ~EpollEchoServer() { Cleanup(); }

private:
    static bool SetNonBlocking(int fd);
    void OnAcceptable();               // 리슨 준비 → 대기 연결 모두 수락
    void OnReadable(int fd);           // 클라 준비 → EAGAIN까지 읽고 그대로 반향
    void CloseClient(int fd);
    void Cleanup();

    int _listenFd = -1;
    int _epollFd  = -1;
    std::atomic<bool> _running{ false };
    static constexpr int kMaxEvents = 256;
};

// ── 구현 ───────────────────────────────────────────────────────────
inline bool EpollEchoServer::SetNonBlocking(int fd)
{
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

inline bool EpollEchoServer::Init(uint16_t port)
{
    _listenFd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (_listenFd < 0) { ::perror("socket"); return false; }

    int on = 1;
    ::setsockopt(_listenFd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(port);
    if (::bind(_listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
    { ::perror("bind"); return false; }

    if (!SetNonBlocking(_listenFd))       { ::perror("fcntl");  return false; }
    if (::listen(_listenFd, SOMAXCONN) < 0) { ::perror("listen"); return false; }

    _epollFd = ::epoll_create1(0);
    if (_epollFd < 0) { ::perror("epoll_create1"); return false; }

    epoll_event ev{};
    ev.events  = EPOLLIN;                 // 레벨 트리거(기본) — PoC 단순화
    ev.data.fd = _listenFd;
    if (::epoll_ctl(_epollFd, EPOLL_CTL_ADD, _listenFd, &ev) < 0)
    { ::perror("epoll_ctl:listen"); return false; }

    return true;
}

inline void EpollEchoServer::Run()
{
    _running.store(true);
    std::vector<epoll_event> events(kMaxEvents);

    while (_running.load())
    {
        int n = ::epoll_wait(_epollFd, events.data(), kMaxEvents, 1000 /*ms*/);
        if (n < 0)
        {
            if (errno == EINTR) continue; // 시그널 인터럽트 → 재시도
            ::perror("epoll_wait");
            break;
        }
        for (int i = 0; i < n; ++i)
        {
            const int      fd = events[i].data.fd;
            const uint32_t e  = events[i].events;

            if (e & (EPOLLHUP | EPOLLERR)) { CloseClient(fd); continue; }
            if (fd == _listenFd)           OnAcceptable();
            else if (e & EPOLLIN)          OnReadable(fd);
        }
    }
}

inline void EpollEchoServer::OnAcceptable()
{
    // 레벨 트리거라도 대기 연결을 한 번에 비워 깨어나는 횟수를 줄인다.
    for (;;)
    {
        int cfd = ::accept(_listenFd, nullptr, nullptr);
        if (cfd < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;  // 더 없음
            if (errno == EINTR) continue;
            ::perror("accept");
            break;
        }
        if (!SetNonBlocking(cfd)) { ::close(cfd); continue; }

        epoll_event ev{};
        ev.events  = EPOLLIN;
        ev.data.fd = cfd;
        if (::epoll_ctl(_epollFd, EPOLL_CTL_ADD, cfd, &ev) < 0)
        { ::perror("epoll_ctl:accept"); ::close(cfd); }
    }
}

inline void EpollEchoServer::OnReadable(int fd)
{
    char buf[4096];
    for (;;)
    {
        ssize_t r = ::recv(fd, buf, sizeof(buf), 0);
        if (r > 0)
        {
            ssize_t off = 0;                 // 그대로 반향 (부분 송신만 처리)
            while (off < r)
            {
                ssize_t w = ::send(fd, buf + off, r - off, MSG_NOSIGNAL);
                if (w < 0)
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                    if (errno == EINTR) continue;
                    CloseClient(fd); return;
                }
                off += w;
            }
        }
        else if (r == 0) { CloseClient(fd); return; }        // 상대 종료
        else
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return; // 다 읽음
            if (errno == EINTR) continue;
            CloseClient(fd); return;
        }
    }
}

inline void EpollEchoServer::CloseClient(int fd)
{
    ::epoll_ctl(_epollFd, EPOLL_CTL_DEL, fd, nullptr);
    ::close(fd);
}

inline void EpollEchoServer::Cleanup()
{
    if (_epollFd  >= 0) { ::close(_epollFd);  _epollFd  = -1; }
    if (_listenFd >= 0) { ::close(_listenFd); _listenFd = -1; }
}

#endif // __linux__