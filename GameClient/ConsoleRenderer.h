#pragma once

#include "ClientPlayer.h"
#define NOMINMAX
#include <Windows.h>
#include <string>
#include <vector>
#include <unordered_map>

// 콘솔 기반 2D 게임 렌더러
class CConsoleRenderer
{
public:
    CConsoleRenderer();
    ~CConsoleRenderer();

    void Init();
    void SetMapSize(int width, int height);

    // 매 프레임 전체 화면 갱신
    void RenderFrame(const ClientPlayer* me,
                     const std::unordered_map<int32_t, ClientPlayer>& others,
                     const std::vector<std::wstring>& chatLog,
                     const std::wstring& chatInput,
                     bool chatMode);

private:
    // 개별 영역 렌더
    void RenderStatusBar(const ClientPlayer* me);
    void RenderHelpBar();
    void RenderGameView(const ClientPlayer* me,
                        const std::unordered_map<int32_t, ClientPlayer>& others);
    void RenderChatArea(const std::vector<std::wstring>& chatLog);
    void RenderChatInput(const std::wstring& chatInput, bool chatMode);

    // 유틸
    void WriteTextAt(SHORT x, SHORT y, const wchar_t* text, WORD attr, int maxLen);
    void ClearLine(SHORT y, WORD attr);

private:
    HANDLE _hConsole;

    // 게임 뷰 설정 (서버에서 S2C_ZONE_INFO로 수신)
    int _mapWidth = 400;
    int _mapHeight = 400;
    static constexpr int VIEW_WIDTH = 80;
    static constexpr int VIEW_HEIGHT = 21;

    // 레이아웃 (행 번호)
    static constexpr SHORT STATUS_ROW = 0;
    static constexpr SHORT HELP_ROW = 1;
    static constexpr SHORT VIEW_START_ROW = 2;
    static constexpr SHORT CHAT_START_ROW = VIEW_START_ROW + VIEW_HEIGHT; // 23
    static constexpr int MAX_CHAT_LINES = 24;
    static constexpr SHORT CHAT_INPUT_ROW = CHAT_START_ROW + MAX_CHAT_LINES; // 47
    static constexpr SHORT CONSOLE_WIDTH = 80;

    // 게임 뷰 버퍼 (WriteConsoleOutputW용)
    CHAR_INFO _viewBuffer[VIEW_HEIGHT][VIEW_WIDTH];

    // 색상 상수
    static constexpr WORD COLOR_DEFAULT = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    static constexpr WORD COLOR_STATUS = FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
    static constexpr WORD COLOR_MY_PLAYER = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
    static constexpr WORD COLOR_OTHER_PLAYER = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
    static constexpr WORD COLOR_TILE = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    static constexpr WORD COLOR_WALL = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
    static constexpr WORD COLOR_OUTSIDE = 0;
    static constexpr WORD COLOR_CHAT_NAME = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
    static constexpr WORD COLOR_CHAT_TEXT = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    static constexpr WORD COLOR_CHAT_INPUT = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
    static constexpr WORD COLOR_CHAT_MODE = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
    static constexpr WORD COLOR_BORDER = FOREGROUND_BLUE | FOREGROUND_INTENSITY;
};
