// 📁 network/packet.h
// RTP-LIKE PACKET STRUCTURE (Packed binary format)
#pragma once

#include <cstdint>
#include "constants.h"

// Wire format: Must be packed for direct UDP serialization
#pragma pack(push, 1)
struct AudioPacket {
    uint32_t ssrc;                      // Synchronization source (caller ID)
    uint16_t seq;                       // Sequence number (for reordering)
    uint32_t timestamp;                 // RTP timestamp (sample-based)
    uint16_t payload_len;               // Actual payload size
    uint8_t  flags;                     // Bit 0: positional data present
    uint8_t  reserved[3];               // Alignment / future use
    float    position[3];               // Positional audio (x,y,z) in meters
    uint8_t  payload[OPUS_MAX_PAYLOAD]; // Opus-encoded audio data
};
#pragma pack(pop)

// Static size verification at compile time
static_assert(sizeof(AudioPacket) == 28 + OPUS_MAX_PAYLOAD, 
              "AudioPacket size mismatch");

constexpr uint8_t AUDIO_FLAG_POSITIONAL = 1 << 0;
