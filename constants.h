// ðŸ“ constants.h
// GLOBAL AUDIO & NETWORK CONSTANTS
#pragma once

#include <cstdint>

// ================= AUDIO =================
constexpr int SAMPLE_RATE = 48000;
constexpr int CHANNELS = 1;
constexpr int FRAME_MS = 20;
constexpr int FRAME_SIZE = SAMPLE_RATE * FRAME_MS / 1000;

// ================= OPUS =================
constexpr int OPUS_BITRATE = 24000;
constexpr int OPUS_COMPLEXITY = 5;
constexpr int OPUS_MAX_PAYLOAD = 4000;

// ================= NETWORK =================
constexpr uint16_t DEFAULT_AUDIO_PORT = 5004;
constexpr uint16_t DEFAULT_CONTROL_PORT = 5005;

// ================= JITTER =================
constexpr int INITIAL_BUFFER_LAN = 2;
constexpr int MAX_BUFFER_LAN = 6;
constexpr int INITIAL_BUFFER_PKT = 6;
constexpr int MAX_BUFFER_PKT = 20;

// ================= MODE =================
enum class NetworkMode {
    LAN,
    WIFI,
    MOBILE
};
