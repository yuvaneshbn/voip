#pragma once

#include <cstddef>
#include <cstdint>

#include <speex/speex_preprocess.h>

class NoiseSuppressor {
public:
    NoiseSuppressor();
    ~NoiseSuppressor();

    void process(int16_t* pcm, size_t frames, size_t channels);
    void reset();

private:
    SpeexPreprocessState* speex_preprocess_ = nullptr;
};
