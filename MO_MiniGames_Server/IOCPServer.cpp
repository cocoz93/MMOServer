#include "IOCPServer.h"
#include <iostream>

extern void SignalProcessShutdown(); // main쪽에 정의된 함수

// CSession Implementation
CSession::CSession()
{
    Initialize(INVALID_SOCKET, 0);
}

void CSession::Initialize(SOCKET socket, int64_t sessionId)
{
    _socket = socket;
    _sessionId = sessionId;
    _valid.store(true); // 유효한 세션
    _sending.store(false);
    _recvQ.Clear();
    _sendQ.Clear();

    _recvOverlapped.operation = IOOperation::RECV;
    _recvOverlapped.sessionId = sessionId;
    _sendOverlapped.operation = IOOperation::SEND;
    _sendOverlapped.sessionId = sessionId;

    ZeroMemory(&_recvOverlapped.overlapped, sizeof(OVERLAPPED));
}

CSession::~CSession()
{
    Close();
}

void CSession::Close()
{
    // 소캣만 종료하고, 나머지는 할당할떄 초기화한다.
    if (_socket != INVALID_SOCKET)
    {
        closesocket(_socket);
        _socket = INVALID_SOCKET;
    }
}

// CIOCPServer Implementation
CIOCPServer::CIOCPServer(int port, int maxClients, ServerArchitectureType type)
    : _port(port)
    , _maxClients(maxClients)
    , _architectureType(type)
    , _running(false)
    , _sessionIdCounter(1)  // 0은 사용하지 않음
    , _listenSocket(INVALID_SOCKET)
    , _iocpHandle(NULL)
{
    // 멤버 변수만 초기화
}

CIOCPServer::~CIOCPServer()
{
    Disconnect();
    SignalProcessShutdown(); //메인 스레드 종료
}


bool CIOCPServer::Start()
{
    // Vector 초기화 및 Session 동접자만큼 확보
    _sessions.resize(_maxClients);
    for (uint16_t i = 0; i < _maxClients; ++i)
    {
        // INVALID_SOCKET과 0 세션ID로 미리 생성
        _sessions[i] = std::make_unique<CSession>();
    }

    // 빈 인덱스 큐 초기화 (0번부터 maxClients-1까지)
    for (uint16_t i = 0; i < _maxClients; ++i)
    {
        _availableIndices.push(i);
    }

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cerr << "WSAStartup failed" << std::endl;
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

    _running = true;

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
    case ServerArchitectureType::EchoTest: std::cout << "EchoTest"; break;
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
        std::cerr << "WSASocket failed: " << WSAGetLastError() << std::endl;
        return false;
    }

    SOCKADDR_IN serverAddr;
    ZeroMemory(&serverAddr, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(_port);

    if (bind(_listenSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
    {
        std::cerr << "bind failed: " << WSAGetLastError() << std::endl;
        closesocket(_listenSocket);
        return false;
    }

    if (listen(_listenSocket, SOMAXCONN) == SOCKET_ERROR)
    {
        std::cerr << "listen failed: " << WSAGetLastError() << std::endl;
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
        std::cerr << "BindIOCP failed: " << GetLastError() << std::endl;
        return false;
    }
    return true;
}

void CIOCPServer::ReleaseSession()
{
    std::lock_guard<std::mutex> lock(_pendingDisconMtx);
    while (!_pendingDisconStack.empty())
    {
        uint64_t sessionid = _pendingDisconStack.top();
        _pendingDisconStack.pop();

        auto session = FindSession(sessionid);
        if (session)
        {
            session->Close();
            _availableIndices.push(CSession::ExtractIndex(sessionid));
        }
    }
}

// 즉시 RST 전송(강제 종료)
void CIOCPServer::Disconnect()
{
    if (!_running)
    {
        return;
    }

    _running = false;

    // 모든 세션 강제 종료 (SO_LINGER{on,0} -> abortive close (RST))
    {
        LINGER lingerOpt;
        lingerOpt.l_onoff = 1;
        lingerOpt.l_linger = 0;

        for (auto& session : _sessions)
        {
            if (session && session->_valid.exchange(false))  // exchange로 소유권 획득
            {
                if (session && session->_socket != INVALID_SOCKET)
                {
                    closesocket(session->_socket);
                    session->_valid.store(false);
                }
            }
        }
        
        // Vector 초기화
        //std::fill(_sessions.begin(), _sessions.end(), nullptr);
        
        // 빈 인덱스 큐 재초기화
        std::queue<uint16_t> empty;
        std::swap(_availableIndices, empty);
        for (uint16_t i = 0; i < _maxClients; ++i)
        {
            _availableIndices.push(i);
        }
    }

    // Listen 소켓도 닫음 (정상적으로 닫아도 무방)
    if (_listenSocket != INVALID_SOCKET)
    {
        closesocket(_listenSocket);
        _listenSocket = INVALID_SOCKET;
    }

    // IOCP 워커 스레드 깨우기
    if (_iocpHandle != NULL)
    {
        for (size_t i = 0; i < _workerThreads.size(); ++i)
        {
            PostQueuedCompletionStatus(_iocpHandle, 0, 0, nullptr);
        }
    }

    // 스레드 종료 대기
    for (auto& thread : _workerThreads)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }

    if (_acceptThread.joinable())
    {
        _acceptThread.join();
    }

    if (_iocpHandle != NULL)
    {
        CloseHandle(_iocpHandle);
        _iocpHandle = NULL;
    }

    WSACleanup();
}

// 윈도우 accept에는 timeout 기능이 없음.
// (그럴일은 없겠지만) 무한히 block걸려도 문제없음
void CIOCPServer::AcceptThread()
{
    while (_running)
    {
        SOCKADDR_IN clientAddr;
        int addrLen = sizeof(clientAddr);

        SOCKET clientSocket = accept(_listenSocket, (SOCKADDR*)&clientAddr, &addrLen);

        // 깨어났으면 해제할 세션있는지 체크
        ReleaseSession();


        if (clientSocket == INVALID_SOCKET)
        {
            if (_running)
            {
                std::cerr << "accept failed: " << WSAGetLastError() << std::endl;
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
    if (_availableIndices.empty())
    {
        std::cerr << "[Error] No free session index available" << std::endl;
        closesocket(clientSocket);
        return;
    }

    // 빈 인덱스 가져오기
    uint16_t index = _availableIndices.front();
    _availableIndices.pop();

    // 고유 ID 생성 (하위 48비트만 사용)
    int64_t uniqueId = (_sessionIdCounter.fetch_add(1)) & CSession::SESSION_UNIQUE_MASK;
    
    // SessionID 생성 [16bit Index][48bit UniqueID]
    int64_t sessionId = CSession::MakeSessionId(index, uniqueId);

    // session 초기화. 사용가능한 상태가 됨
    _sessions[index]->Initialize(clientSocket, sessionId);
    
    // IOCP의 CompletionKey는 단순 식별자 역할이므로, 세션 소유권을 갖지 않는다.
    if (!BindIOCP(clientSocket, (ULONG_PTR)_sessions[index].get()))
    {
        std::cerr << "Failed to bind client socket to IOCP" << std::endl;
        _sessions[index]->Close();
        _availableIndices.push(index);
        closesocket(clientSocket);
        return;
    }

    // 컨텐츠쪽 전달 
    switch (_architectureType)
    {
    case ServerArchitectureType::EchoTest:
        // 따로 전달 없음
        break;
    case ServerArchitectureType::Centralized: // 큐에 넣어서 별도 스레드로 전달
        PushNetworkEvent(NetworkEvent(NetworkEvent::Type::CONNECTED, sessionId));
        break;

    default:
        break;
    }

    //std::cout << "Client connected - SessionId: " << sessionId << " (Index: " << index << ", UniqueID: " << uniqueId << ")" << std::endl;

    // 첫 Recv 요청
    PostRecv(_sessions[index].get());
}

void CIOCPServer::WorkerThread()
{
    while (_running)
    {
        DWORD bytesTransferred = 0;
        ULONG_PTR completionKey = 0;
        OVERLAPPED* overlapped = nullptr;

        BOOL result = GetQueuedCompletionStatus(_iocpHandle, &bytesTransferred,
            &completionKey, &overlapped, INFINITE);

        if (!_running)
            break;

        // 에러 또는 연결 종료 (에러와 연결종료 상황을 분류하지 않음)
        if (result == FALSE || bytesTransferred == 0)
        {
            if (overlapped != nullptr)
            {
                auto session = reinterpret_cast<CSession*>(completionKey);
                if (session)
                {
                    DisconnectSessionInternal(session);
                }
            }
            continue;
        }

        auto overlappedEx = reinterpret_cast<CSession::OverlappedEx*>(overlapped);
        auto session = reinterpret_cast<CSession*>(completionKey);

        //overlappedEx, session이 nullptr인 상황은 있을 수 없음
        if(overlappedEx == nullptr || session == nullptr)
        {
            std::cerr << "[Error] Invalid overlappedEx or session pointer in WorkerThread" << std::endl;
            continue;
        }

        // 이미 연결이 끊긴 세션 
        if (!session->_valid.load())
        {
            continue;
        }

        // ABA 방지: 세션ID 일치 여부 확인
        if (overlappedEx->sessionId != session->_sessionId)
        {
            // 세션이 재사용된 경우, 해당 I/O 무시
            continue;
        }


        switch (overlappedEx->operation)
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
}

// Recv 완료 통지 처리
void CIOCPServer::ProcessRecv(CSession* session, DWORD bytesTransferred)
{
    // 방어 로직
    if (bytesTransferred == 0)
    {
        std::cerr << "[Warning] ProcessRecv called with bytesTransferred == 0 - SessionId: " << session->_sessionId << std::endl;
        DisconnectSessionInternal(session);
        return;
    }

    // 링버퍼 쓰기 포인터 이동 
    size_t movedSize = session->_recvQ.MoveWritePtr(bytesTransferred);
    if (movedSize != bytesTransferred)
    {
        std::cerr << "[Error] Recv buffer overflow - SessionId: " << session->_sessionId 
                  << ", Expected: " << bytesTransferred << ", Moved: " << movedSize << std::endl;
        DisconnectSessionInternal(session);
        return;
    }


    // 패킷 파싱
    ParsePackets(session);

    // 다음 Recv 요청
    PostRecv(session);
}

// PostRecv: 링버퍼 기반 WSARecv 요청
void CIOCPServer::PostRecv(CSession* session)
{
    if (!session || !session->_valid.load())
    {
        return;
    }

    // OverlappedEx 초기화
    ZeroMemory(&session->_recvOverlapped.overlapped, sizeof(OVERLAPPED));
    session->_recvOverlapped.sessionId = session->_sessionId;
    session->_recvOverlapped.operation = IOOperation::RECV;

    // 링버퍼에서 쓰기 가능한 공간 확보
    char* writePtr = session->_recvQ.GetWritePtr();
    size_t directWriteSize = session->_recvQ.GetDirectWriteSize();

    // 지역 WSABUF 배열 선언
    WSABUF wsaBuf[2];
    int bufCount = 0;

    // 첫 번째 버퍼: 쓰기 포인터부터 버퍼 끝까지
    if (directWriteSize > 0)
    {
        wsaBuf[bufCount].buf = writePtr;
        wsaBuf[bufCount].len = static_cast<ULONG>(directWriteSize);
        bufCount++;

        // 두 번째 버퍼: 버퍼가 랩되는 경우
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
        // 링버퍼가 가득 찬 경우 - 연결 종료
        std::cerr << "[Error] Recv buffer full - SessionId: " << session->_sessionId << std::endl;
        DisconnectSessionInternal(session);
        return;
    }

    DWORD flags = 0;
    DWORD recvBytes = 0;

    int result = WSARecv(session->_socket, wsaBuf, bufCount, &recvBytes, &flags,
        &session->_recvOverlapped.overlapped, NULL);

    if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
    {
        std::cerr << "[Error] WSARecv failed: " << WSAGetLastError() << " - SessionId: " << session->_sessionId << std::endl;
        DisconnectSessionInternal(session);
    }
}

// ParsePackets: 링버퍼에서 완성된 패킷 추출 및 처리
void CIOCPServer::ParsePackets(CSession* session)
{
    while (true)
    {
        size_t dataSize = session->_recvQ.GetDataSize();

        // 1. 헤더 크기 체크
        if (dataSize < sizeof(MsgHeader))
        {
            break; // 데이터 부족
        }

        // 2. 헤더 peek
        MsgHeader header;
        size_t peekedSize = session->_recvQ.Peek(&header, sizeof(MsgHeader));
        if (peekedSize != sizeof(MsgHeader))
        {
            break; // peek 실패
        }

        // 에코 테스트 전용
        if (_architectureType == ServerArchitectureType::EchoTest)
        {
            header.size += sizeof(MsgHeader); // 에코 테스트용 보정
        }

        // 3. 패킷 크기 검증
        if (header.size < MIN_PACKET_SIZE || header.size > MAX_PACKET_SIZE)
        {
            std::cerr << "[Error] Invalid packet size: " << header.size 
                      << " - SessionId: " << session->_sessionId << std::endl;
            DisconnectSessionInternal(session);
            return;
        }

        // 4. 전체 패킷이 수신되었는지 확인
        if (dataSize < header.size)
        {
            break; // 데이터 부족 - 다음 Recv 대기
        }

        // 5. 완성된 패킷 추출 (스택 버퍼 사용, heap 할당 회피)
        std::array<char, MAX_PACKET_SIZE> packetBuffer;
        size_t dequeuedSize = session->_recvQ.Dequeue(packetBuffer.data(), header.size);
        if (dequeuedSize != header.size)
        {
            std::cerr << "[Error] Packet dequeue failed - SessionId: " << session->_sessionId << std::endl;
            DisconnectSessionInternal(session);
            return;
        }

        // 6. 컨텐츠쪽 전달 또는 처리
        switch (_architectureType)
        {
        case ServerArchitectureType::EchoTest:
            EchoTestSend(session, packetBuffer.data(), header.size);
                break;
        case ServerArchitectureType::Centralized: // 큐에 넣어서 별도 스레드로 전달
            PushNetworkEvent(NetworkEvent(NetworkEvent::Type::RECEIVED, 
                session->_sessionId, packetBuffer.data(), header.size));
            break;

        default:
            break;
        }
        
    }
}

// Send 완료 통지 처리
void CIOCPServer::ProcessSend(CSession* session, DWORD bytesTransferred)
{
    if (!session || !session->_valid.load())
    {
        return;
    }

    // SendQ에서 송신한 만큼 Consume
    size_t consumed = session->_sendQ.Consume(bytesTransferred);
    if (consumed != bytesTransferred)
    {
        std::cerr << "[Error] Send consume mismatch - SessionId: " << session->_sessionId
                  << ", Expected: " << bytesTransferred << ", Consumed: " << consumed << std::endl;
        DisconnectSessionInternal(session);
        return;
    }

    // ★ 수정: 남은 데이터 확인 후 연속 송신 또는 플래그 해제
    auto sendInfo = session->_sendQ.GetSendInfo();
    
    if (sendInfo.dataSize > 0)
    {
        // 남은 데이터가 있으면 _sending = true 유지한 채 바로 WSASend
        ZeroMemory(&session->_sendOverlapped.overlapped, sizeof(OVERLAPPED));
        session->_sendOverlapped.sessionId = session->_sessionId;
        session->_sendOverlapped.operation = IOOperation::SEND;

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

        if (bufCount > 0)
        {
            DWORD sendBytes = 0;
            int result = WSASend(session->_socket, wsaBuf, bufCount, &sendBytes, 0,
                &session->_sendOverlapped.overlapped, NULL);

            if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
            {
                std::cerr << "[Error] WSASend failed: " << WSAGetLastError()
                          << " - SessionId: " << session->_sessionId << std::endl;
                session->_sending.store(false);
                DisconnectSessionInternal(session);
            }
            return;
        }
    }

    // 보낼 데이터가 없을 때만 플래그 해제
    session->_sending.store(false);
    
    // Double-check: 플래그 해제 직후 다시 확인 (다른 스레드가 Enqueue했을 수 있음)
    // https://www.notion.so/IOCP-2e216a0b9f5980718fbbe6d70d9d537f?source=copy_link#2e216a0b9f5980259bece18c0876edf8
    if (session->_sendQ.GetDataSize() > 0)
    {
        PostSend(session);
    }
}

// Send는 1회로 제한
// https://www.notion.so/C-IOCP-2e216a0b9f5980718fbbe6d70d9d537f?source=copy_link#2e216a0b9f5980a183ecccce201aff54

// PostSend: SendQ에서 데이터를 꺼내 WSASend 호출
void CIOCPServer::PostSend(CSession* session)
{
    if (!session || !session->_valid.load())
        return;


    // 사이즈 체크보다 플래그 변경처리가 우선되어야 함
    // https://www.notion.so/IOCP-2e216a0b9f5980718fbbe6d70d9d537f?source=copy_link#2e216a0b9f5980e0bff1d298912f7b60
    if (true == session->_sending.exchange(true))
        return;

    // ★ 한 번의 락으로 일관된 상태 획득
    auto sendInfo = session->_sendQ.GetSendInfo();
    
    if (sendInfo.dataSize == 0)
    {
        session->_sending.store(false);
        return;
    }

    ZeroMemory(&session->_sendOverlapped.overlapped, sizeof(OVERLAPPED));
    session->_sendOverlapped.sessionId = session->_sessionId;
    session->_sendOverlapped.operation = IOOperation::SEND;

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
        // 데이터가 없는 경우 (방어 코드)
        // TODO : 이쪽으로 들어오는 경우는 없는지 확인

        session->_sending.store(false);
        return;
    }

    DWORD sendBytes = 0;
    if (wsaBuf[0].len == 0 && wsaBuf[1].len == 0)
    {
        session->_sending.store(false);
        return;
    }

    int result = WSASend(session->_socket, wsaBuf, bufCount, &sendBytes, 0,
        &session->_sendOverlapped.overlapped, NULL);

    if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
    {
        std::cerr << "[Error] WSASend failed: " << WSAGetLastError() 
                  << " - SessionId: " << session->_sessionId << std::endl;
        session->_sending.store(false);
        DisconnectSessionInternal(session);
    }
}

// 게임 로직 레이어가 사용할 인터페이스
// 송신 요청: SendQ에 데이터 Enqueue 후 송신 시작
void CIOCPServer::RequestSendMsg(int64_t sessionId, const char* data, int length)
{
    auto session = FindSession(sessionId);
    if (!session || !session->_valid.load())
    {
        return;
    }

    // SendQ에 데이터 Enqueue
    size_t enqueued = session->_sendQ.Enqueue(data, length);
    if (enqueued != length)
    {
        std::cerr << "[Error] Send buffer overflow - SessionId: " << sessionId 
                  << ", Requested: " << length << ", Enqueued: " << enqueued << std::endl;
        DisconnectSessionInternal(session);
        return;
    }

    PostSend(session);
}


void CIOCPServer::EchoTestSend(CSession* session, const char* data, size_t length)
{
    // 에코 테스트: 받은 패킷을 그대로 돌려보냄
    RequestSendMsg(session->_sessionId, data, length);
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

// 실제 할당, 해제는 acceptthread에서.
// 결국 모든 세션해제는 이함수를 통함
bool CIOCPServer::DisconnectSessionInternal(CSession* session)
{
    if (!session)
        return false;

    // 이미 해제 진행중이거나 해제된 세션
    if (!session->_valid.exchange(false))
        return false;

    // closesocket
    if (session->_socket != INVALID_SOCKET)
    {
        closesocket(session->_socket);
        session->_socket = INVALID_SOCKET;
    }

    session->_sending.store(false);

    // AcceptThread에서 세션 객체 정리 (인덱스 반환)
    {
        std::lock_guard<std::mutex> lock(_pendingDisconMtx);
        _pendingDisconStack.push(session->_sessionId);
    }

    // 해제요청이 끝났다면 컨텐츠 쪽에 전달해준다.
    switch (_architectureType)
    {
    case ServerArchitectureType::EchoTest:
        // 따로 전달 없음
        break;

    case ServerArchitectureType::Centralized:
            PushNetworkEvent(NetworkEvent(NetworkEvent::Type::DISCONNECTED, session->_sessionId));
        break;

    default:
        break;
    }

    return true;
}




// 게임 로직 레이어가 사용할 인터페이스
bool CIOCPServer::RequestDisconnectSession(int64_t sessionId)
{
    auto session = FindSession(sessionId);
    if (!session)
        return false;

    if (!DisconnectSessionInternal(session))
        return false;

    return true;
}

// 여러 스레드에서 접근가능 !!!
// session객체 건드릴때 주의
CSession* CIOCPServer::FindSession(int64_t sessionId)
{
    uint16_t index = CSession::ExtractIndex(sessionId);
    int64_t uniqueId = CSession::ExtractUniqueId(sessionId);

    if (index >= _sessions.size())
        return nullptr;

    auto& session = _sessions[index];
    if (session && CSession::ExtractUniqueId(session->_sessionId) == uniqueId)
        return session.get();

    return nullptr;
}
