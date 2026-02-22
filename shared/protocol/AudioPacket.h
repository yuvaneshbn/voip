#pragma once

#include <cstdint>

struct AudioPacket {
    uint32_t ssrc = 0;
    uint16_t seq = 0;
    uint32_t timestamp = 0;
    uint16_t payload_len = 0;
};
