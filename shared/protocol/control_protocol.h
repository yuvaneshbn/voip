// 📁 network/control_protocol.h
// CONTROL PLANE PROTOCOL (binary, packed)
#pragma once

#include <cstdint>

enum class CtrlType : uint8_t {
    PING = 1,
    PONG = 2,
    JOIN = 3,
    LEAVE = 4,
    USER_LIST = 5,
    TALK = 6,
    LIST = 7,
    MUTE = 8,
    UNMUTE = 9,
    SET_CHANNEL = 10
};

#pragma pack(push, 1)
struct CtrlHeader {
    CtrlType type;
    uint16_t size;  // payload size in bytes
};

struct CtrlJoin {
    uint32_t ssrc;
    char name[32];
};

struct CtrlLeave {
    uint32_t ssrc;
};

struct CtrlTalk {
    uint32_t from;
    uint16_t count;
    uint16_t reserved;
    // Followed by uint32_t targets[count]
};

struct CtrlMute {
    uint32_t from;
    uint32_t target;
};

struct CtrlSetChannel {
    uint32_t ssrc;
    uint32_t channel_id;
};

struct CtrlUserInfo {
    uint32_t ssrc;
    char name[32];
    uint8_t online;
    uint8_t reserved[3];
};

struct CtrlUserList {
    uint32_t count;
    // Followed by CtrlUserInfo[count]
};
#pragma pack(pop)
