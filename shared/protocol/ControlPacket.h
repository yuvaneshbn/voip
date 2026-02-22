#pragma once

#include <cstdint>

enum class ControlPacketType : uint8_t {
    Ping = 1,
    Pong = 2,
    Join = 3,
    Leave = 4,
    UserList = 5,
    Talk = 6,
    List = 7
};

struct ControlPacketHeader {
    ControlPacketType type;
    uint16_t size;
};
