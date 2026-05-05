#pragma once

#include <cstdint>

// 패킷 타입 (혼용 방지를 위해 L7 Msg로 표기)
enum class MsgType : uint16_t
{
	// C2S: Client to Server
	// S2C: Server to Client
    
    C2S_REQUEST_ROOM_LIST = 1000,
    S2C_ROOM_LIST,
    
    C2S_CREATE_ROOM,
    S2C_ROOM_CREATED,

    C2S_JOIN_ROOM,
    S2C_ROOM_JOINED,

    C2S_LEAVE_ROOM,
    S2C_ROOM_LEFT,

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

// 방 정보 (목록용)
struct RoomInfo
{
    int32_t roomId;
    char title[64];
    int32_t currentPlayers;
    int32_t maxPlayers;
    uint8_t status; // 0: WAITING, 1: PLAYING
};

// S2C: 방 목록 응답 (가변 길이)
struct MSG_S2C_ROOM_LIST
{
    MsgHeader header;
    int32_t roomCount;
    // RoomInfo rooms[roomCount]; // 가변 배열
};

// C2S: 방 생성 요청 (최신화 요청 등)
struct MSG_C2S_CREATE_ROOM
{
    MsgHeader header;
    char title[64];
    int32_t maxPlayers;
};

// S2C: 방 생성 응답
struct MSG_S2C_ROOM_CREATED
{
    MsgHeader header;
    int32_t roomId;
    uint8_t success; // 0: 실패, 1: 성공
};

// C2S: 방 입장 요청
struct MSG_C2S_JOIN_ROOM
{
    MsgHeader header;
    int32_t roomId;
};

// S2C: 방 입장 응답
struct MSG_S2C_ROOM_JOINED
{
    MsgHeader header;
    int32_t roomId;
    uint8_t success;
};

// C2S: 방 퇴장 요청
struct MSG_C2S_LEAVE_ROOM
{
    MsgHeader header;
};

// S2C: 방 퇴장 응답
struct MSG_S2C_ROOM_LEFT
{
    MsgHeader header;
    uint8_t success;
};

// S2C: 에러 응답
struct MSG_S2C_ERROR
{
    MsgHeader header;
    char message[256];
};

#pragma pack(pop)
