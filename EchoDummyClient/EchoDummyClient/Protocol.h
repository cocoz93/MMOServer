#pragma once

#include <cstdint>

// 에코 전용 패킷 타입 (서버는 에코 모드에서 type을 검사하지 않음)
constexpr uint16_t ECHO_MSG_TYPE = 0;

#pragma pack(push, 1)
struct MsgHeader
{
    uint16_t size;        // 패킷 전체 크기 (헤더 포함)
    uint16_t type;        // 패킷 타입
};
#pragma pack(pop)

constexpr uint16_t ECHO_BODY_SIZE  = static_cast<uint16_t>(sizeof(uint64_t));
constexpr uint16_t ECHO_TOTAL_SIZE = static_cast<uint16_t>(sizeof(MsgHeader) + sizeof(uint64_t));
