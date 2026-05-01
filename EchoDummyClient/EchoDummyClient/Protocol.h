#pragma once

#include <cstdint>

#pragma pack(push, 1)
struct MsgHeader
{
    uint16_t size;
};
#pragma pack(pop)

constexpr uint16_t ECHO_BODY_SIZE = static_cast<uint16_t>(sizeof(uint64_t));
constexpr uint16_t ECHO_TOTAL_SIZE = static_cast<uint16_t>(sizeof(MsgHeader) + sizeof(uint64_t));
