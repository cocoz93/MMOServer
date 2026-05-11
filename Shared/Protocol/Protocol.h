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
    MsgHeader header;
    int32_t playerId;     // 이동하는 플레이어
    uint8_t direction;
    float x;              // 이동 시작 시점 좌표
    float y;
};

// S2C: 다른 플레이어 이동 정지
struct MSG_S2C_MOVE_STOP
{
    MsgHeader header;
    int32_t playerId;
    uint8_t direction;
    float x;              // 정지 좌표
    float y;
};

//==================================================
// 스폰 / 삭제
//==================================================

// S2C: 내 캐릭터 생성 (접속 직후)
struct MSG_S2C_CREATE_MY_PLAYER
{
    MsgHeader header;
    int32_t playerId;
    uint8_t direction;
    float x;
    float y;
};

// S2C: 다른 캐릭터 생성 (시야 진입)
struct MSG_S2C_CREATE_OTHER_PLAYER
{
    MsgHeader header;
    int32_t playerId;
    uint8_t direction;
    uint8_t moveState;    // MoveState enum (진입 시 이동 중일 수 있음)
    float x;
    float y;
};

// S2C: 캐릭터 삭제 (시야 이탈 / 퇴장)
struct MSG_S2C_DELETE_PLAYER
{
    MsgHeader header;
    int32_t playerId;
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
    MsgHeader header;
    int32_t playerId;     // 발신자
    wchar_t message[CHAT_MSG_MAX_LEN];
};

//==================================================
// 에러
//==================================================

// S2C: 에러 응답
struct MSG_S2C_ERROR
{
    MsgHeader header;
    char message[256];
};

#pragma pack(pop)
