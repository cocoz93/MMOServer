#include "GameInstance.h"
#include "../Shared/Common/ErrorLog.h"
#include <iostream>
#define NOMINMAX
#include <Windows.h>
#include <fcntl.h>
#include <io.h>
#include <conio.h>

CGameInstance::CGameInstance()
    : _running(false)
{
}

CGameInstance::~CGameInstance()
{
    Shutdown();
}

bool CGameInstance::Initialize()
{
    _network.SetGameInstance(this); // 여기서 연결
    _room.SetNetwork(&_network);

    // 콘솔 창 설정
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hConsole != INVALID_HANDLE_VALUE)
    {
        COORD bufferSize = { 200, 50 };
        SetConsoleScreenBufferSize(hConsole, bufferSize);

        SMALL_RECT windowSize = { 0, 0, 199, 49 };
        SetConsoleWindowInfo(hConsole, TRUE, &windowSize);
    }

    // 유니코드 출력 설정
    _setmode(_fileno(stdout), _O_U16TEXT);
    _setmode(_fileno(stderr), _O_U16TEXT);

    _running = true;
    return true;
}

void CGameInstance::Run()
{
    int selectedMenu = ShowMainMenuWithSelection();

    if (1 == selectedMenu)
    {
        std::wcout << L"Single Play preparing..." << std::endl;
        return;
    }
    else if (3 == selectedMenu)
    {
        std::wcout << L"Exiting game..." << std::endl;
        return;
    }

    // 2번(MULTI PLAYER) 선택 시 서버 연결
    std::wstring serverIp;
    int port;

    std::wcout << L"\nEnter server IP (default: 127.0.0.1): ";
    std::getline(std::wcin, serverIp);
    if (serverIp.empty())
    {
        serverIp = L"127.0.0.1";
    }

    std::wcout << L"Enter server port (default: 9000): ";
    std::wstring portStr;
    std::getline(std::wcin, portStr);
    if (portStr.empty())
    {
        port = 9000;
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

    // 메인 루프
    while (_running && _network.IsConnected())
    {
        if (_room.IsInRoom())
        {
            ProcessRoomInput();
        }
        else
        {
            ProcessLobbyInput();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
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

void CGameInstance::OnRoomListReceived(const MSG_S2C_ROOM_LIST* msg, size_t msgSize)
{
    std::wcout << L"==================================" << std::endl;
    std::wcout << L"ROOM LIST (Total: " << msg->roomCount << L" rooms)" << std::endl;
    std::wcout << L"==================================" << std::endl;

    if (msg->roomCount == 0)
    {
        std::wcout << L"No rooms available." << std::endl;
        std::wcout << L"==================================" << std::endl;
        return;
    }

    const RoomInfo* roomInfoArray = reinterpret_cast<const RoomInfo*>(
        reinterpret_cast<const char*>(msg) + sizeof(MSG_S2C_ROOM_LIST));

    std::vector<RoomInfo> rooms;
    for (int32_t i = 0; i < msg->roomCount; ++i)
    {
        rooms.push_back(roomInfoArray[i]);
    }

    _room.DisplayRoomList(rooms);
}

void CGameInstance::OnRoomCreated(const MSG_S2C_ROOM_CREATED* msg)
{
    std::wcout << L"\n==================================" << std::endl;
    if (msg->success)
    {
        _room.OnRoomCreated(msg->roomId);
        std::wcout << L"Room created successfully!" << std::endl;
        std::wcout << L"Room ID: " << msg->roomId << std::endl;
    }
    else
    {
        std::wcout << L"Failed to create room." << std::endl;
    }
    std::wcout << L"==================================" << std::endl;
}

void CGameInstance::OnRoomJoined(const MSG_S2C_ROOM_JOINED* msg)
{
    std::wcout << L"\n==================================" << std::endl;
    if (msg->success)
    {
        _room.OnRoomJoined(msg->roomId);
        std::wcout << L"Joined room successfully!" << std::endl;
        std::wcout << L"Room ID: " << msg->roomId << std::endl;
    }
    else
    {
        std::wcout << L"Failed to join room." << std::endl;
    }
    std::wcout << L"==================================" << std::endl;
}

void CGameInstance::OnRoomLeft(const MSG_S2C_ROOM_LEFT* msg)
{
    std::wcout << L"\n==================================" << std::endl;
    if (msg->success)
    {
        _room.OnRoomLeft();
        std::wcout << L"Left room successfully!" << std::endl;
    }
    else
    {
        std::wcout << L"Failed to leave room." << std::endl;
    }
    std::wcout << L"==================================" << std::endl;
}

void CGameInstance::OnError(const MSG_S2C_ERROR* msg)
{
    std::wcout << L"\n==================================" << std::endl;
    std::wcout << L"[ERROR] " << msg->message << std::endl;
    std::wcout << L"==================================" << std::endl;
}

int CGameInstance::ShowMainMenuWithSelection()
{
    int selectedIndex = 0; // 0: Single, 1: Multi, 2: Exit
    const int menuCount = 3;

    while (true)
    {
        // 화면 지우기
        system("cls");

        // 메뉴 출력 (선택된 항목에 ▶ 표시)
        std::wcout << L"┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓\n";
        std::wcout << L"┃                                                                      ┃\n";
        std::wcout << L"┃   ████████╗███████╗████████╗██████╗ ██╗███████╗                      ┃\n";
        std::wcout << L"┃   ╚══██╔══╝██╔════╝╚══██╔══╝██╔══██╗██║██╔════╝                      ┃\n";
        std::wcout << L"┃      ██║   █████╗     ██║   ██████╔╝██║███████╗                      ┃\n";
        std::wcout << L"┃      ██║   ██╔══╝     ██║   ██╔══██╗██║╚════██║                      ┃\n";
        std::wcout << L"┃      ██║   ███████╗   ██║   ██║  ██║██║███████║                      ┃\n";
        std::wcout << L"┃      ╚═╝   ╚══════╝   ╚═╝   ╚═╝  ╚═╝╚═╝╚══════╝                      ┃\n";
        std::wcout << L"┃   ▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒            ┃\n";
        std::wcout << L"┃                                                                      ┃\n";
        std::wcout << L"┣━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫\n";
        std::wcout << L"┃                                                                      ┃\n";
        std::wcout << L"┃                [ SELECT YOUR MISSION ]                               ┃\n";
        std::wcout << L"┃                                                                      ┃\n";

        if (selectedIndex == 0)
        {
            std::wcout << L"┃             ▶  1. SINGLE PLAYER (PRACTICE)                          ┃\n";
        }
        else
        {
            std::wcout << L"┃             　  1. SINGLE PLAYER (PRACTICE)                          ┃\n";
        }

        if (selectedIndex == 1)
        {
            std::wcout << L"┃             ▶  2. MULTI PLAYER  (IOCP ARENA)                        ┃\n";
        }
        else
        {
            std::wcout << L"┃             　  2. MULTI PLAYER  (IOCP ARENA)                        ┃\n";
        }

        if (selectedIndex == 2)
        {
            std::wcout << L"┃             ▶  3. EXIT GAME                                         ┃\n";
        }
        else
        {
            std::wcout << L"┃               　3. EXIT GAME                                         ┃\n";
        }

        std::wcout << L"┃                                                                      ┃\n";
        std::wcout << L"┃                                                                      ┃\n";
        std::wcout << L"┣━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┫\n";
        std::wcout << L"┃                                                                      ┃\n";
        std::wcout << L"┃  Use ↑↓ to navigate, Enter to select                               ┃\n";
        std::wcout << L"┃  © 2025 ALL RIGHTS RESERVED. VER 1.0.0                               ┃\n";
        std::wcout << L"┃                                                                      ┃\n";
        std::wcout << L"┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛\n";

        // 키 입력 받기
        int key = _getch();

        if (key == 224 || key == 0) // 방향키는 2바이트
        {
            key = _getch();
            if (key == 72) // ↑
            {
                selectedIndex = (selectedIndex - 1 + menuCount) % menuCount;
            }
            else if (key == 80) // ↓
            {
                selectedIndex = (selectedIndex + 1) % menuCount;
            }
        }
        else if (key == 13) // Enter
        {
            return selectedIndex + 1; // 1, 2, 3 반환
        }
    }
}

void CGameInstance::ShowLobbyMenu()
{
    std::wcout << L"\n==================================" << std::endl;
    std::wcout << L"LOBBY MENU" << std::endl;
    std::wcout << L"==================================" << std::endl;
    std::wcout << L"[1] View Room List" << std::endl;
    std::wcout << L"[2] Create Room" << std::endl;
    std::wcout << L"[3] Join Room" << std::endl;
    std::wcout << L"[0] Disconnect" << std::endl;
    std::wcout << L"==================================" << std::endl;
    std::wcout << L"Select: ";
}

void CGameInstance::ProcessLobbyInput()
{
    ShowLobbyMenu();

    int choice;
    std::wcin >> choice;

    if (std::wcin.fail())
    {
        std::wcin.clear();
        std::wcin.ignore(10000, L'\n');
        std::wcout << L"Invalid input. Please enter a number." << std::endl;
        return;
    }

    switch (choice)
    {
    case 1:
        _network.RequestRoomList();
        break;
    case 2:
    {
        std::wstring title;
        int32_t maxPlayers;

        std::wcout << L"Enter room title: ";
        std::wcin.ignore();
        std::getline(std::wcin, title);

        std::wcout << L"Enter max players (2-10): ";
        std::wcin >> maxPlayers;

        if (maxPlayers < 2 || maxPlayers > 10)
        {
            std::wcout << L"Invalid max players. Must be between 2 and 10." << std::endl;
            break;
        }

        _room.RequestCreateRoom(std::string(title.begin(), title.end()), maxPlayers);
        break;
    }
    case 3:
    {
        int32_t roomId;
        std::wcout << L"Enter room ID: ";
        std::wcin >> roomId;

        _room.RequestJoinRoom(roomId);
        break;
    }
    case 0:
        std::wcout << L"Disconnecting..." << std::endl;
        _running = false;
        break;
    default:
        std::wcout << L"Invalid choice." << std::endl;
        break;
    }

    std::wcin.clear();
    std::wcin.ignore(10000, L'\n');
}

void CGameInstance::ProcessRoomInput()
{
    _room.DisplayRoomView();

    int choice;
    std::wcin >> choice;

    if (std::wcin.fail())
    {
        std::wcin.clear();
        std::wcin.ignore(10000, L'\n');
        std::wcout << L"Invalid input. Please enter a number." << std::endl;
        return;
    }

    switch (choice)
    {
    case 1:
        _room.RequestLeaveRoom();
        break;
    case 0:
        std::wcout << L"Disconnecting..." << std::endl;
        _running = false;
        break;
    default:
        std::wcout << L"Invalid choice." << std::endl;
        break;
    }

    std::wcin.clear();
    std::wcin.ignore(10000, L'\n');
}