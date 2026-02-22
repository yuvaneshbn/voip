#include "NoiseSuppressor.h"

#include <cstring>
#include <speex/speex_preprocess.h>

NoiseSuppressor::NoiseSuppressor() {
    speex_preprocess_ = speex_preprocess_state_init(960, 48000);
    if (speex_preprocess_) {
        int denoise = 1;
        int noise_suppress_db = -25;
        speex_preprocess_ctl(speex_preprocess_, SPEEX_PREPROCESS_SET_DENOISE, &denoise);
        speex_preprocess_ctl(speex_preprocess_, SPEEX_PREPROCESS_SET_NOISE_SUPPRESS, &noise_suppress_db);
    }
}

NoiseSuppressor::~NoiseSuppressor() {
    if (speex_preprocess_) {
        speex_preprocess_state_destroy(speex_preprocess_);
    }
}

void NoiseSuppressor::process(int16_t* pcm, size_t frames, size_t channels) {
    if (!pcm || frames == 0 || channels == 0) {
        return;
    }

    if (speex_preprocess_) {
        speex_preprocess_run(speex_preprocess_, pcm);
        return;
    }

    (void)pcm;
}

void NoiseSuppressor::reset() {
    // No-op for now
}
