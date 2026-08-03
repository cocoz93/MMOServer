//
// epoll 전송 계층 — 리눅스 백엔드. 공통 골격(IOCPServer.cpp)이 Transport* 위임 지점으로만 호출한다.
//   Windows 빌드에서는 파일 전체가 비활성 — 빈 번역단위가 된다.
//
//   [IOCP와 무엇이 다른가]
//     IOCP : "이 버퍼로 보내/받아 달라"고 걸어두면 끝났을 때 완료를 돌려준다 (proactor).
//     epoll: "이 fd가 읽을/쓸 수 있게 되면 알려 달라"고 등록하면 준비 상태를 알려준다 (reactor).
//   그래서 완료 통지에 실려오던 OVERLAPPED·전송 바이트 수가 없고, 준비 통지를 받은 쪽이
//   직접 read/write를 호출해 실제 크기를 그 자리에서 얻는다.
//
//   [현재 범위 — 4-D 뼈대]
//     연결 수락·등록·해제까지. 수신 경로는 4-F, 송신 경로(EPOLLOUT 재설계)는 4-G에서 채운다.
//
#include "IOCPServer.h"

#ifndef _WIN32

#include "../../Shared/Common/ErrorLog.h"
#include "CoreAffinity.h"

#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <chrono>
#include <algorithm>

namespace
{
    // epoll_wait 한 번에 회수할 이벤트 상한 — 너무 크면 한 워커가 오래 붙잡고,
    //   너무 작으면 syscall 횟수가 는다. IOCP의 완료 배치 상한과 같은 성격의 값.
    constexpr int EPOLL_EVENT_BATCH = 256;

    // epoll_wait 타임아웃(ms) — 정지 플래그를 확인할 주기다.
    //   무한 대기로 두면 종료 시 워커가 안 깨어난다(IOCP는 PostQueuedCompletionStatus로 깨웠다).
    constexpr int EPOLL_WAIT_TIMEOUT_MS = 100;

    // 논블로킹 전환 — reactor 모델에서는 필수다. 준비 통지를 받고 read를 불러도
    //   그 사이 다른 워커가 먼저 가져갔으면 블로킹될 수 있다.
    bool SetNonBlocking(int fd)
    {
        const int flags = ::fcntl(fd, F_GETFL, 0);
        if (flags < 0)
            return false;
        return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
    }
}

// ============================================================================
// 전송 계층 경계 — 공통 골격이 위임하는 지점 (선언은 IOCPServer.h)
// ============================================================================

// 세션 링버퍼 준비 — epoll은 세션마다 자기 힙 버퍼를 갖는다 (IOCP 팔과 동일).
bool CIOCPServer::TransportInitSessionBuffer(CSession* session)
{
#if USE_LOCKFREE_SENDQ
    return session->_recvQ.Init() && session->_sendQ.Init(256);
#else
    return session->_recvQ.Init() && session->_sendQ.Init();
#endif
}

// 리슨 소켓 "전" 준비 — epoll 인스턴스를 만든다. accept 소켓을 여기에 등록한다.
bool CIOCPServer::TransportPreListen()
{
    // 워커 수 산정 — affinity 가용 코어 수가 기준인 것은 IOCP 팔과 같다.
    //   다만 epoll에는 "동시 실행 워커 상한(concurrency)" 개념이 없다. 커널이 완료를
    //   깨우는 수를 조절해 주던 IOCP와 달리, 여기서는 등록한 워커 전부가 자유롭게 돈다.
    _coreCount = Platform::GetAvailableCoreCount();
    _workerThreadCount = (_configuredWorkers > 0) ? _configuredWorkers : _coreCount;

    _epollFd = ::epoll_create1(EPOLL_CLOEXEC);
    return (_epollFd >= 0);
}

void CIOCPServer::TransportPreListenCleanup()
{
    if (_epollFd >= 0)
    {
        ::close(_epollFd);
        _epollFd = -1;
    }
}

// 리슨 소켓 "후" 준비 — epoll은 할 일이 없다 (RIO만 함수 테이블·슬랩·CQ가 필요).
bool CIOCPServer::TransportPostListen()
{
    return true;
}

// 소켓 생성 플래그 — 리눅스에는 WSASocket의 플래그 개념이 없다(골격이 값을 버린다).
uint32_t CIOCPServer::TransportListenFlags() const
{
    return 0;
}

// 워커 기동 — epoll_wait 루프를 도는 스레드들.
void CIOCPServer::TransportStartWorkers()
{
    for (int i = 0; i < _workerThreadCount; ++i)
        _workerThreads.emplace_back(&CIOCPServer::EpollWorkerThread, this, i);
}

// 기동 완료 로그 — 스모크가 이 문구로 팔(arm)을 판정한다.
void CIOCPServer::TransportLogStarted(const char* modeName) const
{
    SLOG_INFO("[Network] Server started — epoll workers={} (affinity cores={}, Mode: {})",
              _workerThreadCount, _coreCount, modeName);
}

// IOCount 드레인 "전" 정지 — 4-G에서 송신 워커가 생기면 여기서 멈춘다.
void CIOCPServer::TransportStopBeforeDrain()
{
}

// IOCount 드레인 "후" 정지 — 워커 종료·epoll 해제.
//   IOCP는 PostQueuedCompletionStatus로 워커를 깨웠지만, epoll에는 그런 "가짜 완료"가 없다.
//   대신 epoll_wait에 타임아웃을 줘 주기적으로 _running을 확인하게 했다.
void CIOCPServer::TransportStopAfterDrain()
{
    for (auto& thread : _workerThreads)
    {
        if (thread.joinable())
            thread.join();
    }
    _workerThreads.clear();

    if (_epollFd >= 0)
    {
        ::close(_epollFd);
        _epollFd = -1;
    }
}

// 통지 연결 — accept한 소켓을 epoll에 등록한다.
//   IOCP의 BindIOCP에 대응하지만, 여기서는 "무엇을 알려줄지"(EPOLLIN)까지 같이 정한다.
bool CIOCPServer::TransportAttachSession(CSession* session, Platform::NetSocket clientSocket)
{
    if (!SetNonBlocking(clientSocket))
    {
        SLOG_ERROR("[Network] SetNonBlocking failed: {}", Platform::LastSocketError());
        return false;
    }

    epoll_event ev{};
    ev.events   = EPOLLIN | EPOLLRDHUP;   // 읽기 준비 + 상대 종료
    ev.data.ptr = session;                // 통지에서 세션을 바로 찾는다 (IOCP의 CompletionKey와 같은 역할)

    if (::epoll_ctl(_epollFd, EPOLL_CTL_ADD, clientSocket, &ev) != 0)
    {
        SLOG_ERROR("[Network] epoll_ctl(ADD) failed: {}", Platform::LastSocketError());
        return false;
    }
    return true;
}

// 첫 Recv 착수 — epoll에는 "수신을 걸어둔다"는 개념이 없다.
//   TransportAttachSession의 EPOLLIN 등록이 그 자리를 대신하므로 여기서 할 일이 없다.
void CIOCPServer::TransportStartFirstRecv(CSession* session, Platform::NetSocket clientSocket, int64_t sessionId)
{
    (void)session;
    (void)clientSocket;
    (void)sessionId;
}

// SendFlush::Immediate — 4-G에서 채운다.
void CIOCPServer::TransportSendImmediate(CSession* session, int64_t sessionId)
{
    (void)session;
    (void)sessionId;
}

// 틱 끝 dirty 배치 → 송신 — 4-G에서 채운다.
void CIOCPServer::TransportFlushDirty()
{
    _dirtySessions.clear();
}

// 종료 유도 — IOCP는 CancelIoEx로 걸린 I/O를 취소했지만, epoll에는 취소할 "걸린 I/O"가 없다.
//   등록을 빼고 shutdown으로 상대에게 알리면, 남은 통지는 워커가 정리한다.
bool CIOCPServer::TransportRequestDisconnect(CSession* session)
{
    // 다른 스레드가 이미 처리 중이면 여기서 멈춘다 (IOCP 팔과 같은 게이트).
    if (InterlockedExchange(&session->_disconnecting, TRUE) == TRUE)
        return false;

    const Platform::NetSocket sock = session->_socket;
    if (sock != Platform::kInvalidSocket)
    {
        ::epoll_ctl(_epollFd, EPOLL_CTL_DEL, sock, nullptr);   // 더 이상 통지받지 않는다
        ::shutdown(sock, SHUT_RDWR);                           // 상대에게 종료를 알린다
    }

    // [IOCP와 결정적으로 다른 곳]
    //   IOCP는 CancelIoEx가 걸려 있던 I/O를 실패로 완료시키고, 그 완료를 받은 워커가
    //   IOCountDecrement를 불러 IOCount를 0으로 수렴시킨다 — 즉 "완료 통지"가 ref를 놓는다.
    //   epoll에는 걸린 I/O가 없어 그런 통지가 영영 오지 않는다. 세션을 만들 때 세운
    //   IOCount=1은 여기서는 "epoll에 등록되어 있다"는 뜻이므로, 등록을 빼는 이 자리에서
    //   직접 놓아 준다. 위의 _disconnecting 게이트가 이 경로를 한 번만 통과시킨다.
    IOCountDecrement(session);
    return true;
}

// ============================================================================
// epoll 워커 — 준비 통지를 받아 처리한다 (IOCP의 WorkerThread에 대응)
// ============================================================================
void CIOCPServer::EpollWorkerThread(int workerIndex)
{
    CoreAffinity::PinIoThread();   // I/O 스레드 → 게임코어 밖으로 (격리 off면 no-op)

    const int monitorIndex = _monitor.RegisterWorkerThread();
    if (monitorIndex >= 0 && monitorIndex < CMonitorManager::MAX_WORKER_THREADS)
        _monitor._workerCounters[monitorIndex].threadHandle = Platform::CaptureCurrentThreadCpu();

    epoll_event events[EPOLL_EVENT_BATCH];

    while (_running == TRUE)
    {
        const int n = ::epoll_wait(_epollFd, events, EPOLL_EVENT_BATCH, EPOLL_WAIT_TIMEOUT_MS);
        if (n < 0)
        {
            if (Platform::LastSocketError() == EINTR)   // 시그널로 깨어난 것은 정상
                continue;
            SLOG_ERROR("[Network] epoll_wait failed: {}", Platform::LastSocketError());
            break;
        }

        for (int i = 0; i < n; ++i)
        {
            auto* session = static_cast<CSession*>(events[i].data.ptr);
            if (session == nullptr)
                continue;

            const uint32_t flags = events[i].events;

            // 끊김·오류 — 상대가 닫았거나 소켓이 망가졌다.
            if (flags & (EPOLLHUP | EPOLLERR | EPOLLRDHUP))
            {
                RequestDisconnectSession(session);
                continue;
            }

            // 읽기 준비 — 4-F에서 수신 경로를 채운다.
            // 쓰기 준비(EPOLLOUT) — 4-G에서 송신 경로를 채운다.
        }

        if (monitorIndex >= 0 && monitorIndex < CMonitorManager::MAX_WORKER_THREADS)
            _monitor._workerCounters[monitorIndex].AddDequeue();
    }
}

// ── 4-F/4-G에서 채울 자리 ──
//   지금은 링크만 되게 두고, 준비 통지를 실제 read/write로 잇는 일은 다음 페이즈에서 한다.

// 수신 착수 — epoll에서는 "걸어두는" 것이 없다. EPOLLIN 통지를 받은 워커가 직접 read를 부른다.
void CIOCPServer::PostRecv(CSession* session, bool skipAcquire)
{
    (void)session;
    (void)skipAcquire;
}

// 세그먼트 제출 — 4-G에서 write + EPOLLOUT 재등록으로 채운다.
//   반환값 규약은 IOCP 팔과 같다(제출 바이트 수, 실패는 음수).
int CIOCPServer::TransportSubmitSegment(CSession* session, SOCKET socket,
                                        const CRingBufferMT::SubmitInfo& info,
                                        int slot, int slotsAvailable, size_t* submittedBytes)
{
    (void)session; (void)socket; (void)info; (void)slot; (void)slotsAvailable;
    if (submittedBytes)
        *submittedBytes = 0;
    return -1;
}

#endif  // !_WIN32
