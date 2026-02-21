#include "OpusCodec.h"

#include <opus.h>

#include <algorithm>
#include <array>

namespace {
constexpr int kSampleRate = 48000;
constexpr int kChannels = 1;
constexpr int kFrameMs = 20;
constexpr int kFrameSamples = (kSampleRate * kFrameMs) / 1000; // 960
constexpr int kFrameBytes = kFrameSamples * kChannels * static_cast<int>(sizeof(opus_int16)); // 1920
constexpr int kMaxOpusPacketBytes = 512;
}

OpusCodec::OpusCodec() {
    int err = 0;
    encoderLow_ = opus_encoder_create(kSampleRate, kChannels, OPUS_APPLICATION_VOIP, &err);
    if (!encoderLow_ || err != OPUS_OK) {
        encoderLow_ = nullptr;
        return;
    }

    encoderHigh_ = opus_encoder_create(kSampleRate, kChannels, OPUS_APPLICATION_VOIP, &err);
    if (!encoderHigh_ || err != OPUS_OK) {
        opus_encoder_destroy(encoderLow_);
        encoderLow_ = nullptr;
        encoderHigh_ = nullptr;
        return;
    }

    opus_encoder_ctl(encoderLow_, OPUS_SET_BITRATE(16000));
    opus_encoder_ctl(encoderLow_, OPUS_SET_COMPLEXITY(4));
    opus_encoder_ctl(encoderLow_, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    opus_encoder_ctl(encoderLow_, OPUS_SET_INBAND_FEC(1));
    opus_encoder_ctl(encoderLow_, OPUS_SET_PACKET_LOSS_PERC(15));
    opus_encoder_ctl(encoderLow_, OPUS_SET_DTX(0));

    opus_encoder_ctl(encoderHigh_, OPUS_SET_BITRATE(32000));
    opus_encoder_ctl(encoderHigh_, OPUS_SET_COMPLEXITY(6));
    opus_encoder_ctl(encoderHigh_, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    opus_encoder_ctl(encoderHigh_, OPUS_SET_INBAND_FEC(1));
    opus_encoder_ctl(encoderHigh_, OPUS_SET_PACKET_LOSS_PERC(10));
    opus_encoder_ctl(encoderHigh_, OPUS_SET_DTX(0));
}

OpusCodec::~OpusCodec() {
    if (encoderLow_) {
        opus_encoder_destroy(encoderLow_);
        encoderLow_ = nullptr;
    }
    if (encoderHigh_) {
        opus_encoder_destroy(encoderHigh_);
        encoderHigh_ = nullptr;
    }

    for (auto it = decoders_.begin(); it != decoders_.end(); ++it) {
        if (it.value()) {
            opus_decoder_destroy(it.value());
        }
    }
    decoders_.clear();
}

bool OpusCodec::isReady() const {
    return encoderLow_ != nullptr && encoderHigh_ != nullptr;
}

bool OpusCodec::encodeFrame(const QByteArray &pcm16le, QByteArray &opusPayload) {
    return encodeFrameHigh(pcm16le, opusPayload);
}

bool OpusCodec::encodeFrameLow(const QByteArray &pcm16le, QByteArray &opusPayload) {
    return encodeWithEncoder(encoderLow_, pcm16le, opusPayload);
}

bool OpusCodec::encodeFrameHigh(const QByteArray &pcm16le, QByteArray &opusPayload) {
    return encodeWithEncoder(encoderHigh_, pcm16le, opusPayload);
}

bool OpusCodec::encodeWithEncoder(OpusEncoder *enc, const QByteArray &pcm16le, QByteArray &opusPayload) {
    opusPayload.clear();
    if (!enc || pcm16le.size() != kFrameBytes) {
        return false;
    }

    std::array<unsigned char, kMaxOpusPacketBytes> out{};
    const auto *samples = reinterpret_cast<const opus_int16 *>(pcm16le.constData());
    const opus_int32 n = opus_encode(enc, samples, kFrameSamples, out.data(), kMaxOpusPacketBytes);
    if (n <= 0) {
        return false;
    }

    opusPayload = QByteArray(reinterpret_cast<const char *>(out.data()), static_cast<int>(n));
    return true;
}

bool OpusCodec::decodeFrame(uint32_t ssrc, const QByteArray &opusPayload, QByteArray &pcm16leOut) {
    pcm16leOut.clear();
    OpusDecoder *decoder = ensureDecoder(ssrc);
    if (!decoder || opusPayload.isEmpty()) {
        return false;
    }

    std::array<opus_int16, kFrameSamples * kChannels> pcm{};
    const int decoded = opus_decode(
        decoder,
        reinterpret_cast<const unsigned char *>(opusPayload.constData()),
        static_cast<opus_int32>(opusPayload.size()),
        pcm.data(),
        kFrameSamples,
        0);
    if (decoded <= 0) {
        return false;
    }

    pcm16leOut = QByteArray(reinterpret_cast<const char *>(pcm.data()),
                            decoded * kChannels * static_cast<int>(sizeof(opus_int16)));
    return true;
}

bool OpusCodec::decodeFecFromNext(uint32_t ssrc, const QByteArray &nextOpusPayload, QByteArray &pcm16leOut) {
    pcm16leOut.clear();
    OpusDecoder *decoder = ensureDecoder(ssrc);
    if (!decoder || nextOpusPayload.isEmpty()) {
        return false;
    }

    std::array<opus_int16, kFrameSamples * kChannels> pcm{};
    const int decoded = opus_decode(
        decoder,
        reinterpret_cast<const unsigned char *>(nextOpusPayload.constData()),
        static_cast<opus_int32>(nextOpusPayload.size()),
        pcm.data(),
        kFrameSamples,
        1);
    if (decoded <= 0) {
        return false;
    }

    pcm16leOut = QByteArray(reinterpret_cast<const char *>(pcm.data()),
                            decoded * kChannels * static_cast<int>(sizeof(opus_int16)));
    return true;
}

bool OpusCodec::decodePlc(uint32_t ssrc, QByteArray &pcm16leOut) {
    pcm16leOut.clear();
    OpusDecoder *decoder = ensureDecoder(ssrc);
    if (!decoder) {
        return false;
    }

    std::array<opus_int16, kFrameSamples * kChannels> pcm{};
    const int decoded = opus_decode(decoder, nullptr, 0, pcm.data(), kFrameSamples, 1);
    if (decoded <= 0) {
        return false;
    }

    pcm16leOut = QByteArray(reinterpret_cast<const char *>(pcm.data()),
                            decoded * kChannels * static_cast<int>(sizeof(opus_int16)));
    return true;
}

bool OpusCodec::setBitrate(int bps, int expectedLossPct) {
    if (!encoderLow_ || !encoderHigh_) {
        return false;
    }

    const int highBitrate = std::clamp(bps, 12000, 64000);
    const int lowBitrate = std::clamp(highBitrate / 2, 12000, 32000);
    const int loss = std::clamp(expectedLossPct, 0, 40);
    const int rc1 = opus_encoder_ctl(encoderHigh_, OPUS_SET_BITRATE(highBitrate));
    const int rc2 = opus_encoder_ctl(encoderHigh_, OPUS_SET_PACKET_LOSS_PERC(loss));
    const int rc3 = opus_encoder_ctl(encoderLow_, OPUS_SET_BITRATE(lowBitrate));
    const int rc4 = opus_encoder_ctl(encoderLow_, OPUS_SET_PACKET_LOSS_PERC(std::min(loss + 8, 40)));
    return rc1 == OPUS_OK && rc2 == OPUS_OK && rc3 == OPUS_OK && rc4 == OPUS_OK;
}

OpusDecoder *OpusCodec::ensureDecoder(uint32_t ssrc) {
    auto it = decoders_.find(ssrc);
    if (it != decoders_.end() && it.value()) {
        return it.value();
    }

    int err = 0;
    OpusDecoder *dec = opus_decoder_create(kSampleRate, kChannels, &err);
    if (!dec || err != OPUS_OK) {
        return nullptr;
    }

    decoders_.insert(ssrc, dec);
    return dec;
}
