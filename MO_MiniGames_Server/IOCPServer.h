#pragma once

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <queue>
#include <functional>
#include <stack>
#include <array>

#include "RingBuffer.h"
#include "Protocol.h"

#pragma comment(lib, "ws2_32.lib")

constexpr size_t MAX_PACKET_SIZE = 65535;  // 최대 패킷 크기 (64KB)
constexpr size_t MIN_PACKET_SIZE = sizeof(MsgHeader);  // 최소 패킷 크기

enum class IOOperation
{
    RECV,
    SEND,
    ACCEPT
};

// 서버 아키텍처 타입
enum class ServerArchitectureType
{
    EchoTest,       // 에코 테스트용 (최소 기능)
    Centralized,    // 중앙 집중형 - 별도 스레드에서 이벤트 처리
    Partitioned,    // 분산형 - 여러 스레드/큐로 분리 처리
};

class CSession
{
public:
    // 내부 I/O 관리용 확장 OVERLAPPED 구조체
    struct OverlappedEx
    {
        OVERLAPPED overlapped;      // 반드시 첫 번째 멤버
        int64_t sessionId;          // I/O 요청 시점의 세션ID (ABA 방지)
        IOOperation operation;      // I/O 타입 (RECV, SEND, ACCEPT 등)
    };

    explicit CSession();
    virtual ~CSession();

    void Initialize(SOCKET socket, int64_t sessionId);
    void Close();

    // SessionID 구조 헬퍼 (static 멤버로 이동)
    static constexpr int SESSION_INDEX_BITS = 16;
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

    // 변경: 연결 상태 확인 함수

public: 
    SOCKET _socket;
    int64_t _sessionId;
    std::atomic<bool> _valid; // 유효성
    std::atomic<bool> _sending; // 송신 중 플래그

    CRingBufferST _recvQ; // 한 스레드에서만 접근
    CRingBufferMT _sendQ; // 다중 스레드에서 접근

    OverlappedEx _recvOverlapped;
    OverlappedEx _sendOverlapped;
};

// 게임 로직 레이어로 전달할 네트워크 이벤트
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
    std::vector<char> data;

    NetworkEvent(Type t, int64_t id)
        : type(t), sessionId(id)
    {
    }

    NetworkEvent(Type t, int64_t id, const char* buffer, size_t length)
        : type(t), sessionId(id), data(buffer, buffer + length)
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

private:
    std::queue<T> _queue;
    mutable std::mutex _mutex;
};

//TODO: 아키텍쳐별 설계..

// 네트워크 I/O 처리 레이어
class CIOCPServer
{
public:
    explicit CIOCPServer(int port, int maxClients, ServerArchitectureType type);
    virtual ~CIOCPServer();

    bool Start();
    void Disconnect();

    // 게임 로직 레이어가 사용할 인터페이스 (직접 호출)
    // thread-safe하다면 굳이 큐방식으로 부하를 줄 필요가 없음.
    void RequestSendMsg(int64_t sessionId, const char* data, int length);
    bool RequestDisconnectSession(int64_t sessionId);

    // 게임 로직 레이어로 전달할 이벤트 가져오기 (QUEUE_BASED 모드용)
    bool PopNetworkEvent(NetworkEvent& event);

    // 처리 방식 타입 가져오기
    ServerArchitectureType GetArchitectureType() const;

    // 내부에서 사용할 함수
private:
    bool DisconnectSessionInternal(CSession* session);

private:

    void EchoTestSend(CSession* session, const char* data, size_t length);
    // 게임 로직으로 이벤트 전달 (QUEUE_BASED 모드용)
    void PushNetworkEvent(NetworkEvent&& event);

    void AcceptThread();
    void WorkerThread();

    bool CreateListenSocket();
    bool SetSocketOptions(SOCKET socket);
    bool BindIOCP(SOCKET socket, ULONG_PTR completionKey);
    void ReleaseSession();

    void ProcessAccept(SOCKET clientSocket);
    void ProcessRecv(CSession* session, DWORD bytesTransferred);
    void ProcessSend(CSession* session, DWORD bytesTransferred);

    void PostRecv(CSession* session);
    void PostSend(CSession* session); // 송신 요청 함수 추가
    void ParsePackets(CSession* session);

    CSession* FindSession(int64_t sessionId);

private:
    int _port;
    int _maxClients;
    ServerArchitectureType _architectureType;
    std::atomic<bool> _running;
    std::atomic<int64_t> _sessionIdCounter;  // 고유 ID용 (하위 48비트)

    SOCKET _listenSocket;
    HANDLE _iocpHandle;

    std::vector<std::thread> _workerThreads;
    std::thread _acceptThread;

    std::vector<std::unique_ptr<CSession>> _sessions;  // Index 기반 접근가능
    std::queue<uint16_t> _availableIndices;  // 재사용 가능한 인덱스 큐
    std::stack<uint64_t> _pendingDisconStack; // 종료 대기 중인 세션ID 스택 // TODO : 락프리구조로 바꿔야함

    std::mutex _pendingDisconMtx; //_pendingDisconStack 전용 mutex

    // 레이어 간 통신 큐 (QUEUE_BASED 모드용)
    ThreadSafeQueue<NetworkEvent> _eventQueue;    // 네트워크 -> 게임 로직
};
