#include "AecProcessor.h"

#include <algorithm>
#include <cstring>

#if defined(NOX_HAS_SPEEXDSP_AEC)
#include <speex/speex_echo.h>
#include <speex/speex_preprocess.h>
#endif

namespace {
constexpr int kTailMs = 120;
}

AecProcessor::AecProcessor() = default;

AecProcessor::~AecProcessor() {
    reset();
}

bool AecProcessor::initialize(int sampleRate, int frameSamples) {
    reset();
    sampleRate_ = sampleRate;
    frameSamples_ = frameSamples;

#if defined(NOX_HAS_SPEEXDSP_AEC)
    if (sampleRate_ <= 0 || frameSamples_ <= 0) {
        return false;
    }
    const int tailSamples = (sampleRate_ * kTailMs) / 1000;
    echoState_ = speex_echo_state_init(frameSamples_, tailSamples);
    if (!echoState_) {
        return false;
    }
    speex_echo_ctl(echoState_, SPEEX_ECHO_SET_SAMPLING_RATE, &sampleRate_);

    preState_ = speex_preprocess_state_init(frameSamples_, sampleRate_);
    if (!preState_) {
        speex_echo_state_destroy(echoState_);
        echoState_ = nullptr;
        return false;
    }
    speex_preprocess_ctl(preState_, SPEEX_PREPROCESS_SET_ECHO_STATE, echoState_);
    ready_ = true;
    return true;
#else
    ready_ = false;
    return false;
#endif
}

void AecProcessor::reset() {
#if defined(NOX_HAS_SPEEXDSP_AEC)
    if (preState_) {
        speex_preprocess_state_destroy(preState_);
        preState_ = nullptr;
    }
    if (echoState_) {
        speex_echo_state_destroy(echoState_);
        echoState_ = nullptr;
    }
#endif
    ready_ = false;
}

bool AecProcessor::isReady() const {
    return ready_;
}

QByteArray AecProcessor::processFrame(const QByteArray &nearPcm16le, const QByteArray &farPcm16le) {
    if (!ready_ || nearPcm16le.isEmpty()) {
        return nearPcm16le;
    }

    const int samples = std::min<int>(frameSamples_, nearPcm16le.size() / static_cast<int>(sizeof(int16_t)));
    if (samples <= 0) {
        return nearPcm16le;
    }

    std::vector<int16_t> nearBuf(static_cast<size_t>(samples), 0);
    std::vector<int16_t> farBuf(static_cast<size_t>(samples), 0);
    std::vector<int16_t> outBuf(static_cast<size_t>(samples), 0);
    std::memcpy(nearBuf.data(), nearPcm16le.constData(), static_cast<size_t>(samples) * sizeof(int16_t));

    const int farSamples = std::min<int>(samples, farPcm16le.size() / static_cast<int>(sizeof(int16_t)));
    if (farSamples > 0) {
        std::memcpy(farBuf.data(), farPcm16le.constData(), static_cast<size_t>(farSamples) * sizeof(int16_t));
    }

#if defined(NOX_HAS_SPEEXDSP_AEC)
    speex_echo_cancellation(echoState_,
                            reinterpret_cast<spx_int16_t *>(nearBuf.data()),
                            reinterpret_cast<spx_int16_t *>(farBuf.data()),
                            reinterpret_cast<spx_int16_t *>(outBuf.data()));
    speex_preprocess_run(preState_, reinterpret_cast<spx_int16_t *>(outBuf.data()));
#else
    outBuf = nearBuf;
#endif

    QByteArray out;
    out.resize(samples * static_cast<int>(sizeof(int16_t)));
    std::memcpy(out.data(), outBuf.data(), static_cast<size_t>(out.size()));
    return out;
}

