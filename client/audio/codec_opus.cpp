// 📁 codec/opus_codec.cpp
// OPUS CODEC IMPLEMENTATION
#include "codec_opus.h"
#include <cstring>
#include <iostream>

OpusCodec::OpusCodec(bool wifi_mode) 
    : encoder_(nullptr), decoder_(nullptr) {
    
    int err = OPUS_OK;

    // Create encoder with VOIP application profile
    encoder_ = opus_encoder_create(SAMPLE_RATE, CHANNELS,
                                   OPUS_APPLICATION_VOIP, &err);
    if (err != OPUS_OK || !encoder_) {
        throw std::runtime_error("Failed to create Opus encoder");
    }

    // Create decoder
    decoder_ = opus_decoder_create(SAMPLE_RATE, CHANNELS, &err);
    if (err != OPUS_OK || !decoder_) {
        opus_encoder_destroy(encoder_);
        throw std::runtime_error("Failed to create Opus decoder");
    }

    // === ENCODER CONFIGURATION ===
    
    // Set bitrate (24 kbps optimal for VoIP quality/bandwidth tradeoff)
    opus_encoder_ctl(encoder_, OPUS_SET_BITRATE(OPUS_BITRATE));
    
    // Set complexity (5 = balanced between CPU and quality)
    opus_encoder_ctl(encoder_, OPUS_SET_COMPLEXITY(OPUS_COMPLEXITY));
    
    // Signal type = voice (improves codec tuning)
    opus_encoder_ctl(encoder_, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    
    // Wi-Fi mode: Enable FEC (Forward Error Correction)
    // This adds redundancy to recover from packet loss
    if (wifi_mode) {
        opus_encoder_ctl(encoder_, OPUS_SET_INBAND_FEC(1));
        // Assume 10% packet loss on Wi-Fi (can be adaptive)
        opus_encoder_ctl(encoder_, OPUS_SET_PACKET_LOSS_PERC(10));
        std::cout << "[Opus] Wi-Fi mode: FEC enabled\n";
    } else {
        // LAN mode: minimal FEC overhead
        opus_encoder_ctl(encoder_, OPUS_SET_INBAND_FEC(0));
        opus_encoder_ctl(encoder_, OPUS_SET_PACKET_LOSS_PERC(1));
        std::cout << "[Opus] LAN mode: FEC disabled\n";
    }

    // Use VBR (Variable Bitrate) for better quality at lower bitrates
    opus_encoder_ctl(encoder_, OPUS_SET_VBR(1));
    
    // Allow variable frame sizes? No - we use fixed 20ms frames
    opus_encoder_ctl(encoder_, OPUS_SET_VBR_CONSTRAINT(1));

    std::cout << "[Opus] Codec initialized: " << SAMPLE_RATE << "Hz, " 
              << OPUS_BITRATE << " bps\n";
}

OpusCodec::~OpusCodec() {
    if (encoder_) {
        opus_encoder_destroy(encoder_);
        encoder_ = nullptr;
    }
    if (decoder_) {
        opus_decoder_destroy(decoder_);
        decoder_ = nullptr;
    }
}

int OpusCodec::encode(const int16_t* pcm, uint8_t* out) {
    if (!encoder_ || !pcm || !out) {
        return OPUS_BAD_ARG;
    }

    int encoded_size = opus_encode(encoder_, pcm, FRAME_SIZE, out, OPUS_MAX_PAYLOAD);
    
    if (encoded_size < 0) {
        std::cerr << "[Opus] Encode error: " << encoded_size << "\n";
    }
    
    return encoded_size;
}

int OpusCodec::decode(const uint8_t* data, int len, int16_t* pcm_out) {
    if (!decoder_ || !pcm_out) {
        return OPUS_BAD_ARG;
    }

    // PLC (Packet Loss Concealment): data == nullptr triggers Opus PLC
    // This generates synthetic audio to mask packet loss
    int decode_fec = 0; // No FEC mode for receive side
    
    int decoded = opus_decode(decoder_, data, len, pcm_out, FRAME_SIZE, decode_fec);
    
    if (decoded < 0) {
        std::cerr << "[Opus] Decode error: " << decoded << "\n";
        // Fill with silence on decode error
        std::memset(pcm_out, 0, FRAME_SIZE * sizeof(int16_t));
        return 0;
    }
    
    return decoded;
}

int OpusCodec::get_bitrate() const {
    if (!encoder_) return -1;
    
    int bitrate = 0;
    opus_encoder_ctl(encoder_, OPUS_GET_BITRATE(&bitrate));
    return bitrate;
}
