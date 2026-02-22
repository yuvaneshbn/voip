// 📁 audio/wasapi_playback.h
// WASAPI PLAYBACK (Event-driven, 48kHz mono)
#pragma once

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif

#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <vector>

#include "resampler.h"

class WasapiPlayback {
public:
    using Callback = std::function<void(int16_t* pcm, int frames)>;

    WasapiPlayback();
    ~WasapiPlayback();

    bool init();
    void start(std::atomic<bool>& running, Callback cb);
    void stop();

private:
    IAudioClient* audio_;
    IAudioRenderClient* render_;
    HANDLE event_;
    WAVEFORMATEX* fmt_;
    int channels_;
    int sample_rate_;
    bool is_float_;
    UINT32 buffer_frames_;

    std::vector<int16_t> pending_;
    LinearResampler resampler_;

    void release_resources();
    void fill_output(uint8_t* out, int frames, Callback& cb);
};

#endif
