#include "ConsoleRenderer.h"
#include <cstdio>
#include <cstring>
#include <algorithm>

CConsoleRenderer::CConsoleRenderer()
    : _hConsole(INVALID_HANDLE_VALUE)
{
    memset(_viewBuffer, 0, sizeof(_viewBuffer));
}

CConsoleRenderer::~CConsoleRenderer()
{
}

void CConsoleRenderer::Init()
{
    _hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

    // 커서 숨기기
    CONSOLE_CURSOR_INFO cursorInfo;
    cursorInfo.dwSize = 1;
    cursorInfo.bVisible = FALSE;
    SetConsoleCursorInfo(_hConsole, &cursorInfo);

    // 콘솔 크기 설정
    COORD bufferSize = { CONSOLE_WIDTH, 40 };
    SetConsoleScreenBufferSize(_hConsole, bufferSize);

    SMALL_RECT windowSize = { 0, 0, CONSOLE_WIDTH - 1, 39 };
    SetConsoleWindowInfo(_hConsole, TRUE, &windowSize);

    // 화면 초기화
    DWORD written;
    COORD origin = { 0, 0 };
    FillConsoleOutputCharacterW(_hConsole, L' ', CONSOLE_WIDTH * 40, origin, &written);
    FillConsoleOutputAttribute(_hConsole, COLOR_DEFAULT, CONSOLE_WIDTH * 40, origin, &written);
}

// ==========================================================================
// 매 프레임 전체 화면 갱신
// ==========================================================================

void CConsoleRenderer::RenderFrame(const ClientPlayer* me,
                                   const std::unordered_map<int32_t, ClientPlayer>& others,
                                   const std::vector<std::wstring>& chatLog,
                                   const std::wstring& chatInput,
                                   bool chatMode)
{
    RenderStatusBar(me);
    RenderGameView(me, others);
    RenderChatArea(chatLog);
    RenderChatInput(chatInput, chatMode);
}

// ==========================================================================
// 상태바 (Row 0)
// ==========================================================================

void CConsoleRenderer::RenderStatusBar(const ClientPlayer* me)
{
    wchar_t buf[CONSOLE_WIDTH + 1];

    if (me)
    {
        const wchar_t* stateStr = (me->moveState == MoveState::MOVING) ? L"MOVING" : L"IDLE";
        const wchar_t* dirStr = L"";
        switch (me->direction)
        {
        case Direction::UP:    dirStr = L"UP";    break;
        case Direction::DOWN:  dirStr = L"DOWN";  break;
        case Direction::LEFT:  dirStr = L"LEFT";  break;
        case Direction::RIGHT: dirStr = L"RIGHT"; break;
        default:               dirStr = L"-";     break;
        }

        swprintf_s(buf, CONSOLE_WIDTH + 1,
            L" Player:%-4d  Pos:(%6.1f, %6.1f)  [%s %s]  | Arrow:Move  Enter:Chat  ESC:Quit",
            me->playerId, me->x, me->y, stateStr, dirStr);
    }
    else
    {
        swprintf_s(buf, CONSOLE_WIDTH + 1, L" Waiting for server...");
    }

    // 나머지를 공백으로 채우기
    size_t len = wcslen(buf);
    for (size_t i = len; i < CONSOLE_WIDTH; ++i)
        buf[i] = L' ';
    buf[CONSOLE_WIDTH] = L'\0';

    WriteTextAt(0, STATUS_ROW, buf, COLOR_STATUS, CONSOLE_WIDTH);
}

// ==========================================================================
// 게임 뷰 (Row 1~22) — WriteConsoleOutputW 단일 호출
// ==========================================================================

void CConsoleRenderer::RenderGameView(const ClientPlayer* me,
                                      const std::unordered_map<int32_t, ClientPlayer>& others)
{
    // 뷰포트 원점 (월드 좌표)
    float camX = 0.0f, camY = 0.0f;
    if (me)
    {
        camX = me->x - VIEW_WIDTH / 2.0f;
        camY = me->y - VIEW_HEIGHT / 2.0f;
    }

    // 빈 타일로 채우기
    for (int row = 0; row < VIEW_HEIGHT; ++row)
    {
        for (int col = 0; col < VIEW_WIDTH; ++col)
        {
            _viewBuffer[row][col].Char.UnicodeChar = L'.';
            _viewBuffer[row][col].Attributes = COLOR_TILE;
        }
    }

    // 다른 플레이어 배치
    for (const auto& pair : others)
    {
        const ClientPlayer& p = pair.second;
        int sx = static_cast<int>(p.x - camX + 0.5f);
        int sy = static_cast<int>(p.y - camY + 0.5f);

        if (sx >= 0 && sx < VIEW_WIDTH && sy >= 0 && sy < VIEW_HEIGHT)
        {
            _viewBuffer[sy][sx].Char.UnicodeChar = L'#';
            _viewBuffer[sy][sx].Attributes = COLOR_OTHER_PLAYER;
        }
    }

    // 내 캐릭터 배치 (항상 중앙)
    if (me)
    {
        int mx = static_cast<int>(me->x - camX + 0.5f);
        int my = static_cast<int>(me->y - camY + 0.5f);

        if (mx >= 0 && mx < VIEW_WIDTH && my >= 0 && my < VIEW_HEIGHT)
        {
            _viewBuffer[my][mx].Char.UnicodeChar = L'@';
            _viewBuffer[my][mx].Attributes = COLOR_MY_PLAYER;
        }
    }

    // 한 번에 출력
    COORD bufSize = { VIEW_WIDTH, VIEW_HEIGHT };
    COORD bufCoord = { 0, 0 };
    SMALL_RECT writeRegion = { 0, VIEW_START_ROW,
                               VIEW_WIDTH - 1,
                               static_cast<SHORT>(VIEW_START_ROW + VIEW_HEIGHT - 1) };

    WriteConsoleOutputW(_hConsole, &_viewBuffer[0][0], bufSize, bufCoord, &writeRegion);
}

// ==========================================================================
// 채팅 영역 (Row 23~36)
// ==========================================================================

void CConsoleRenderer::RenderChatArea(const std::vector<std::wstring>& chatLog)
{
    // 구분선
    wchar_t separator[CONSOLE_WIDTH + 1];
    for (int i = 0; i < CONSOLE_WIDTH; ++i)
        separator[i] = L'-';
    separator[CONSOLE_WIDTH] = L'\0';
    WriteTextAt(0, CHAT_START_ROW, separator, COLOR_BORDER, CONSOLE_WIDTH);

    // 채팅 메시지 표시 (최근 MAX_CHAT_LINES-1 개)
    int chatDisplayLines = MAX_CHAT_LINES - 1; // 1줄은 구분선
    int startIdx = 0;
    if (static_cast<int>(chatLog.size()) > chatDisplayLines)
        startIdx = static_cast<int>(chatLog.size()) - chatDisplayLines;

    for (int i = 0; i < chatDisplayLines; ++i)
    {
        SHORT row = CHAT_START_ROW + 1 + static_cast<SHORT>(i);
        ClearLine(row, COLOR_DEFAULT);

        int logIdx = startIdx + i;
        if (logIdx < static_cast<int>(chatLog.size()))
        {
            WriteTextAt(1, row, chatLog[logIdx].c_str(), COLOR_CHAT_TEXT,
                        CONSOLE_WIDTH - 2);
        }
    }
}

// ==========================================================================
// 채팅 입력 (Row 37)
// ==========================================================================

void CConsoleRenderer::RenderChatInput(const std::wstring& chatInput, bool chatMode)
{
    ClearLine(CHAT_INPUT_ROW, COLOR_DEFAULT);

    wchar_t buf[CONSOLE_WIDTH + 1];
    if (chatMode)
    {
        swprintf_s(buf, CONSOLE_WIDTH + 1, L" > %s_", chatInput.c_str());
        WriteTextAt(0, CHAT_INPUT_ROW, buf, COLOR_CHAT_MODE, CONSOLE_WIDTH);
    }
    else
    {
        swprintf_s(buf, CONSOLE_WIDTH + 1, L" [Enter to chat]");
        WriteTextAt(0, CHAT_INPUT_ROW, buf, COLOR_BORDER, CONSOLE_WIDTH);
    }
}

// ==========================================================================
// 유틸리티
// ==========================================================================

void CConsoleRenderer::WriteTextAt(SHORT x, SHORT y, const wchar_t* text, WORD attr, int maxLen)
{
    COORD pos = { x, y };
    DWORD written;

    int len = static_cast<int>(wcslen(text));
    if (len > maxLen) len = maxLen;

    WriteConsoleOutputCharacterW(_hConsole, text, len, pos, &written);

    // 속성 배열
    std::vector<WORD> attrs(len, attr);
    WriteConsoleOutputAttribute(_hConsole, attrs.data(), len, pos, &written);
}

void CConsoleRenderer::ClearLine(SHORT y, WORD attr)
{
    COORD pos = { 0, y };
    DWORD written;
    FillConsoleOutputCharacterW(_hConsole, L' ', CONSOLE_WIDTH, pos, &written);
    FillConsoleOutputAttribute(_hConsole, attr, CONSOLE_WIDTH, pos, &written);
}
