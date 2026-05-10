#pragma once

#include <cstdint>

// 패킷 타입 (혼용 방지를 위해 L7 Msg로 표기)
enum class MsgType : uint16_t
{
	// C2S: Client to Server
	// S2C: Server to Client
    
    S2C_ERROR = 1000
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

// S2C: 에러 응답
struct MSG_S2C_ERROR
{
    MsgHeader header;
    char message[256];
};

#pragma pack(pop)
