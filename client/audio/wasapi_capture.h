// 📁 audio/wasapi_capture.h
// WASAPI CAPTURE (Event-driven, 48kHz mono)
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

class WasapiCapture {
public:
    using Callback = std::function<void(const int16_t* pcm, int frames)>;

    WasapiCapture();
    ~WasapiCapture();

    bool init();
    void start(std::atomic<bool>& running, Callback cb);
    void stop();
    void set_enabled(bool enabled);
    bool is_enabled() const;

private:
    IAudioClient* audio_;
    IAudioCaptureClient* capture_;
    HANDLE event_;
    WAVEFORMATEX* fmt_;
    int channels_;
    int sample_rate_;
    bool is_float_;
    std::vector<int16_t> pending_;
    LinearResampler resampler_;
    std::atomic<bool> enabled_{false};
    bool stream_started_ = false;

    void release_resources();
    void convert_and_queue(const uint8_t* data, int frames);
};

#endif
