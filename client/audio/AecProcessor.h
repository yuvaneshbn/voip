#pragma once

#include <QByteArray>

#include <cstdint>
#include <vector>

class AecProcessor {
public:
    AecProcessor();
    ~AecProcessor();

    bool initialize(int sampleRate, int frameSamples);
    void reset();
    bool isReady() const;
    QByteArray processFrame(const QByteArray &nearPcm16le, const QByteArray &farPcm16le);

private:
    int sampleRate_ = 0;
    int frameSamples_ = 0;
    bool ready_ = false;

#if defined(NOX_HAS_SPEEXDSP_AEC)
    struct SpeexEchoState_ *echoState_ = nullptr;
    struct SpeexPreprocessState_ *preState_ = nullptr;
#endif
};

