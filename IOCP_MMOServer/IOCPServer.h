#pragma once

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include <vector>
#include <memory>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <queue>
#include <functional>
#include <array>

#include "RingBuffer.h"
#include "SerialBuffer.h"
#include "Protocol.h"
#include "LockFree/LockFreeStack.h"
#include "TimingWheel.h"
#include "MonitorManager.h"

#pragma comment(lib, "ws2_32.lib")

constexpr size_t MAX_PACKET_SIZE = 4096;  // 최대 패킷 크기 (4KB)
constexpr size_t MIN_PACKET_SIZE = sizeof(EchoMsgHeader);  // 최소 패킷 크기 (가장 작은 헤더 기준)

// I/O 작업 종류
enum class IOOperation
{
    RECV,
    SEND,
    ACCEPT
};


// 서버 아키텍처 타입
enum class ServerArchitectureType
{
    GameCodiEchoTest, // 에코 테스트용 (최소 기능) - 헤더: EchoMsgHeader(2byte), size=페이로드크기
    Centralized,    // 중앙 집중형 - 헤더: MsgHeader(4byte), size=전체크기(헤더포함)
    Partitioned,    // 분산형 - 헤더: MsgHeader(4byte), size=전체크기(헤더포함)
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
    volatile LONG _sending;         // 송신 중 플래그 (InterlockedExchange 사용)

    CRingBufferST _recvQ; // 한 스레드에서만 접근
    CRingBufferMT _sendQ; // 다중 스레드에서 접근


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

//TODO: 아키텍쳐별 설계..

// IOCP 기반 네트워크 서버
class CIOCPServer
{
public:
    explicit CIOCPServer(int port, int maxClients, ServerArchitectureType type,
                        CMonitorManager& monitor);
    virtual ~CIOCPServer();

    bool Start();
    void ShutdownServer();

    // 게임 로직 레이어가 사용할 인터페이스 (직접 호출)
    // thread-safe하다면 굳이 큐방식으로 부하를 줄 필요가 없음.
    void RequestSendMsg(int64_t sessionId, const char* data, int length);
    void RequestSendMsg(int64_t sessionId, CSerialBuffer* pMsg);
    bool RequestDisconnectSession(int64_t sessionId);

    // 게임 로직 레이어로 전달할 이벤트 가져오기 (QUEUE_BASED 모드용)
    bool PopNetworkEvent(NetworkEvent& event);

    // 처리 방식 타입 가져오기
    ServerArchitectureType GetArchitectureType() const;

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
    ServerArchitectureType _architectureType;
    CMonitorManager& _monitor;
    volatile LONG _running;
    volatile LONGLONG _sessionIdCounter;  // 고유 ID용 (하위 48비트)

    SOCKET _listenSocket;
    HANDLE _iocpHandle;

    std::vector<std::thread> _workerThreads;
    std::thread _acceptThread;

    std::vector<std::unique_ptr<CSession>> _sessions;  // Index 기반 접근가능
    LockFree::CLockFreeStack<uint16_t> _availableIndices;  // 재사용 가능한 인덱스 스택

    // 레이어 간 통신 큐 (QUEUE_BASED 모드용)
    ThreadSafeQueue<NetworkEvent> _eventQueue;    // 네트워크 -> 게임 로직

    // 세션 무활동 타임아웃 (타이밍 휠)
    static constexpr int SESSION_TIMEOUT_SEC = 60;
    static constexpr int TIMER_TICK_INTERVAL_MS = 1000;
    std::unique_ptr<CTimingWheel> _timingWheel;

    static void OnSessionTimeout(void* context, int64_t sessionId);
};
