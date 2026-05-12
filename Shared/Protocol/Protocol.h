#pragma once

#include <cstdint>

// 패킷 타입 (혼용 방지를 위해 L7 Msg로 표기)
enum class MsgType : uint16_t
{
	// C2S: Client to Server
	// S2C: Server to Client

    //--------------------------------------------------
    // 이동
    //--------------------------------------------------
    C2S_MOVE_START = 1000,  // 클라이언트 → 서버: 이동 시작
    C2S_MOVE_STOP,          // 클라이언트 → 서버: 이동 정지

    S2C_MOVE_START,         // 서버 → 클라이언트: 다른 플레이어 이동 시작
    S2C_MOVE_STOP,          // 서버 → 클라이언트: 다른 플레이어 이동 정지

    //--------------------------------------------------
    // 스폰 / 삭제
    //--------------------------------------------------
    S2C_CREATE_MY_PLAYER,   // 서버 → 클라이언트: 내 캐릭터 생성
    S2C_CREATE_OTHER_PLAYER,// 서버 → 클라이언트: 다른 캐릭터 생성 (시야 진입)
    S2C_DELETE_PLAYER,      // 서버 → 클라이언트: 캐릭터 삭제 (시야 이탈/퇴장)

    //--------------------------------------------------
    // 채팅
    //--------------------------------------------------
    C2S_CHAT,               // 클라이언트 → 서버: 채팅 메시지 전송
    S2C_CHAT,               // 서버 → 클라이언트: 채팅 메시지 브로드캐스트

    //--------------------------------------------------
    // 좌표 보정
    //--------------------------------------------------
    S2C_SYNC_POSITION,  // 서버 → 클라이언트: 좌표 강제 보정

    //--------------------------------------------------
    // 존 이동
    //--------------------------------------------------
    C2S_ZONE_CHANGE,      // 클라이언트 → 서버: 맵 이동 요청
    S2C_ZONE_CHANGE_OK,   // 서버 → 클라이언트: 이동 성공
    S2C_ZONE_CHANGE_FAIL, // 서버 → 클라이언트: 이동 실패

    //--------------------------------------------------
    // 에러
    //--------------------------------------------------
    S2C_ERROR
};

// 패킷 헤더 (모든 패킷 공통)
#pragma pack(push, 1)
struct MsgHeader
{
    uint16_t size;        // 패킷 전체 크기 (헤더 포함)
    MsgType type;         // 패킷 타입
};

// 에코 테스트용 헤더 (GameCodiEchoTest 더미 클라이언트 호환)
struct EchoMsgHeader
{
    uint16_t size;        // 페이로드 크기 (헤더 미포함)
};

//==================================================
// 이동
//==================================================

// C2S: 이동 시작 (방향키 누름)
struct MSG_C2S_MOVE_START
{
    MsgHeader header;
    uint8_t direction;    // Direction enum
};

// C2S: 이동 정지 (방향키 뗌)
struct MSG_C2S_MOVE_STOP
{
    MsgHeader header;
    uint8_t direction;    // 마지막 방향 (캐릭터가 바라보는 방향)
    float x;              // 정지 시점 좌표
    float y;
};

// S2C: 다른 플레이어 이동 시작
struct MSG_S2C_MOVE_START
{
    static constexpr MsgType TYPE = MsgType::S2C_MOVE_START;
    MsgHeader header;
    int32_t playerId;     // 이동하는 플레이어
    uint8_t direction;
    float x;              // 이동 시작 시점 좌표
    float y;

    MSG_S2C_MOVE_START() : header{ sizeof(*this), TYPE }, playerId(0), direction(0), x(0), y(0) {}
};

// S2C: 다른 플레이어 이동 정지
struct MSG_S2C_MOVE_STOP
{
    static constexpr MsgType TYPE = MsgType::S2C_MOVE_STOP;
    MsgHeader header;
    int32_t playerId;
    uint8_t direction;
    float x;              // 정지 좌표
    float y;

    MSG_S2C_MOVE_STOP() : header{ sizeof(*this), TYPE }, playerId(0), direction(0), x(0), y(0) {}
};

//==================================================
// 스폰 / 삭제
//==================================================

// S2C: 내 캐릭터 생성 (접속 직후)
struct MSG_S2C_CREATE_MY_PLAYER
{
    static constexpr MsgType TYPE = MsgType::S2C_CREATE_MY_PLAYER;
    MsgHeader header;
    int32_t playerId;
    uint8_t direction;
    float x;
    float y;
    int32_t speed;

    MSG_S2C_CREATE_MY_PLAYER() : header{ sizeof(*this), TYPE }, playerId(0), direction(0), x(0), y(0), speed(0) {}
};

// S2C: 다른 캐릭터 생성 (시야 진입)
struct MSG_S2C_CREATE_OTHER_PLAYER
{
    static constexpr MsgType TYPE = MsgType::S2C_CREATE_OTHER_PLAYER;
    MsgHeader header;
    int32_t playerId;
    uint8_t direction;
    uint8_t moveState;    // MoveState enum (진입 시 이동 중일 수 있음)
    float x;
    float y;
    int32_t speed;

    MSG_S2C_CREATE_OTHER_PLAYER() : header{ sizeof(*this), TYPE }, playerId(0), direction(0), moveState(0), x(0), y(0), speed(0) {}
};

// S2C: 캐릭터 삭제 (시야 이탈 / 퇴장)
struct MSG_S2C_DELETE_PLAYER
{
    static constexpr MsgType TYPE = MsgType::S2C_DELETE_PLAYER;
    MsgHeader header;
    int32_t playerId;

    MSG_S2C_DELETE_PLAYER() : header{ sizeof(*this), TYPE }, playerId(0) {}
};

//==================================================
// 채팅
//==================================================

constexpr int32_t CHAT_MSG_MAX_LEN = 64; // wchar_t 기준 글자 수 (128바이트)

// C2S: 채팅 메시지
struct MSG_C2S_CHAT
{
    MsgHeader header;
    wchar_t message[CHAT_MSG_MAX_LEN];
};

// S2C: 채팅 메시지 (발신자 정보 포함)
struct MSG_S2C_CHAT
{
    static constexpr MsgType TYPE = MsgType::S2C_CHAT;
    MsgHeader header;
    int32_t playerId;     // 발신자
    wchar_t message[CHAT_MSG_MAX_LEN];

    MSG_S2C_CHAT() : header{ sizeof(*this), TYPE }, playerId(0), message{} {}
};

//==================================================
// 좌표 보정
//==================================================

// S2C: 서버 권위 좌표 강제 동기화
struct MSG_S2C_SYNC_POSITION
{
    static constexpr MsgType TYPE = MsgType::S2C_SYNC_POSITION;
    MsgHeader header;
    float x;
    float y;

    MSG_S2C_SYNC_POSITION() : header{ sizeof(*this), TYPE }, x(0), y(0) {}
};

//==================================================
// 존 이동
//==================================================

// C2S: 맵 이동 요청
struct MSG_C2S_ZONE_CHANGE
{
    MsgHeader header;
    int32_t targetMapId;
};

// S2C: 맵 이동 성공
struct MSG_S2C_ZONE_CHANGE_OK
{
    static constexpr MsgType TYPE = MsgType::S2C_ZONE_CHANGE_OK;
    MsgHeader header;
    int32_t mapId;
    int32_t channelIndex;
    int32_t playerId;
    float x;
    float y;

    MSG_S2C_ZONE_CHANGE_OK() : header{ sizeof(*this), TYPE }, mapId(0), channelIndex(0), playerId(0), x(0), y(0) {}
};

// S2C: 맵 이동 실패
struct MSG_S2C_ZONE_CHANGE_FAIL
{
    static constexpr MsgType TYPE = MsgType::S2C_ZONE_CHANGE_FAIL;
    MsgHeader header;
    uint8_t reason;  // 0: 존재하지 않는 맵, 1: 모든 채널 가득 참

    MSG_S2C_ZONE_CHANGE_FAIL() : header{ sizeof(*this), TYPE }, reason(0) {}
};

//==================================================
// 에러
//==================================================

// S2C: 에러 응답
struct MSG_S2C_ERROR
{
    static constexpr MsgType TYPE = MsgType::S2C_ERROR;
    MsgHeader header;
    char message[256];

    MSG_S2C_ERROR() : header{ sizeof(*this), TYPE }, message{} {}
};

#pragma pack(pop)
