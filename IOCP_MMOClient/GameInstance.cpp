#include "GameInstance.h"
#include "../Shared/Common/ErrorLog.h"
#include <iostream>
#include <chrono>
#define NOMINMAX
#include <Windows.h>
#include <fcntl.h>
#include <io.h>
#include <conio.h>

CGameInstance::CGameInstance()
    : _running(false)
    , _keyPressed{ false, false, false, false }
    , _chatMode(false)
{
}

CGameInstance::~CGameInstance()
{
    Shutdown();
}

bool CGameInstance::Initialize()
{
    _network.SetGameInstance(this);

    // 유니코드 출력 설정 (렌더러 Init 전에 설정)
    _setmode(_fileno(stdout), _O_U16TEXT);
    _setmode(_fileno(stderr), _O_U16TEXT);

    _renderer.Init();

    _running = true;
    return true;
}

void CGameInstance::Run()
{
    // IP/포트 입력
    std::wstring serverIp;
    int port;

    std::wcout << L"Enter server IP (default: 127.0.0.1): ";
    std::getline(std::wcin, serverIp);
    if (serverIp.empty())
    {
        serverIp = L"127.0.0.1";
    }

    std::wcout << L"Enter server port (default: 6000): ";
    std::wstring portStr;
    std::getline(std::wcin, portStr);
    if (portStr.empty())
    {
        port = 6000;
    }
    else
    {
        port = std::stoi(portStr);
    }

    if (!ConnectToServer(std::string(serverIp.begin(), serverIp.end()), port))
    {
        WLOG_ERROR_STREAM(L"Failed to connect to server.");
        return;
    }

    // 게임 루프 진입
    GameLoop();
}

void CGameInstance::Shutdown()
{
    _running = false;
    _network.Disconnect();
}

bool CGameInstance::ConnectToServer(const std::string& serverIp, int port)
{
    return _network.Connect(serverIp, port);
}

// ==========================================================================
// 게임 루프 (25fps)
// ==========================================================================

void CGameInstance::GameLoop()
{
    using Clock = std::chrono::steady_clock;

    auto prevTime = Clock::now();

    while (_running && _network.IsConnected())
    {
        auto frameStart = Clock::now();

        // deltaTime 계산
        float deltaTime = std::chrono::duration<float>(frameStart - prevTime).count();
        prevTime = frameStart;

        // 1) 네트워크 이벤트 소비
        ProcessNetworkEvents();

        // 2) 키보드 입력 처리
        if (_chatMode)
            ProcessChatInput();
        else
            ProcessInput();

        // 3) 클라이언트 예측 이동 갱신
        _playerManager.UpdateMovingPlayers(deltaTime);

        // 4) 렌더링
        _renderer.RenderFrame(
            _playerManager.GetMyPlayer(),
            _playerManager.GetOtherPlayers(),
            _chatLog,
            _chatInput,
            _chatMode);

        // 5) 프레임 제한
        auto frameEnd = Clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(frameEnd - frameStart);
        int sleepMs = FRAME_INTERVAL_MS - static_cast<int>(elapsed.count());
        if (sleepMs > 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
        }
    }
}

// ==========================================================================
// 네트워크 이벤트 소비 (게임 루프 스레드)
// ==========================================================================

void CGameInstance::ProcessNetworkEvents()
{
    ClientNetworkEvent event;
    while (_eventQueue.Pop(event))
    {
        switch (event.type)
        {
        case ClientNetworkEvent::Type::CREATE_MY_PLAYER:
            _playerManager.SetMyPlayer(
                event.playerId, event.x, event.y,
                static_cast<Direction>(event.direction));
            AddChatMessage(L"[System] Connected to server.");
            break;

        case ClientNetworkEvent::Type::CREATE_OTHER_PLAYER:
            _playerManager.AddOtherPlayer(
                event.playerId, event.x, event.y,
                static_cast<Direction>(event.direction),
                static_cast<MoveState>(event.moveState));
            break;

        case ClientNetworkEvent::Type::DELETE_PLAYER:
            _playerManager.RemovePlayer(event.playerId);
            break;

        case ClientNetworkEvent::Type::MOVE_START:
        {
            ClientPlayer* player = _playerManager.FindPlayer(event.playerId);
            if (player)
            {
                player->x = event.x;
                player->y = event.y;
                player->direction = static_cast<Direction>(event.direction);
                player->moveState = MoveState::MOVING;
            }
            break;
        }

        case ClientNetworkEvent::Type::MOVE_STOP:
        {
            // 내 캐릭터인 경우 (서버 경계 클램핑 등)
            ClientPlayer* me = _playerManager.GetMyPlayer();
            if (me && me->playerId == event.playerId)
            {
                me->x = event.x;
                me->y = event.y;
                me->direction = static_cast<Direction>(event.direction);
                me->moveState = MoveState::IDLE;
                break;
            }

            ClientPlayer* player = _playerManager.FindPlayer(event.playerId);
            if (player)
            {
                player->x = event.x;
                player->y = event.y;
                player->direction = static_cast<Direction>(event.direction);
                player->moveState = MoveState::IDLE;
            }
            break;
        }

        case ClientNetworkEvent::Type::CHAT:
        {
            wchar_t buf[128];
            swprintf_s(buf, L"[Player %d] %s", event.playerId, event.chatMessage);
            AddChatMessage(buf);
            break;
        }

        case ClientNetworkEvent::Type::SYNC_POSITION:
        {
            ClientPlayer* me = _playerManager.GetMyPlayer();
            if (me)
            {
                me->x = event.x;
                me->y = event.y;
            }
            break;
        }

        case ClientNetworkEvent::Type::ZONE_CHANGE_OK:
        {
            _playerManager.Clear();
            _playerManager.SetMyPlayer(
                event.playerId, event.x, event.y, Direction::NONE);

            wchar_t buf[128];
            swprintf_s(buf, L"[System] Zone changed: Map=%d Channel=%d",
                        event.mapId, event.channelIndex);
            AddChatMessage(buf);
            break;
        }

        case ClientNetworkEvent::Type::ZONE_CHANGE_FAIL:
        {
            const wchar_t* reason = (event.reason == 0) ? L"Map not found" : L"All channels full";
            wchar_t buf[128];
            swprintf_s(buf, L"[System] Zone change failed: %s", reason);
            AddChatMessage(buf);
            break;
        }

        case ClientNetworkEvent::Type::ERROR_MSG:
        {
            wchar_t buf[300];
            // char → wchar_t 변환
            wchar_t wMsg[256];
            MultiByteToWideChar(CP_UTF8, 0, event.errorMessage, -1, wMsg, 256);
            swprintf_s(buf, L"[Error] %s", wMsg);
            AddChatMessage(buf);
            break;
        }
        }
    }
}

// ==========================================================================
// 키보드 입력 처리 — 일반 모드 (이동)
// ==========================================================================

void CGameInstance::ProcessInput()
{
    // 현재 프레임의 키 상태 조사
    bool currentKeys[4];
    currentKeys[0] = (GetAsyncKeyState(VK_UP) & 0x8000) != 0;
    currentKeys[1] = (GetAsyncKeyState(VK_DOWN) & 0x8000) != 0;
    currentKeys[2] = (GetAsyncKeyState(VK_LEFT) & 0x8000) != 0;
    currentKeys[3] = (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0;

    // ESC 종료
    if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)
    {
        _running = false;
        return;
    }

    // Enter → 채팅 모드 진입
    if (GetAsyncKeyState(VK_RETURN) & 0x0001)
    {
        // 이동 중이면 정지 후 진입
        ClientPlayer* me = _playerManager.GetMyPlayer();
        if (me && me->moveState == MoveState::MOVING)
        {
            _network.SendMoveStop(
                static_cast<uint8_t>(me->direction), me->x, me->y);
            me->moveState = MoveState::IDLE;
        }

        // 키 상태 초기화 (채팅 모드 해제 시 잔여 상태 방지)
        for (int i = 0; i < 4; ++i)
            _keyPressed[i] = false;

        _chatMode = true;
        _chatInput.clear();
        return;
    }

    // 내 캐릭터가 아직 생성되지 않았으면 입력 무시
    if (!_playerManager.HasMyPlayer())
        return;

    // 키 누름 감지
    for (int i = 0; i < 4; ++i)
    {
        if (currentKeys[i] && !_keyPressed[i])
        {
            ClientPlayer* me = _playerManager.GetMyPlayer();
            if (me->moveState == MoveState::MOVING)
            {
                _network.SendMoveStop(
                    static_cast<uint8_t>(me->direction), me->x, me->y);
                me->moveState = MoveState::IDLE;
            }

            uint8_t dir = static_cast<uint8_t>(i + 1);
            _network.SendMoveStart(dir);

            me->direction = static_cast<Direction>(dir);
            me->moveState = MoveState::MOVING;
        }
    }

    // 키 뗌 감지
    for (int i = 0; i < 4; ++i)
    {
        if (!currentKeys[i] && _keyPressed[i])
        {
            ClientPlayer* me = _playerManager.GetMyPlayer();
            uint8_t dir = static_cast<uint8_t>(i + 1);

            if (me->moveState == MoveState::MOVING &&
                me->direction == static_cast<Direction>(dir))
            {
                _network.SendMoveStop(dir, me->x, me->y);
                me->moveState = MoveState::IDLE;
            }
        }
    }

    // 상태 갱신
    for (int i = 0; i < 4; ++i)
    {
        _keyPressed[i] = currentKeys[i];
    }
}

// ==========================================================================
// 키보드 입력 처리 — 채팅 모드
// ==========================================================================

void CGameInstance::ProcessChatInput()
{
    while (_kbhit())
    {
        int ch = _getch();

        // 확장 키(방향키, F1~F12 등)는 2바이트 시퀀스 — 스캔코드 소비 후 무시
        if (ch == 0 || ch == 0xE0)
        {
            _getch();
            continue;
        }

        if (ch == 27) // ESC → 채팅 취소
        {
            _chatMode = false;
            _chatInput.clear();
            return;
        }

        if (ch == 13) // Enter → 전송
        {
            if (!_chatInput.empty())
            {
                if (_chatInput[0] == L'/')
                {
                    HandleChatCommand(_chatInput);
                }
                else
                {
                    _network.SendChat(_chatInput.c_str());
                }
            }
            _chatMode = false;
            _chatInput.clear();
            return;
        }

        if (ch == 8) // Backspace
        {
            if (!_chatInput.empty())
            {
                _chatInput.pop_back();
            }
            continue;
        }

        // 일반 문자 입력 (ASCII 범위)
        if (ch >= 32 && ch < 127)
        {
            if (_chatInput.size() < CHAT_MSG_MAX_LEN - 1)
            {
                _chatInput += static_cast<wchar_t>(ch);
            }
        }
    }
}

// ==========================================================================
// S2C 패킷 핸들러 — 이벤트 큐에 Push (수신 스레드에서 호출)
// ==========================================================================

void CGameInstance::OnCreateMyPlayer(const MSG_S2C_CREATE_MY_PLAYER* msg)
{
    ClientNetworkEvent event{};
    event.type = ClientNetworkEvent::Type::CREATE_MY_PLAYER;
    event.playerId = msg->playerId;
    event.x = msg->x;
    event.y = msg->y;
    event.direction = msg->direction;
    _eventQueue.Push(std::move(event));
}

void CGameInstance::OnCreateOtherPlayer(const MSG_S2C_CREATE_OTHER_PLAYER* msg)
{
    ClientNetworkEvent event{};
    event.type = ClientNetworkEvent::Type::CREATE_OTHER_PLAYER;
    event.playerId = msg->playerId;
    event.x = msg->x;
    event.y = msg->y;
    event.direction = msg->direction;
    event.moveState = msg->moveState;
    _eventQueue.Push(std::move(event));
}

void CGameInstance::OnDeletePlayer(const MSG_S2C_DELETE_PLAYER* msg)
{
    ClientNetworkEvent event{};
    event.type = ClientNetworkEvent::Type::DELETE_PLAYER;
    event.playerId = msg->playerId;
    _eventQueue.Push(std::move(event));
}

void CGameInstance::OnMoveStart(const MSG_S2C_MOVE_START* msg)
{
    ClientNetworkEvent event{};
    event.type = ClientNetworkEvent::Type::MOVE_START;
    event.playerId = msg->playerId;
    event.x = msg->x;
    event.y = msg->y;
    event.direction = msg->direction;
    _eventQueue.Push(std::move(event));
}

void CGameInstance::OnMoveStop(const MSG_S2C_MOVE_STOP* msg)
{
    ClientNetworkEvent event{};
    event.type = ClientNetworkEvent::Type::MOVE_STOP;
    event.playerId = msg->playerId;
    event.x = msg->x;
    event.y = msg->y;
    event.direction = msg->direction;
    _eventQueue.Push(std::move(event));
}

void CGameInstance::OnChat(const MSG_S2C_CHAT* msg)
{
    ClientNetworkEvent event{};
    event.type = ClientNetworkEvent::Type::CHAT;
    event.playerId = msg->playerId;
    wcsncpy_s(event.chatMessage, msg->message, 63);
    _eventQueue.Push(std::move(event));
}

void CGameInstance::OnSyncPosition(const MSG_S2C_SYNC_POSITION* msg)
{
    ClientNetworkEvent event{};
    event.type = ClientNetworkEvent::Type::SYNC_POSITION;
    event.x = msg->x;
    event.y = msg->y;
    _eventQueue.Push(std::move(event));
}

void CGameInstance::OnZoneChangeOk(const MSG_S2C_ZONE_CHANGE_OK* msg)
{
    ClientNetworkEvent event{};
    event.type = ClientNetworkEvent::Type::ZONE_CHANGE_OK;
    event.playerId = msg->playerId;
    event.x = msg->x;
    event.y = msg->y;
    event.mapId = msg->mapId;
    event.channelIndex = msg->channelIndex;
    _eventQueue.Push(std::move(event));
}

void CGameInstance::OnZoneChangeFail(const MSG_S2C_ZONE_CHANGE_FAIL* msg)
{
    ClientNetworkEvent event{};
    event.type = ClientNetworkEvent::Type::ZONE_CHANGE_FAIL;
    event.reason = msg->reason;
    _eventQueue.Push(std::move(event));
}

void CGameInstance::OnError(const MSG_S2C_ERROR* msg)
{
    ClientNetworkEvent event{};
    event.type = ClientNetworkEvent::Type::ERROR_MSG;
    strncpy_s(event.errorMessage, msg->message, 255);
    _eventQueue.Push(std::move(event));
}

// ==========================================================================
// 채팅 로그 관리
// ==========================================================================

void CGameInstance::AddChatMessage(const std::wstring& msg)
{
    _chatLog.push_back(msg);

    // 최대 라인 수 초과 시 오래된 메시지 제거
    while (static_cast<int>(_chatLog.size()) > MAX_CHAT_LINES * 4)
    {
        _chatLog.erase(_chatLog.begin());
    }
}

// ==========================================================================
// 채팅 커맨드 처리
// ==========================================================================

void CGameInstance::HandleChatCommand(const std::wstring& command)
{
    if (command == L"/map" || command == L"/map list")
    {
        AddChatMessage(L"[System] Usage: /map <id> | /map random");
        return;
    }

    if (command.size() > 5 && command.substr(0, 5) == L"/map ")
    {
        std::wstring arg = command.substr(5);

        if (arg == L"random")
        {
            _network.SendZoneChange(-1);
            AddChatMessage(L"[System] Requesting random zone change...");
            return;
        }

        try
        {
            int32_t mapId = std::stoi(arg);
            _network.SendZoneChange(mapId);

            wchar_t buf[128];
            swprintf_s(buf, L"[System] Requesting zone change to map %d...", mapId);
            AddChatMessage(buf);
        }
        catch (...)
        {
            AddChatMessage(L"[System] Invalid map ID. Usage: /map <id> | /map random");
        }
        return;
    }

    AddChatMessage(L"[System] Unknown command. Available: /map");
}
