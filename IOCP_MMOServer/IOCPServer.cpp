#include "IOCPServer.h"
#include "../Shared/Common/ErrorLog.h"
#include <iostream>

#pragma comment(lib, "winmm.lib")

extern void SignalProcessShutdown(); // main 쪽에 정의된 종료 알림 함수


// CSession Implementation
CSession::CSession()
    : _ioCount(0)
    , _disconnecting(FALSE)
    , _socket(INVALID_SOCKET)
    , _sessionId(0)
    , _sending(FALSE)
{
    ZeroMemory(&_recvOverlapped.overlapped, sizeof(OVERLAPPED));
    _recvOverlapped.operation = IOOperation::RECV;
    ZeroMemory(&_sendOverlapped.overlapped, sizeof(OVERLAPPED));
    _sendOverlapped.operation = IOOperation::SEND;
}

void CSession::Initialize(SOCKET socket, int64_t sessionId)
{
    _socket = socket;
    _sessionId = sessionId;
    _disconnecting = FALSE;
    _sending = FALSE;
    _recvQ.Clear();
    _sendQ.Clear();

    // 세션 고정 Overlapped 방식: IO 요청마다 재사용하므로 요청 전 OVERLAPPED만 초기화한다.
    ZeroMemory(&_recvOverlapped.overlapped, sizeof(OVERLAPPED));
    _recvOverlapped.operation = IOOperation::RECV;
    ZeroMemory(&_sendOverlapped.overlapped, sizeof(OVERLAPPED));
    _sendOverlapped.operation = IOOperation::SEND;

    // _ioCount를 마지막에 설정 — 첫 번째 Recv IO의 ref (base ref 아님).
    // InterlockedExchange가 full barrier를 제공하므로 위의 모든 쓰기가 이 시점 전에 완료된다.
    // 이 시점부터 세션이 외부에 공개된다.
    InterlockedExchange(&_ioCount, 1);
}

CSession::~CSession()
{
    Close();
}

void CSession::Close()
{
    // 소켓과 세션 상태를 정리한다. 버퍼 등 나머지는 Initialize()에서 초기화한다.
    // IOCount=0 시점에 단일 스레드에서만 호출되므로 Interlocked 불필요.
    _sending = FALSE;
    _sessionId = 0;

    SOCKET socket = _socket;
    _socket = INVALID_SOCKET;
    if (socket != INVALID_SOCKET)
    {
        closesocket(socket);
    }
}

// CIOCPServer Implementation
CIOCPServer::CIOCPServer(int port, int maxClients, ServerArchitectureType type,
                         CMonitorManager& monitor)
    : _port(port)
    , _maxClients(maxClients)
    , _architectureType(type)
    , _monitor(monitor)
    , _running(FALSE)
    , _sessionIdCounter(1)  // 0은 사용하지 않음
    , _listenSocket(INVALID_SOCKET)
    , _iocpHandle(NULL)
{
    // 멤버 변수만 초기화
}

CIOCPServer::~CIOCPServer()
{
    ShutdownServer();
}


bool CIOCPServer::Start()
{
    if (_maxClients <= 0 || _maxClients > CSession::SESSION_MAX_COUNT)
    {
        LOG_ERROR_STREAM("[Error] maxClients(" << _maxClients << ") out of range. valid: 1~" << CSession::SESSION_MAX_COUNT);
        return false;
    }

    // 세션 객체는 서버 시작 시 동접자 수만큼 고정 생성하고 이후 index만 재사용한다.

    // 세션 객체는 서버 시작 시 동접자 수만큼 고정 생성하고 이후 index만 재사용한다.
    _sessions.resize(_maxClients);
    for (uint16_t i = 0; i < _maxClients; ++i)
    {
        // INVALID_SOCKET과 0 세션ID로 미리 생성
        _sessions[i] = std::make_unique<CSession>();
        if (!_sessions[i]->_recvQ.Init() || !_sessions[i]->_sendQ.Init())
        {
            LOG_ERROR_STREAM("Failed to init session RingBuffer [index=" << i << "]");
            return false;
        }
    }


    // SerialBuffer 메모리풀 초기화
    // 클라이언트당 동시 송신 대기 버퍼 1~2개 가정, 부족 시 청크 자동 증가
    CSerialBuffer::_TlsMsgFreeList = new CExternalTlsFreeList<CSerialBuffer>();
    if (!CSerialBuffer::_TlsMsgFreeList->Init(_maxClients * 2))
    {
        LOG_ERROR_STREAM("Failed to init SerialBuffer FreeList");
        return false;
    }

    // 인덱스 스택 초기화 (maxClients만큼 사전 할당)
    if (!_availableIndices.Init(_maxClients))
    {
        printf("[Error] Failed to init available indices stack\n");
        return false;
    }

    // Start 재호출 시 이전 lock-free 스택에 남은 index를 비운다.
    uint16_t staleIndex = 0;
    while (_availableIndices.Pop(&staleIndex))
    {
    }

    // 빈 인덱스 스택 초기화 (0번부터 maxClients-1까지)
    for (uint16_t i = 0; i < _maxClients; ++i)
    {
        _availableIndices.Push(i);
    }

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        LOG_ERROR_STREAM("WSAStartup failed");
        return false;
    }

    // IOCP 핸들 생성
    _iocpHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (_iocpHandle == NULL)
    {
        WSACleanup();
        return false;
    }

    // Listen 소켓 생성
    if (!CreateListenSocket())
    {
        CloseHandle(_iocpHandle);
        WSACleanup();
        return false;
    }

    InterlockedExchange(&_running, TRUE);

    // 시스템 타이머 해상도를 1ms로 설정 (Sleep, WaitForSingleObject 등 정밀도 향상)
    timeBeginPeriod(1);

    // 타이밍 휠 생성 및 시작
    _timingWheel = std::make_unique<CTimingWheel>();
    if (!_timingWheel->Init(_maxClients, SESSION_TIMEOUT_SEC, TIMER_TICK_INTERVAL_MS))
    {
        printf("[TimingWheel] Init failed\n");
        return false;
    }
    _timingWheel->Start(OnSessionTimeout, this);

    // 워커 스레드 생성 (CPU 코어 * 2)
    int threadCount = std::thread::hardware_concurrency() * 2;
    for (int i = 0; i < threadCount; ++i)
    {
        _workerThreads.emplace_back(&CIOCPServer::WorkerThread, this);
    }

    // Accept 스레드 생성
    _acceptThread = std::thread(&CIOCPServer::AcceptThread, this);

    std::cout << "[Network] Server started with " << threadCount << " worker threads (Mode: ";

    switch (_architectureType)
    {
    case ServerArchitectureType::GameCodiEchoTest: std::cout << "GameCodiEchoTest"; break;
    case ServerArchitectureType::Centralized: std::cout << "Centralized"; break;
    case ServerArchitectureType::Partitioned: std::cout << "Partitioned"; break;

    default:
        break;
    }
    std::cout << ")" << std::endl;
    return true;
}


bool CIOCPServer::CreateListenSocket()
{
    _listenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (_listenSocket == INVALID_SOCKET)
    {
        const int wsaErr = WSAGetLastError();
        LOG_WSA_ERROR_STREAM("WSASocket failed: ", wsaErr);
        return false;
    }

    SOCKADDR_IN serverAddr;
    ZeroMemory(&serverAddr, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(_port);

    if (bind(_listenSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
    {
        const int wsaErr = WSAGetLastError();
        LOG_WSA_ERROR_STREAM("bind failed: ", wsaErr);
        closesocket(_listenSocket);
        return false;
    }

    if (listen(_listenSocket, SOMAXCONN_HINT(1024)) == SOCKET_ERROR)
    {
        const int wsaErr = WSAGetLastError();
        LOG_WSA_ERROR_STREAM("listen failed: ", wsaErr);
        closesocket(_listenSocket);
        return false;
    }

    return true;
}


// 대부분 연결별 동작에 영향을 주는 옵션은 accept후에 설정해야한다.
bool CIOCPServer::SetSocketOptions(SOCKET socket)
{
    // LINGER 옵션: 연결 종료 시 RST 전송 (즉시 종료)
    LINGER lingerOpt;
    lingerOpt.l_onoff = 1;
    lingerOpt.l_linger = 0;
    setsockopt(socket, SOL_SOCKET, SO_LINGER, reinterpret_cast<char*>(&lingerOpt), sizeof(lingerOpt));

    // TCP_NODELAY 옵션: Nagle 알고리즘 비활성화 (지연 없이 송신)
    //int flag = 1;
    //setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(flag));

    // 필요시 추가 옵션 설정 가능
    return true;
}

bool CIOCPServer::BindIOCP(SOCKET socket, ULONG_PTR completionKey)
{
    auto handle = CreateIoCompletionPort((HANDLE)socket, _iocpHandle, completionKey, 0);
    if (handle == NULL)
    {
        LOG_ERROR_STREAM("BindIOCP failed: " << GetLastError());
        return false;
    }
    return true;
}


// 새 I/O 제출을 막고 pending I/O 완료를 유도한다. 실제 closesocket은 IOCount 0에서 수행한다.
void CIOCPServer::ShutdownServer()
{
    if (InterlockedExchange(&_running, FALSE) == FALSE)
    {
        return;
    }

    // 1. Listen 소켓 닫기 — 새 연결 차단 (AcceptThread 깨움)
    if (_listenSocket != INVALID_SOCKET)
    {
        closesocket(_listenSocket);
        _listenSocket = INVALID_SOCKET;
    }

    if (_acceptThread.joinable())
    {
        _acceptThread.join();
    }

    // 타이밍 휠 정지 (더 이상 타임아웃 disconnect 발생하지 않음)
    if (_timingWheel)
    {
        _timingWheel->Stop();
    }

    // 2. 모든 세션에 종료 유도 — CancelIoEx로 pending IO 완료를 촉진
    for (auto& session : _sessions)
    {
        if (session)
        {
            RequestDisconnectSession(session.get());
        }
    }

    // 3. 모든 세션의 IOCount가 0이 될 때까지 대기
    //    워커 스레드가 cancelled IO 완료 통지를 처리하여 IOCount를 감소시킨다.
    for (auto& session : _sessions)
    {
        if (!session)
            continue;

        while (session->_ioCount > 0)
        {
            Sleep(1);
        }
    }

    // 4. 모든 IO 정리 완료 — 워커 스레드 종료
    if (_iocpHandle != NULL)
    {
        for (size_t i = 0; i < _workerThreads.size(); ++i)
        {
            PostQueuedCompletionStatus(_iocpHandle, 0, 0, nullptr);
        }
    }

    for (auto& thread : _workerThreads)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }

    if (_iocpHandle != NULL)
    {
        CloseHandle(_iocpHandle);
        _iocpHandle = NULL;
    }

    WSACleanup();

    // 타이머 해상도 복원
    timeEndPeriod(1);

    // 네트워크 종료 완료 → 프로세스 종료 알림
    SignalProcessShutdown();
}



// 윈도우 accept에는 timeout 기능이 없음.
// listen socket close로 accept를 깨운다.
void CIOCPServer::AcceptThread()
{
    while (_running == TRUE)
    {
        SOCKADDR_IN clientAddr;
        int addrLen = sizeof(clientAddr);

        SOCKET clientSocket = accept(_listenSocket, (SOCKADDR*)&clientAddr, &addrLen);


        if (clientSocket == INVALID_SOCKET)
        {
            if (_running == TRUE)
            {
                const int wsaErr = WSAGetLastError();
                LOG_WSA_ERROR_STREAM("accept failed: ", wsaErr);
            }
            continue;
        }

        SetSocketOptions(clientSocket);
        ProcessAccept(clientSocket);
    }
}

void CIOCPServer::ProcessAccept(SOCKET clientSocket)
{
    // 빈 인덱스 확인 (여유가 없다면 동접 max)
    uint16_t index = 0;
    if (!_availableIndices.Pop(&index))
    {
        LOG_ERROR_STREAM("[Error] No free session index available");
        closesocket(clientSocket);
        return;
    }

    // 고유 ID 생성 (하위 48비트만 사용)
    int64_t uniqueId = InterlockedIncrement64(&_sessionIdCounter) & CSession::SESSION_UNIQUE_MASK;

    // SessionID 생성 [16bit Index][48bit UniqueID]
    int64_t sessionId = CSession::MakeSessionId(index, uniqueId);

    // session 초기화. 사용가능한 상태가 됨
    _sessions[index]->Initialize(clientSocket, sessionId);

    // IOCP의 CompletionKey는 단순 식별자 역할이므로, 세션 소유권을 갖지 않는다.
    if (!BindIOCP(clientSocket, (ULONG_PTR)_sessions[index].get()))
    {
        LOG_ERROR_STREAM("Failed to bind client socket to IOCP");
        // RequestDisconnectSession → ReleaseSession(IOCount→0)으로 정리
        RequestDisconnectSession(_sessions[index].get());
        IOCountDecrement(_sessions[index].get());
        return;
    }

    // 컨텐츠쪽 전달
    switch (_architectureType)
    {
    case ServerArchitectureType::GameCodiEchoTest:
        // 따로 전달 없음
        break;
    case ServerArchitectureType::Centralized:
        PushNetworkEvent(NetworkEvent(NetworkEvent::Type::CONNECTED, sessionId));
        break;

    default:
        break;
    }

    // 세션 지표 기록
    _monitor._sessionCreated.fetch_add(1, std::memory_order_relaxed);
    _monitor._sessionCount.fetch_add(1, std::memory_order_relaxed);

    // 타이밍 휠에 세션 등록 (타임아웃 카운트 시작)
    _timingWheel->RequestRegister(CSession::ExtractIndex(sessionId), sessionId);

    // 첫 Recv — Initialize의 IOCount=1이 이 IO의 ref. AcquireSession 불필요.
    PostRecv(_sessions[index].get(), true);
}

// 완료 통지 처리. IOCount 감소는 ProcessRecv/ProcessSend 이후에만 수행한다.
void CIOCPServer::WorkerThread()
{
    while (true)
    {
        DWORD bytesTransferred = 0;
        ULONG_PTR completionKey = 0;
        OVERLAPPED* overlapped = nullptr;

        BOOL result = GetQueuedCompletionStatus(_iocpHandle, &bytesTransferred,
            &completionKey, &overlapped, INFINITE);

        if (overlapped == nullptr)
        {
            if (_running == FALSE)
                break;
            continue;
        }

        auto overlappedEx = reinterpret_cast<CSession::OverlappedEx*>(overlapped);
        auto session = reinterpret_cast<CSession*>(completionKey);

        if (overlappedEx == nullptr || session == nullptr)
        {
            LOG_ERROR_STREAM("[Error] Invalid overlappedEx or session pointer in WorkerThread");
            continue;
        }

        IOOperation op = overlappedEx->operation;
        bool canProcess = (result != FALSE && bytesTransferred != 0 &&
            session->_disconnecting == FALSE);

        if (canProcess)
        {
            switch (op)
            {
            case IOOperation::RECV:
                ProcessRecv(session, bytesTransferred);
                break;
            case IOOperation::SEND:
                ProcessSend(session, bytesTransferred);
                break;
            default:
                break;
            }
        }
        else if (result == FALSE || bytesTransferred == 0)
        {
            RequestDisconnectSession(session);
        }

        IOCountDecrement(session);
    }
}

// Recv 완료 통지 처리
void CIOCPServer::ProcessRecv(CSession* session, DWORD bytesTransferred)
{
    // 방어 로직
    if (bytesTransferred == 0)
    {
        LOG_ERROR_STREAM("[Warning] ProcessRecv called with bytesTransferred == 0 - SessionId: " << session->_sessionId);
        RequestDisconnectSession(session);
        return;
    }

    // 타이밍 휠 수명 갱신 (데이터 수신 = 세션 활성 상태)
    _timingWheel->RequestRefresh(CSession::ExtractIndex(session->_sessionId), session->_sessionId);

    // 링버퍼 쓰기 포인터 이동
    size_t movedSize = session->_recvQ.MoveWritePtr(bytesTransferred);
    if (movedSize != bytesTransferred)
    {
        LOG_ERROR_STREAM("[Error] Recv buffer overflow - SessionId: " << session->_sessionId
            << ", Expected: " << bytesTransferred << ", Moved: " << movedSize);
        RequestDisconnectSession(session);
        return;
    }

    // 패킷 파싱
    ParsePackets(session);

    // 다음 Recv 요청
    PostRecv(session);
}


// Recv I/O 제출. 제출 전 IOCount로 세션을 pin한다.
// skipAcquire=true: ProcessAccept에서 첫 Recv 시 Initialize의 IOCount=1을 그대로 사용.
void CIOCPServer::PostRecv(CSession* session, bool skipAcquire)
{
    if (!session)
        return;

    if (!skipAcquire)
    {
        if (!AcquireSession(session))
            return;
    }

    const int64_t sessionId = session->_sessionId;

    CSession::OverlappedEx* ex = &session->_recvOverlapped;
    ZeroMemory(&ex->overlapped, sizeof(OVERLAPPED));
    ex->operation = IOOperation::RECV;

    // 링버퍼에서 쓰기 가능한 공간 확보
    char* writePtr = session->_recvQ.GetWritePtr();
    size_t directWriteSize = session->_recvQ.GetDirectWriteSize();

    WSABUF wsaBuf[2];
    int bufCount = 0;

    if (directWriteSize > 0)
    {
        wsaBuf[bufCount].buf = writePtr;
        wsaBuf[bufCount].len = static_cast<ULONG>(directWriteSize);
        bufCount++;

        size_t freeSize = session->_recvQ.GetFreeSize();
        if (freeSize > directWriteSize)
        {
            size_t wrapSize = freeSize - directWriteSize;
            wsaBuf[bufCount].buf = session->_recvQ._buffer;
            wsaBuf[bufCount].len = static_cast<ULONG>(wrapSize);
            bufCount++;
        }
    }

    if (bufCount == 0)
    {
        LOG_ERROR_STREAM("[Error] Recv buffer full - SessionId: " << sessionId);
        RequestDisconnectSession(session);
        IOCountDecrement(session);
        return;
    }

    if (session->_disconnecting == TRUE)
    {
        // WSABUF 준비 ~ IO 제출 사이에 세션이 disconnect되면 제출하지 않는다.
        IOCountDecrement(session);
        return;
    }

    SOCKET socket = session->_socket;
    if (socket == INVALID_SOCKET)
    {
        RequestDisconnectSession(session);
        IOCountDecrement(session);
        return;
    }

    DWORD flags = 0;
    DWORD recvBytes = 0;

    int result = WSARecv(socket, wsaBuf, bufCount, &recvBytes, &flags,
        &ex->overlapped, NULL);

    if (result == SOCKET_ERROR)
    {
        const int wsaErr = WSAGetLastError();
        if (wsaErr != WSA_IO_PENDING)
        {
            if (!shared::ShouldIgnoreWsaError(wsaErr))
            {
                LOG_WSA_ERROR_STREAM("[Error] WSARecv failed - SessionId: " << sessionId << ", WSAError: ", wsaErr);
            }
            RequestDisconnectSession(session);
            IOCountDecrement(session);
            return;
        }
    }

    // Post-check: pre-check ~ WSARecv 사이에 끼어든 disconnect race 회수.
    // CancelIoEx가 먼저 지나간 뒤 WSARecv가 늦게 걸린 IO를 취소한다.
    if (session->_disconnecting == TRUE)
    {
        CancelIoEx(reinterpret_cast<HANDLE>(socket), &ex->overlapped);
    }
}

// ParsePackets: 링버퍼에서 완성된 패킷 추출 → CSerialBuffer에 적재
void CIOCPServer::ParsePackets(CSession* session)
{
    // 특정 더미타입에 맞게 헤더사이즈 조정
    const size_t headerSize = (_architectureType == ServerArchitectureType::GameCodiEchoTest)
        ? sizeof(EchoMsgHeader)  // 2byte (size만, 페이로드 크기)
        : sizeof(MsgHeader);     // 4byte (size + type, 전체 크기)

    while (true)
    {
        size_t dataSize = session->_recvQ.GetDataSize();

        // 1. 헤더 크기 체크
        if (dataSize < headerSize)
        {
            break; // 데이터 부족
        }

        // 2. 헤더에서 size 필드 peek (size는 항상 첫 2byte)
        uint16_t packetSize = 0;
        size_t peekedSize = session->_recvQ.Peek(&packetSize, sizeof(uint16_t));
        if (peekedSize != sizeof(uint16_t))
        {
            break; // peek 실패
        }

        // 3. 전체 패킷 크기 계산
        // GameCodiEchoTest: size = 페이로드 크기 → 헤더 크기를 더해야 전체 크기
        // 그 외: size = 전체 크기 (헤더 포함) → 그대로 사용
        size_t totalPacketSize = (_architectureType == ServerArchitectureType::GameCodiEchoTest)
            ? static_cast<size_t>(packetSize) + sizeof(EchoMsgHeader)
            : static_cast<size_t>(packetSize);

        // 4. 패킷 크기 검증
        if (totalPacketSize < headerSize || totalPacketSize > MAX_PACKET_SIZE)
        {
            LOG_ERROR_STREAM("[Error] Invalid packet size: " << totalPacketSize
                << " - SessionId: " << session->_sessionId);
            RequestDisconnectSession(session);
            return;
        }

        // 5. 전체 패킷이 수신되었는지 확인
        if (dataSize < totalPacketSize)
        {
            break; // 데이터 부족 - 다음 Recv 대기
        }

        // 6. CSerialBuffer에 패킷 전체 적재 (헤더 포함)
        CSerialBuffer* pMsg = CSerialBuffer::Alloc();
        size_t dequeuedSize = session->_recvQ.Dequeue(pMsg->GetWriteBufferPtr(), totalPacketSize);
        if (dequeuedSize != totalPacketSize)
        {
            LOG_ERROR_STREAM("[Error] Packet dequeue failed - SessionId: " << session->_sessionId);
            CSerialBuffer::Free(pMsg);
            RequestDisconnectSession(session);
            return;
        }
        pMsg->MoveWritePos(static_cast<int>(totalPacketSize));

        // 수신 버퍼는 단일 소비자(GameLogicThread)이므로 Seal 불필요
        // Seal하면 operator>>/GetData가 차단되어 역직렬화 불가
        pMsg->AddRef();

        // 7. 컨텐츠쪽 전달 또는 처리
        switch (_architectureType)
        {
        case ServerArchitectureType::GameCodiEchoTest:
            EchoTestSend(session, pMsg);
            break;
        case ServerArchitectureType::Centralized:
            PushNetworkEvent(NetworkEvent(NetworkEvent::Type::RECEIVED,
                session->_sessionId, pMsg));
            break;

        default:
            pMsg->SubRef();
            break;
        }

    }
}

// Send 완료 통지 처리
void CIOCPServer::ProcessSend(CSession* session, DWORD bytesTransferred)
{
    if (!session || session->_disconnecting == TRUE)
    {
        return;
    }

    // SendQ에서 송신한 만큼 Consume
    size_t consumed = session->_sendQ.Consume(bytesTransferred);
    if (consumed != bytesTransferred)
    {
        LOG_ERROR_STREAM("[Error] Send consume mismatch - SessionId: " << session->_sessionId
            << ", Expected: " << bytesTransferred << ", Consumed: " << consumed);
        RequestDisconnectSession(session);
        return;
    }

    // 플래그 해제 후 PostSend에서 남은 데이터 확인 및 연속 송신
    InterlockedExchange(&session->_sending, FALSE);

    // Double-check: 플래그 해제 직후 다시 확인 (다른 스레드가 Enqueue했을 수 있음)
    if (session->_sendQ.GetDataSize() > 0)
    {
        PostSend(session);
    }
}

// Send는 1회로 제한
// Send I/O 제출. 제출 전 IOCount로 세션을 pin한다.
void CIOCPServer::PostSend(CSession* session)
{
    if (!session)
        return;

    if (!AcquireSession(session))
        return;

    const int64_t sessionId = session->_sessionId;

    if (InterlockedExchange(&session->_sending, TRUE) == TRUE)
    {
        IOCountDecrement(session);
        return;
    }

    // GetSendInfo() 이후 다른 스레드가 Enqueue해도 문제없다.
    // 이번 WSASend는 캡처 시점의 dataSize만 전송하고,
    // 나머지는 ProcessSend의 double-check에서 PostSend를 재호출하여 처리한다.
    auto sendInfo = session->_sendQ.GetSendInfo();

    if (sendInfo.dataSize == 0)
    {
        InterlockedExchange(&session->_sending, FALSE);
        IOCountDecrement(session);
        return;
    }

    CSession::OverlappedEx* ex = &session->_sendOverlapped;
    ZeroMemory(&ex->overlapped, sizeof(OVERLAPPED));
    ex->operation = IOOperation::SEND;

    WSABUF wsaBuf[2];
    int bufCount = 0;

    if (sendInfo.directReadSize > 0)
    {
        wsaBuf[bufCount].buf = sendInfo.readPtr;
        wsaBuf[bufCount].len = static_cast<ULONG>(sendInfo.directReadSize);
        bufCount++;

        if (sendInfo.dataSize > sendInfo.directReadSize)
        {
            size_t wrapSize = sendInfo.dataSize - sendInfo.directReadSize;
            wsaBuf[bufCount].buf = session->_sendQ._buffer;
            wsaBuf[bufCount].len = static_cast<ULONG>(wrapSize);
            bufCount++;
        }
    }

    if (bufCount == 0)
    {
        InterlockedExchange(&session->_sending, FALSE);
        IOCountDecrement(session);
        return;
    }

    if (session->_disconnecting == TRUE)
    {
        // WSABUF 준비 ~ IO 제출 사이에 세션이 disconnect되면 제출하지 않는다.
        InterlockedExchange(&session->_sending, FALSE);
        IOCountDecrement(session);
        return;
    }

    SOCKET socket = session->_socket;
    if (socket == INVALID_SOCKET)
    {
        InterlockedExchange(&session->_sending, FALSE);
        RequestDisconnectSession(session);
        IOCountDecrement(session);
        return;
    }

    DWORD sendBytes = 0;
    int result = WSASend(socket, wsaBuf, bufCount, &sendBytes, 0,
        &ex->overlapped, NULL);

    if (result == SOCKET_ERROR)
    {
        const int wsaErr = WSAGetLastError();
        if (wsaErr != WSA_IO_PENDING)
        {
            if (!shared::ShouldIgnoreWsaError(wsaErr))
            {
                LOG_WSA_ERROR_STREAM("[Error] WSASend failed - SessionId: " << sessionId
                    << ", WSAError: ", wsaErr);
            }
            InterlockedExchange(&session->_sending, FALSE);
            RequestDisconnectSession(session);
            IOCountDecrement(session);
            return;
        }
    }

    // Post-check: pre-check ~ WSASend 사이에 끼어든 disconnect race 회수.
    if (session->_disconnecting == TRUE)
    {
        CancelIoEx(reinterpret_cast<HANDLE>(socket), &ex->overlapped);
    }
}

// 게임 로직 레이어가 사용할 인터페이스
// 송신 요청: SendQ에 데이터 Enqueue 후 송신 시작
void CIOCPServer::RequestSendMsg(int64_t sessionId, const char* data, int length)
{
    // 3단계 Session ABA 검증
    // 1단계: sessionId의 상위 16비트(index)로 세션을 찾고, 저장된 sessionId가 일치하는지 확인
    auto session = FindSession(sessionId);
    if (!session)
        return;

    // 2단계: IOCount를 증가시켜 세션을 pin. 해제 진행 중이면 실패한다.
    if (!AcquireSession(session))
        return;

    // 3단계: pin 성공 후 sessionId 재확인. FindSession ~ AcquireSession 사이에
    //        세션이 해제되고 같은 index에 재할당되었을 경우를 검출한다.
    if (session->_sessionId != sessionId)
    {
        IOCountDecrement(session);
        return;
    }

    // SendQ에 데이터 Enqueue
    size_t enqueued = session->_sendQ.Enqueue(data, length);
    if (enqueued != length)
    {
        LOG_ERROR_STREAM("[Error] Send buffer overflow - SessionId: " << sessionId
            << ", Requested: " << length << ", Enqueued: " << enqueued);
        RequestDisconnectSession(session);
        IOCountDecrement(session);
        return;
    }

    PostSend(session);
    IOCountDecrement(session);
}

void CIOCPServer::EchoTestSend(CSession* session, CSerialBuffer* pMsg)
{
    // 에코 테스트: 받은 패킷을 그대로 돌려보냄
    RequestSendMsg(session->_sessionId, pMsg);
}

// TODO: 송신 최적화 단계별 적용 예정
// 1단계(현재): CSerialBuffer → sendQ(RingBuffer) memcpy → WSASend (버퍼 복사)
// 2단계: sendQ를 CSerialBuffer* 포인터 큐로 교체 (데이터 복사 제거)
// 3단계: SO_SNDBUF=0 적용 (커널 복사까지 제거, 진정한 zero-copy)
void CIOCPServer::RequestSendMsg(int64_t sessionId, CSerialBuffer* pMsg)
{
    RequestSendMsg(sessionId, pMsg->GetReadBufferPtr(), pMsg->GetDataSize());
    pMsg->SubRef();
}

// ParsePackets 쪽에서 호출
void CIOCPServer::PushNetworkEvent(NetworkEvent&& event)
{
    _eventQueue.Push(std::move(event));
}

// GameLogicThread 쪽에서 호출
bool CIOCPServer::PopNetworkEvent(NetworkEvent& event)
{
    return _eventQueue.TryPop(event);
}

ServerArchitectureType CIOCPServer::GetArchitectureType() const
{
    return _architectureType;
}

// 순수 종료 유도 — releaseFlag 설정 + CancelIoEx로 pending IO 취소.
// 추가 IO를 막아 IOCount가 0이되어 ReleaseSession 호출을 유도
bool CIOCPServer::RequestDisconnectSession(CSession* session)
{
    if (!session)
        return false;

    // 다른스레드에서 이미 처리중인 경우 
    if (InterlockedExchange(&session->_disconnecting, TRUE) == TRUE)
        return false;

    // CancelIoEx로 pending IO를 즉시 완료(에러)시켜 IOCount가 0으로 수렴하게 한다.
    // INVALID_SOCKET이면 ERROR_INVALID_HANDLE로 실패할 뿐, 부작용 없음.
    CancelIoEx(reinterpret_cast<HANDLE>(session->_socket), nullptr);

    return true;
}

// 세션 포인터의 lifetime만 pin한다. SessionID 검증은 외부 진입점에서 별도로 수행한다.
// CAS로 IOCount가 0인 세션(해제 완료/진행 중)은 절대 증가시키지 않는다.
bool CIOCPServer::AcquireSession(CSession* session)
{
    if (!session)
        return false;

    if (session->_disconnecting == TRUE)
        return false;

    // CAS 루프: IOCount > 0일 때만 +1. 0이면 즉시 실패.
    while (true)
    {
        LONG current = session->_ioCount;
        if (current <= 0)
            return false;

        if (InterlockedCompareExchange(&session->_ioCount, current + 1, current) == current)
            break;
    }

    // CAS 성공 후 releaseFlag 재확인.
    // IOCount >= 2이므로 세션 해제/재할당 불가
    if (session->_disconnecting == TRUE)
    {
        IOCountDecrement(session);
        return false;
    }

    return true;
}

// IOCount를 1 감소시킨다. 0이 되면 ReleaseSession을 호출한다.
// AcquireSession의 CAS가 IOCount=0 세션 증가를 차단하므로, 0 도달 스레드는 유일하다.
void CIOCPServer::IOCountDecrement(CSession* session)
{
    if (!session)
        return;

    const LONG count = InterlockedDecrement(&session->_ioCount);
    if (count < 0)
    {
        // 언더플로 복구 — 0으로 리셋하지만 release 로직은 실행하지 않으므로
        // 이 세션의 인덱스는 영구히 반환되지 않는다 (세션 누수).
        // TODO: 크래시 로직 도입 후 여기서 즉시 중단하는 것이 안전함
        InterlockedExchange(&session->_ioCount, 0);
        LOG_ERROR_STREAM("[Error] IOCount underflow - SessionId: " << session->_sessionId);
        return;
    }

    if (count == 0)
    {
        ReleaseSession(session);
    }
}

// 세션 최종 정리 — IOCount==0 시점에 단일 스레드에서만 호출된다.
// 컨텐츠 알림, 소켓 종료, 인덱스 반환을 수행한다.
void CIOCPServer::ReleaseSession(CSession* session)
{
    // RequestDisconnectSession을 거치지 않고 IOCount==0에 도달한 비정상 경로 방어.
    // releaseFlag를 강제 설정하여 이후 RequestDisconnectSession 재진입을 차단한다.
    if (InterlockedExchange(&session->_disconnecting, TRUE) == FALSE)
    {
        LOG_ERROR_STREAM("[Error] ReleaseSession without RequestDisconnectSession - SessionId: " << session->_sessionId);
    }

    const int64_t sessionId = session->_sessionId;

    // 컨텐츠 알림 — IOCount==0이므로 이후 RECEIVED 이벤트 불가. 이벤트 순서 보장.
    // 서버 종료 중(_running==false)에는 알림 생략.
    if (sessionId != 0 && _running == TRUE)
    {
        switch (_architectureType)
        {
        case ServerArchitectureType::Centralized:
            PushNetworkEvent(NetworkEvent(NetworkEvent::Type::DISCONNECTED, sessionId));
            break;

        default:
            break;
        }
    }

    // 타이밍 휠에서 세션 제거 (이중 만료 방지)
    _timingWheel->RequestUnregister(CSession::ExtractIndex(sessionId), sessionId);

    session->Close();

    if (sessionId != 0)
    {
        // 세션 지표 기록
        _monitor._sessionDestroyed.fetch_add(1, std::memory_order_relaxed);
        _monitor._sessionCount.fetch_sub(1, std::memory_order_relaxed);

        _availableIndices.Push(CSession::ExtractIndex(sessionId));
    }
}

bool CIOCPServer::RequestDisconnectSession(int64_t sessionId)
{
    // 3단계 Session ABA 검증
    // 1단계: index로 세션 조회 + sessionId 일치 확인
    auto session = FindSession(sessionId);
    if (!session)
        return false;

    // 2단계: IOCount pin
    if (!AcquireSession(session))
        return false;

    // 3단계: pin 후 sessionId 재확인 (FindSession ~ AcquireSession 사이 재할당 검출)
    if (session->_sessionId != sessionId)
    {
        IOCountDecrement(session);
        return false;
    }

    const bool disconnected = RequestDisconnectSession(session);
    IOCountDecrement(session);
    return disconnected;
}

// 타이밍 휠 타임아웃 콜백 — 타이머 스레드에서 호출된다.
// sessionId를 통해 ABA-safe 경로(FindSession → AcquireSession → sessionId 재검증)를 사용한다.
void CIOCPServer::OnSessionTimeout(void* context, int64_t sessionId)
{
    auto* server = static_cast<CIOCPServer*>(context);
    server->RequestDisconnectSession(sessionId);
}

// 여러 스레드에서 접근가능 — ref를 잡지 않으므로 반환된 포인터는 잠정적이다.
// 반드시 AcquireSession + sessionId 재확인 후 사용해야 한다.
CSession* CIOCPServer::FindSession(int64_t sessionId)
{
    uint16_t index = CSession::ExtractIndex(sessionId);

    if (index >= _sessions.size())
        return nullptr;

    auto& session = _sessions[index];
    if (!session)
        return nullptr;

    if (session->_disconnecting == FALSE && session->_sessionId == sessionId)
        return session.get();

    return nullptr;
}
