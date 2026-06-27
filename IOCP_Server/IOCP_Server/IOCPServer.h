#pragma once

#include "BuildConfig.h"  // USE_LOCKFREE_SENDQ 등 빌드 토글 (가장 먼저 include)

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include <vector>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <queue>
#include <functional>
#include <array>
#include <atomic>

#include "RingBuffer.h"
#include "SerialBuffer.h"
#include "../../Shared/Protocol/Protocol.h"
#include "LockFree/LockFreeStack.h"
#if USE_LOCKFREE_SENDQ
#include "LockFree/LockFreeQueue.h"
#endif
#include "TimingWheel.h"
#include "MonitorManager.h"
#include "Common.h"

#pragma comment(lib, "ws2_32.lib")

constexpr size_t MAX_PACKET_SIZE = MSG_DEFAULT_SIZE;  // SerialBuffer 버퍼 크기와 일치 (1460B, TCP MSS 기준)
constexpr size_t MIN_PACKET_SIZE = sizeof(EchoMsgHeader);  // 최소 패킷 크기 (가장 작은 헤더 기준)

// I/O 작업 종류
enum class IOOperation
{
    RECV,
    SEND,
    ACCEPT
};



class CSession
{
public:

    // 세션에 고정 보관되는 OVERLAPPED 확장 구조체
    struct OverlappedEx
    {
        OVERLAPPED overlapped;      // 반드시 첫 번째 멤버
        IOOperation operation;      // I/O 타입 (RECV, SEND, ACCEPT 등)
    };

    explicit CSession();
    virtual ~CSession();

    void Initialize(SOCKET socket, int64_t sessionId);
    void Close();


    // SessionID 구조 헬퍼
    static constexpr int SESSION_INDEX_BITS = 16;
    static constexpr int SESSION_MAX_COUNT = 0xFFFF; // index 0~65534, 65535(0xFFFF)는 예약
    static constexpr int64_t SESSION_INDEX_MASK = 0xFFFF000000000000LL;
    static constexpr int64_t SESSION_UNIQUE_MASK = 0x0000FFFFFFFFFFFFLL;

    static uint16_t ExtractIndex(int64_t sessionId)
    {
        return static_cast<uint16_t>((sessionId >> 48) & 0xFFFF);
    }

    static int64_t ExtractUniqueId(int64_t sessionId)
    {
        return sessionId & SESSION_UNIQUE_MASK;
    }

    static int64_t MakeSessionId(uint16_t index, int64_t uniqueId)
    {
        return (static_cast<int64_t>(index) << 48) | (uniqueId & SESSION_UNIQUE_MASK);
    }

public:
    volatile LONG _ioCount;         // CAS로 0→증가 방지. pending I/O 개수 (base ref 없음)
    volatile LONG _disconnecting;   // 종료 진행 플래그. InterlockedExchange로 1회만 설정
    volatile SOCKET _socket;
    volatile int64_t _sessionId;    // Initialize에서 설정, IOCount>0 동안 유효
    volatile LONG _sending;         // 송신 중 플래그 (InterlockedExchange 사용) — 완료 워커가 고빈도 갱신

    // 게임 스레드가 만지는 송신 표식 묶음 — 위쪽 _sending(완료 워커 고빈도 갱신)과
    // 캐시라인 분리하여 false sharing 방지. (_queuedForSend는 송신 스레드도 clear)
    alignas(64) bool _sendDirty = false;   // [coalescing] 틱 내 송신 대기 표식 (게임 스레드 전용, Initialize에서 리셋)
    volatile LONG _queuedForSend = FALSE;  // [send-worker] 워커 queue 잔류 표식 — 틱을 넘는 중복 push 방지.
                                           // 게임 스레드(push 시 set)·송신 스레드(처리 전 clear) 공유 → Interlocked 필수.

    CRingBufferST _recvQ; // 한 스레드에서만 접근

#if USE_LOCKFREE_SENDQ
    LockFree::CLockFreeQueue<CSerialBuffer*, false, true> _sendQ;

    static constexpr int MAX_SEND_BUFS = 64;
    static constexpr INT64 MAX_SENDQ_DEPTH = 512;  // SendQ 깊이 상한 (OOM 방어)
    CSerialBuffer* _pendingSendBufs[MAX_SEND_BUFS];
    int _pendingSendCount = 0;
    int _pendingSendBytes = 0;

    void ReleasePendingSendBufs()
    {
        for (int i = 0; i < _pendingSendCount; ++i)
        {
            if (_pendingSendBufs[i])
            {
                _pendingSendBufs[i]->SubRef();
                _pendingSendBufs[i] = nullptr;
            }
        }
        _pendingSendCount = 0;
        _pendingSendBytes = 0;
    }
#else
    CRingBufferMT _sendQ; // 다중 스레드에서 접근
#endif


    // IOCount가 0이 되어 세션이 재사용되기 전까지 OVERLAPPED 주소는 유지된다.
    OverlappedEx _recvOverlapped;
    OverlappedEx _sendOverlapped;
};


// 네트워크 레이어에서 게임 로직으로 전달하는 이벤트
struct NetworkEvent
{
    enum class Type
    {
        CONNECTED,
        DISCONNECTED,
        RECEIVED
    };

    Type type;
    int64_t sessionId;
    CSerialBuffer* pMsg;
    int64_t enqueueTimeNs = 0;   // PushNetworkEvent에서 스탬프 (handle-latency 측정용 steady_clock ns)

    NetworkEvent(Type t, int64_t id)
        : type(t), sessionId(id), pMsg(nullptr)
    {
    }

    NetworkEvent(Type t, int64_t id, CSerialBuffer* msg)
        : type(t), sessionId(id), pMsg(msg)
    {
    }
};


// 게임 로직에서 네트워크 레이어로 보낼 명령
struct NetworkCommand
{
    enum class Type
    {
        SEND_MSG,
        DISCONNECT_SESSION,
    };

    Type type;
    int64_t sessionId;
    std::vector<char> data;

    NetworkCommand(Type t, int64_t id)
        : type(t), sessionId(id)
    {
    }

    NetworkCommand(Type t, int64_t id, const char* buffer, size_t length)
        : type(t), sessionId(id), data(buffer, buffer + length)
    {
    }

    // Broadcast용
    NetworkCommand(Type t, const char* buffer, size_t length)
        : type(t), sessionId(-1), data(buffer, buffer + length)
    {
    }
};


// 스레드 안전한 큐
template<typename T>
class ThreadSafeQueue
{
public:
    void Push(T&& item)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _queue.push(std::move(item));
    }

    bool TryPop(T& item)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_queue.empty())
        {
            return false;
        }
        item = std::move(_queue.front());
        _queue.pop();
        return true;
    }

    // 공유 큐를 통째로 빼고 즉시 해제 (단일 소비자 전용 — 락 보유 구간을 swap 1회로 축소)
    // out에 남은 잔여가 있으면 그대로 두고 뒤에 이어붙지 않으므로, 소비자는 비운 큐를 넘길 것
    void SwapOut(std::queue<T>& out)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _queue.swap(out);
    }

    bool IsEmpty() const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _queue.empty();
    }

    size_t GetSize() const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _queue.size();
    }

private:
    std::queue<T> _queue;
    mutable std::mutex _mutex;
};

//TODO: 모드별 설계..

// 송신 디스패치 시점 — 호출부는 의도만 표시하고, Deferred의 실제 지연 여부는
// USE_SEND_COALESCING(BuildConfig.h)이 결정한다. (coalescing off면 Deferred도 즉시 송신)
//   Immediate : 즉시 PostSend (echo·워커 등 단발 송신)
//   Deferred  : 묶어 보내도 되는 게임루프 송신 → 틱 끝 FlushPendingSends에서 일괄
enum class SendFlush { Immediate, Deferred };

// IOCP 기반 네트워크 서버
class CIOCPServer
{
public:
    explicit CIOCPServer(int port, int maxClients, ServerMode mode,
                        CMonitorManager& monitor, int workerThreads = 0, int sendWorkers = 0);
    virtual ~CIOCPServer();

    bool Start();
    void ShutdownServer();

    // 게임 로직 레이어가 사용할 인터페이스 (직접 호출)
    // thread-safe하다면 굳이 큐방식으로 부하를 줄 필요가 없음.
    // 송신은 콘텐츠가 조립한 CSerialBuffer 단일 경로로 통일 (모드 분기는 .cpp 내부 #if)
    // 반환: enqueue 성공 true / 실패(세션무효·ABA·큐오버플로) false — 송신 메트릭은 호출자가 집계 (broadcast 배치)
    bool RequestSendMsg(int64_t sessionId, CSerialBuffer* pMsg, SendFlush flush = SendFlush::Immediate);
    bool RequestDisconnectSession(int64_t sessionId);

    // [coalescing] 게임 루프가 틱 끝에 1회 호출 — 이번 틱에 enqueue된 세션을 한 번에 flush (게임 스레드 단독)
    void FlushPendingSends();

    // 게임 로직 레이어로 전달할 이벤트 가져오기 (QUEUE_BASED 모드용)
    bool PopNetworkEvent(NetworkEvent& event);

    // 프레임 진입 시점의 이벤트를 통째로 스왑해 가져오기 (게임 루프 단일 소비자 전용)
    // 건당 락(N회/프레임)을 swap 1회/프레임로 축소. out은 비운 큐를 넘길 것
    void SwapOutNetworkEvents(std::queue<NetworkEvent>& out) { _eventQueue.SwapOut(out); }

    // 이벤트 큐 현재 크기 (모니터링용)
    size_t GetEventQueueSize() const { return _eventQueue.GetSize(); }

    // 서버 모드 가져오기
    ServerMode GetServerMode() const;

    // 내부에서 사용할 함수
private:
    bool RequestDisconnectSession(CSession* session);
    void ReleaseSession(CSession* session);

private:

    void EchoTestSend(CSession* session, CSerialBuffer* pMsg);

    // 게임 로직으로 이벤트 전달 (QUEUE_BASED 모드용)
    void PushNetworkEvent(NetworkEvent&& event);

    void AcceptThread();
    void WorkerThread();
#if USE_SEND_THREAD
    void SendWorkerThread(int workerIdx);   // 전용 송신 워커 — 자기 워커의 dirty 배치를 받아 WSASend 수행
#endif

    bool CreateListenSocket();
    bool SetSocketOptions(SOCKET socket);
    bool BindIOCP(SOCKET socket, ULONG_PTR completionKey);

    void ProcessAccept(SOCKET clientSocket);
    void ProcessRecv(CSession* session, DWORD bytesTransferred);
    void ProcessSend(CSession* session, DWORD bytesTransferred);

    void PostRecv(CSession* session, bool skipAcquire = false);
    void PostSend(CSession* session);
    void ParsePackets(CSession* session);

    CSession* FindSession(int64_t sessionId);

    // 세션 재사용 방지용 pin/ref 인터페이스
    bool AcquireSession(CSession* session);
    void IOCountDecrement(CSession* session);

private:
    int _port;
    int _maxClients;
    ServerMode _serverMode;
    int _configuredWorkers;   // INI 지정 워커 수 (0=affinity 코어 수로 자동 산정)
    int _configuredSendWorkers;   // INI 지정 송신 워커 수 (0/1=단일)
    CMonitorManager& _monitor;
    volatile LONG _running;
    volatile LONGLONG _sessionIdCounter;  // 고유 ID용 (하위 48비트)

    SOCKET _listenSocket;
    HANDLE _iocpHandle;

    std::vector<std::thread> _workerThreads;
    std::thread _acceptThread;

    std::vector<std::unique_ptr<CSession>> _sessions;  // Index 기반 접근가능
    LockFree::CLockFreeStack<uint16_t> _availableIndices;  // 재사용 가능한 인덱스 스택

    // [coalescing] 틱 내 송신 대기 세션 목록 (게임 스레드 단독 접근 → 무락)
    std::vector<CSession*> _dirtySessions;

#if USE_SEND_THREAD
    // 송신 워커 풀 — 게임루프가 dirty 배치(sessionId)를 sessionId%K 워커에 넘기고 각 워커가 WSASend.
    //   한 세션은 항상 같은 워커(FIFO 보장). 완료(WSASend 결과)는 기존 IOCP 워커가 처리, 제출만 송신 워커 담당.
    struct alignas(64) SendWorker                      // alignas: 워커 간 false sharing 차단(측정 변수 제거용)
    {
        std::mutex              mutex;
        std::condition_variable cv;
        std::vector<int64_t>    queue;               // 게임스레드 push(lock) / 워커스레드 swap-out
        std::thread             thread;
    };
    std::vector<std::unique_ptr<SendWorker>> _sendWorkers;   // mutex/cv 이동불가 → 힙 고정 + 포인터만 보관
    std::atomic<bool>                        _sendStop{ false };
    int                                      _sendWorkerCount = 1;  // 분배 모듈러(=워커 수)
#endif

    // 레이어 간 통신 큐 (QUEUE_BASED 모드용)
    ThreadSafeQueue<NetworkEvent> _eventQueue;    // 네트워크 -> 게임 로직

    // 세션 무활동 타임아웃 (타이밍 휠)
    static constexpr int SESSION_TIMEOUT_SEC = 60;
    static constexpr int TIMER_TICK_INTERVAL_MS = 1000;
    std::unique_ptr<CTimingWheel> _timingWheel;

    static void OnSessionTimeout(void* context, int64_t sessionId);
};
