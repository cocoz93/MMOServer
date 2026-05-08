//
#include "Room.h"
#include "ClientNetwork.h"
#define NOMINMAX
#include <algorithm>
#include <iostream>

CRoom::CRoom()
    : _network(nullptr)
    , _inRoom(false)
    , _roomId(-1)
    , _maxPlayers(0)
    , _pendingMaxPlayers(0)
{
}

CRoom::~CRoom()
{
}

void CRoom::RequestCreateRoom(const std::string& title, int32_t maxPlayers)
{
    if (!_network)
    {
        return;
    }

    _pendingTitle = title;
    _pendingMaxPlayers = maxPlayers;

    _network->RequestCreateRoom(title, maxPlayers);
}

void CRoom::RequestJoinRoom(int32_t roomId)
{
    if (!_network)
    {
        return;
    }

    _network->RequestJoinRoom(roomId);
}

void CRoom::RequestLeaveRoom()
{
    if (!_network)
    {
        return;
    }

    _network->RequestLeaveRoom();
}

void CRoom::OnRoomCreated(int32_t roomId)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _inRoom = true;
    _roomId = roomId;
    _title = _pendingTitle;
    _maxPlayers = _pendingMaxPlayers;
    InitPlayers();
}

void CRoom::OnRoomJoined(int32_t roomId)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _inRoom = true;
    _roomId = roomId;
    // TODO: 서버에서 방 정보를 받아와 _title, _maxPlayers 업데이트
    InitPlayers();
}

void CRoom::OnRoomLeft()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _inRoom = false;
    _roomId = -1;
    _title.clear();
    _maxPlayers = 0;
    _players.clear();
}

void CRoom::InitPlayers()
{
    _players.clear();

    RoomPlayer me;
    me.isMe = true;
    me.isPresent = true;
    me.name = "You";
    _players.push_back(me);

    for (int i = 0; i < 9; ++i)
    {
        RoomPlayer other;
        other.isMe = false;
        other.isPresent = false;
        other.name = "";
        _players.push_back(other);
    }
}

void CRoom::AddPlayer(int index, const std::string& name)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (index >= 0 && index < static_cast<int>(_players.size()))
    {
        _players[index].isPresent = true;
        _players[index].name = name;
    }
}

void CRoom::RemovePlayer(int index)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (index >= 0 && index < static_cast<int>(_players.size()))
    {
        _players[index].isPresent = false;
        _players[index].name = "";
    }
}

void CRoom::DisplayRoomInfo()
{
    std::lock_guard<std::mutex> lock(_mutex);

    int currentPlayers = 0;
    for (const auto& player : _players)
    {
        if (player.isPresent)
        {
            currentPlayers++;
        }
    }

    std::wstring statusStr = L"WAITING";
    if (_maxPlayers > 0 && currentPlayers == _maxPlayers)
    {
        statusStr = L"PLAYING";
    }

    std::wcout << L"\n";
    std::wcout << L"+----+--------------------------+--------+----------+" << std::endl;
    std::wcout << L"| ID | Title                    | Player | Status   |" << std::endl;
    std::wcout << L"+----+--------------------------+--------+----------+" << std::endl;
    wprintf(L"| %-2d | %-24s | %2d/%-3d | %-8s |\n",
        _roomId,
        std::wstring(_title.begin(), _title.end()).c_str(),
        currentPlayers,
        _maxPlayers,
        statusStr.c_str());
    std::wcout << L"+----+--------------------------+--------+----------+" << std::endl;
}

void CRoom::DisplayRoomView()
{
    DisplayRoomInfo();

    std::vector<std::wstring> myScreen = {
        L"+---------------------+",
        L"|                     |",
        L"+---------------------+",
        L"|                     |",
        L"|                     |",
        L"|                     |",
        L"|                     |",
        L"|                     |",
        L"|                     |",
        L"|        WAIT         |",
        L"|                     |",
        L"|                     |",
        L"|                     |",
        L"|                     |",
        L"|                     |",
        L"|                     |",
        L"|                     |",
        L"|                     |",
        L"|                     |",
        L"|                     |",
        L"|                     |",
        L"+---------------------+"
    };

    std::vector<std::vector<std::wstring>> smallScreens;
    {
        std::lock_guard<std::mutex> lock(_mutex);

        std::vector<std::wstring> emptyBox = {
            L"+-------------+",
            L"|             |",
            L"|             |",
            L"|             |",
            L"|             |",
            L"|             |",
            L"|             |",
            L"|             |",
            L"+-------------+"
        };
        smallScreens.push_back(emptyBox);

        for (int i = 1; i <= 9; ++i)
        {
            std::wstring label;
            if (_players.size() > static_cast<size_t>(i) && _players[i].isPresent)
            {
                label = L"[WAIT]";
            }
            else
            {
                label = L"[NONE]";
            }

            std::vector<std::wstring> box = {
                L"+-------------+",
                L"|             |",
                L"+-------------+",
                L"|             |",
                L"|             |",
                L"|   " + std::to_wstring(i) + L"  " + label + std::wstring(7 - label.size(), L' ') + L"|",
                L"|             |",
                L"|             |",
                L"|             |",
                L"|             |",
                L"+-------------+"
            };
            smallScreens.push_back(box);
        }
    }

    std::vector<std::wstring> rightBlock;
    for (size_t line = 0; line < smallScreens[0].size(); ++line)
    {
        std::wstring row;
        for (int j = 0; j <= 4; ++j)
        {
            row += smallScreens[j][line];
            if (j != 4)
            {
                row += L"  ";
            }
        }
        rightBlock.push_back(row);
    }
    for (size_t line = 0; line < smallScreens[5].size(); ++line)
    {
        std::wstring row;
        for (int j = 5; j <= 9; ++j)
        {
            row += smallScreens[j][line];
            if (j != 9)
            {
                row += L"  ";
            }
        }
        rightBlock.push_back(row);
    }

    size_t totalLines = (std::max)(myScreen.size(), rightBlock.size());
    std::wcout << L"\n";
    for (size_t i = 0; i < totalLines; ++i)
    {
        if (i < myScreen.size())
        {
            std::wcout << myScreen[i];
        }
        else
        {
            std::wcout << std::wstring(myScreen[0].size(), L' ');
        }

        if (i < rightBlock.size())
        {
            std::wcout << L"  " << rightBlock[i];
        }
        std::wcout << std::endl;
    }

    std::wcout << L"\n[1] Leave Room" << std::endl;
    std::wcout << L"[0] Disconnect" << std::endl;
    std::wcout << L"Select: ";
}

void CRoom::DisplayRoomList(const std::vector<RoomInfo>& rooms)
{
    std::wcout << L"\n";
    std::wcout << L"+------+------------------------+----------+----------+" << std::endl;
    std::wcout << L"| ID   | TITLE                  | PLAYERS  | STATUS   |" << std::endl;
    std::wcout << L"+------+------------------------+----------+----------+" << std::endl;

    for (const auto& room : rooms)
    {
        std::wstring statusStr = (room.status == 0) ? L"WAITING" : L"PLAYING";

        wprintf(L"| %-4d | %-22s | %2d / %-4d | %-8s |\n",
            room.roomId,
            std::wstring(room.title, room.title + strlen(room.title)).c_str(),
            room.currentPlayers,
            room.maxPlayers,
            statusStr.c_str());
    }

    std::wcout << L"+------+------------------------+----------+----------+" << std::endl;
}