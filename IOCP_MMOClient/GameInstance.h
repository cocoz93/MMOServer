#pragma once

#include "ClientNetwork.h"
#include "PlayerManager.h"
#include "ConsoleRenderer.h"
#include "NetworkEventQueue.h"
#include <string>
#include <vector>

class CGameInstance
{
public:
    CGameInstance();
    ~CGameInstance();

    // 초기화 및 실행
    bool Initialize();
    void Run();
    void Shutdown();

    // 네트워크 연결
    bool ConnectToServer(const std::string& serverIp, int port);

    // S2C 패킷 핸들러 (수신 스레드에서 호출 → 이벤트 큐에 Push)
    void OnCreateMyPlayer(const MSG_S2C_CREATE_MY_PLAYER* msg);
    void OnCreateOtherPlayer(const MSG_S2C_CREATE_OTHER_PLAYER* msg);
    void OnDeletePlayer(const MSG_S2C_DELETE_PLAYER* msg);
    void OnMoveStart(const MSG_S2C_MOVE_START* msg);
    void OnMoveStop(const MSG_S2C_MOVE_STOP* msg);
    void OnChat(const MSG_S2C_CHAT* msg);
    void OnSyncPosition(const MSG_S2C_SYNC_POSITION* msg);
    void OnZoneChangeOk(const MSG_S2C_ZONE_CHANGE_OK* msg);
    void OnZoneChangeFail(const MSG_S2C_ZONE_CHANGE_FAIL* msg);
    void OnError(const MSG_S2C_ERROR* msg);

private:
    // 게임 루프 (25fps)
    void GameLoop();

    // 이벤트 큐 소비 (게임 루프 스레드에서)
    void ProcessNetworkEvents();

    // 키보드 입력 처리
    void ProcessInput();
    void ProcessChatInput();

    // 채팅 로그에 메시지 추가
    void AddChatMessage(const std::wstring& msg);

    // 채팅 커맨드 처리 (/map 등)
    void HandleChatCommand(const std::wstring& command);

private:
    CClientNetwork _network;
    CPlayerManager _playerManager;
    CConsoleRenderer _renderer;
    CNetworkEventQueue _eventQueue;
    bool _running;

    // 키 상태 추적 (키 누름/뗌 감지용)
    bool _keyPressed[4]; // UP, DOWN, LEFT, RIGHT
    bool _enterPressed;

    // 채팅
    std::vector<std::wstring> _chatLog;
    std::wstring _chatInput;
    bool _chatMode;
    static constexpr int MAX_CHAT_LINES = 14;

    // 프레임 설정 (서버 동일)
    static constexpr int FRAME_PER_SEC = 25;
    static constexpr int FRAME_INTERVAL_MS = 1000 / FRAME_PER_SEC;  // 40ms

    // 하트비트 (서버 타임아웃 30초 대비 20초 간격)
    static constexpr int HEARTBEAT_INTERVAL_MS = 20000;
    int _heartbeatAccumMs;
};
