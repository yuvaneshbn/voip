#pragma once

#include <QByteArray>
#include <QHash>
#include <QSet>

#include <cstdint>

struct OpusDecoder;
struct OpusEncoder;

class OpusCodec {
public:
    OpusCodec();
    ~OpusCodec();

    bool isReady() const;
    bool encodeFrame(const QByteArray &pcm16le, QByteArray &opusPayload);
    bool encodeFrameLow(const QByteArray &pcm16le, QByteArray &opusPayload);
    bool encodeFrameHigh(const QByteArray &pcm16le, QByteArray &opusPayload);
    bool decodeFrame(uint32_t ssrc, const QByteArray &opusPayload, QByteArray &pcm16leOut);
    bool decodeFecFromNext(uint32_t ssrc, const QByteArray &nextOpusPayload, QByteArray &pcm16leOut);
    bool decodePlc(uint32_t ssrc, QByteArray &pcm16leOut);
    bool setBitrate(int bps, int expectedLossPct);
    void removeDecoder(uint32_t ssrc);
    void retainDecoders(const QSet<uint32_t> &activeSsrcs);

private:
    OpusDecoder *ensureDecoder(uint32_t ssrc);
    bool encodeWithEncoder(OpusEncoder *enc, const QByteArray &pcm16le, QByteArray &opusPayload);

    OpusEncoder *encoderLow_ = nullptr;
    OpusEncoder *encoderHigh_ = nullptr;
    QHash<uint32_t, OpusDecoder *> decoders_;
};
