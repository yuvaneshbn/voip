// 📁 codec/opus_codec.h
// OPUS AUDIO CODEC (Encode/Decode with conditional FEC)
#pragma once

#include <opus.h>
#include <cstdint>
#include <stdexcept>
#include "constants.h"

class OpusCodec {
public:
    // Constructor: wifi_mode enables Forward Error Correction
    explicit OpusCodec(bool wifi_mode = false);
    
    // Destructor: safely frees encoder/decoder
    ~OpusCodec();

    // Encode PCM samples to Opus bitstream
    // Input:  pcm = 16-bit signed PCM samples (FRAME_SIZE samples)
    // Output: Returns encoded packet size in bytes, or negative on error
    int encode(const int16_t* pcm, uint8_t* out);

    // Decode Opus bitstream to PCM samples
    // Input:  data = Opus bitstream (nullptr triggers PLC)
    // Output: pcm_out = 16-bit signed PCM samples (FRAME_SIZE samples)
    // Returns: Number of decoded samples, or negative on error
    int decode(const uint8_t* data, int len, int16_t* pcm_out);

    // Get current encoder bitrate (for diagnostics)
    int get_bitrate() const;

private:
    OpusEncoder* encoder_;
    OpusDecoder* decoder_;
};
