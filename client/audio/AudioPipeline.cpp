#include "AudioPipeline.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "AudioTransportShim.h"
#include <speex/speex_echo.h>
#include <speex/speex_preprocess.h>

AudioPipeline::AudioPipeline()
    : AudioPipeline(Config{}) {}

AudioPipeline::AudioPipeline(const Config& cfg)
    : cfg_(cfg) {
    const int frame_size = static_cast<int>(cfg_.frame_size);
    const int tail_length = 6144;  // ~128 ms at 48 kHz.
    int sample_rate = static_cast<int>(cfg_.sample_rate);

    speex_echo_state_ = speex_echo_state_init_mc(frame_size, tail_length, 1, 1);
    if (speex_echo_state_) {
        speex_echo_ctl(speex_echo_state_, SPEEX_ECHO_SET_SAMPLING_RATE, &sample_rate);
    }

    speex_preprocess_ = speex_preprocess_state_init(frame_size, sample_rate);
    if (speex_preprocess_) {
        speex_preprocess_ctl(speex_preprocess_, SPEEX_PREPROCESS_SET_ECHO_STATE, speex_echo_state_);

        // Recommended starting preset for home/office use.
        int denoise = 1;
        int noise_suppress_db = -38;
        int agc_enabled = 1;
        int agc_level = 18000;
        int agc_max_gain = 30;
        int vad_enabled = 1;
        int dereverb = 1;
        int echo_suppress = -40;
        int echo_suppress_active = -15;

        speex_preprocess_ctl(speex_preprocess_, SPEEX_PREPROCESS_SET_DENOISE, &denoise);
        speex_preprocess_ctl(speex_preprocess_, SPEEX_PREPROCESS_SET_NOISE_SUPPRESS, &noise_suppress_db);
        speex_preprocess_ctl(speex_preprocess_, SPEEX_PREPROCESS_SET_AGC, &agc_enabled);
        speex_preprocess_ctl(speex_preprocess_, SPEEX_PREPROCESS_SET_AGC_LEVEL, &agc_level);
        speex_preprocess_ctl(speex_preprocess_, SPEEX_PREPROCESS_SET_AGC_MAX_GAIN, &agc_max_gain);
        speex_preprocess_ctl(speex_preprocess_, SPEEX_PREPROCESS_SET_VAD, &vad_enabled);
        speex_preprocess_ctl(speex_preprocess_, SPEEX_PREPROCESS_SET_DEREVERB, &dereverb);
        speex_preprocess_ctl(speex_preprocess_, SPEEX_PREPROCESS_SET_ECHO_SUPPRESS, &echo_suppress);
        speex_preprocess_ctl(speex_preprocess_, SPEEX_PREPROCESS_SET_ECHO_SUPPRESS_ACTIVE, &echo_suppress_active);
    }
}

AudioPipeline::~AudioPipeline() {
    if (speex_preprocess_) {
        speex_preprocess_state_destroy(speex_preprocess_);
    }
    if (speex_echo_state_) {
        speex_echo_state_destroy(speex_echo_state_);
    }
}

void AudioPipeline::set_audio_transport(webrtc::AudioTransport* transport) {
    transport_ = transport;
}

void AudioPipeline::set_loopback_enabled(bool enabled) {
    loopback_enabled_ = enabled;
}

void AudioPipeline::process_playout(int16_t* pcm, size_t frames, size_t channels) {
    if (!pcm || frames == 0 || channels == 0) {
        return;
    }

    const size_t samples = frames * channels;
    last_playout_.assign(pcm, pcm + samples);

    if (speex_echo_state_ && channels == 1 && frames == cfg_.frame_size) {
        speex_echo_playback(speex_echo_state_, pcm);
    }
}

void AudioPipeline::process_capture(int16_t* pcm, size_t frames, size_t channels) {
    if (!pcm || frames == 0 || channels == 0) {
        return;
    }

    if (speex_echo_state_ && !last_playout_.empty() && channels == 1 && frames == cfg_.frame_size) {
        std::vector<spx_int16_t> out(frames * channels);
        speex_echo_capture(speex_echo_state_, pcm, out.data());
        std::memcpy(pcm, out.data(), frames * channels * sizeof(int16_t));
        if (speex_preprocess_) {
            speex_preprocess_run(speex_preprocess_, pcm);
        }
    } else if (speex_preprocess_) {
        speex_preprocess_run(speex_preprocess_, pcm);
    }

    if (loopback_enabled_) {
        for (size_t i = 0; i < frames * channels; ++i) {
            pcm[i] = static_cast<int16_t>(pcm[i] / 2);
        }
    }

    if (transport_) {
        uint32_t new_level = 0;
        transport_->RecordedDataIsAvailable(
            pcm,
            frames,
            sizeof(int16_t),
            channels,
            cfg_.sample_rate,
            0,
            0,
            0,
            false,
            new_level);
    }
}

void AudioPipeline::reset() {
    last_playout_.clear();
}
