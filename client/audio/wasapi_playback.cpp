// 📁 audio/wasapi_playback.cpp
// WASAPI PLAYBACK IMPLEMENTATION
#ifdef _WIN32

#include "wasapi_playback.h"
#include "constants.h"
#include <windows.h>
#include <mmreg.h>

#include <algorithm>
#include <cstring>
#include <iostream>

WasapiPlayback::WasapiPlayback()
    : audio_(nullptr),
      render_(nullptr),
      event_(nullptr),
      fmt_(nullptr),
      channels_(0),
      sample_rate_(0),
      is_float_(false),
      buffer_frames_(0) {}

WasapiPlayback::~WasapiPlayback() {
    stop();
    release_resources();
}

void WasapiPlayback::release_resources() {
    if (render_) {
        render_->Release();
        render_ = nullptr;
    }
    if (audio_) {
        audio_->Release();
        audio_ = nullptr;
    }
    if (event_) {
        CloseHandle(event_);
        event_ = nullptr;
    }
    if (fmt_) {
        CoTaskMemFree(fmt_);
        fmt_ = nullptr;
    }
}

bool WasapiPlayback::init() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        std::cerr << "[WASAPI] CoInitializeEx failed\n";
        return false;
    }
    bool com_inited = true;

    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                          CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) {
        std::cerr << "[WASAPI] MMDeviceEnumerator failed\n";
        if (com_inited) CoUninitialize();
        return false;
    }

    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    enumerator->Release();
    if (FAILED(hr)) {
        std::cerr << "[WASAPI] GetDefaultAudioEndpoint failed\n";
        if (com_inited) CoUninitialize();
        return false;
    }

    hr = device->Activate(__uuidof(IAudioClient),
                          CLSCTX_ALL, nullptr, (void**)&audio_);
    device->Release();
    if (FAILED(hr)) {
        std::cerr << "[WASAPI] Activate IAudioClient failed\n";
        if (com_inited) CoUninitialize();
        return false;
    }

    // Request 48kHz mono PCM
    WAVEFORMATEX desired{};
    desired.wFormatTag = WAVE_FORMAT_PCM;
    desired.nChannels = CHANNELS;
    desired.nSamplesPerSec = SAMPLE_RATE;
    desired.wBitsPerSample = 16;
    desired.nBlockAlign = desired.nChannels * desired.wBitsPerSample / 8;
    desired.nAvgBytesPerSec = desired.nSamplesPerSec * desired.nBlockAlign;

    WAVEFORMATEX* closest = nullptr;
    hr = audio_->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, &desired, &closest);
    if (hr == S_OK) {
        fmt_ = (WAVEFORMATEX*)CoTaskMemAlloc(sizeof(WAVEFORMATEX));
        std::memcpy(fmt_, &desired, sizeof(WAVEFORMATEX));
    } else if (hr == S_FALSE && closest) {
        fmt_ = closest;
    } else {
        audio_->GetMixFormat(&fmt_);
    }

    if (!fmt_) {
        std::cerr << "[WASAPI] No supported format\n";
        if (com_inited) CoUninitialize();
        return false;
    }

    sample_rate_ = fmt_->nSamplesPerSec;
    channels_ = fmt_->nChannels;
    is_float_ = (fmt_->wFormatTag == WAVE_FORMAT_IEEE_FLOAT);
    if (fmt_->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        auto* ext = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(fmt_);
        static const GUID kIeeeFloat = {0x00000003, 0x0000, 0x0010,
                                        {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
        is_float_ = (ext->SubFormat == kIeeeFloat);
    }

    if (sample_rate_ != SAMPLE_RATE) {
        std::cout << "[WASAPI] Resampling playback from " << SAMPLE_RATE
                  << " to " << sample_rate_ << "\n";
    }
    if (channels_ != 1 && channels_ != 2) {
        std::cerr << "[WASAPI] Unsupported channel count: " << channels_ << "\n";
        if (com_inited) CoUninitialize();
        return false;
    }

    REFERENCE_TIME buffer_duration = 20 * 10000;  // 20ms
    hr = audio_->Initialize(AUDCLNT_SHAREMODE_SHARED,
                            AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                            buffer_duration, 0, fmt_, nullptr);
    if (FAILED(hr)) {
        hr = audio_->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                0, 0, fmt_, nullptr);
    }
    if (FAILED(hr)) {
        std::cerr << "[WASAPI] IAudioClient::Initialize failed\n";
        if (com_inited) CoUninitialize();
        return false;
    }

    hr = audio_->GetService(IID_PPV_ARGS(&render_));
    if (FAILED(hr)) {
        std::cerr << "[WASAPI] GetService(IAudioRenderClient) failed\n";
        if (com_inited) CoUninitialize();
        return false;
    }

    hr = audio_->GetBufferSize(&buffer_frames_);
    if (FAILED(hr)) {
        std::cerr << "[WASAPI] GetBufferSize failed\n";
        if (com_inited) CoUninitialize();
        return false;
    }

    event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!event_) {
        std::cerr << "[WASAPI] CreateEvent failed\n";
        if (com_inited) CoUninitialize();
        return false;
    }
    audio_->SetEventHandle(event_);

    resampler_.set_rates(SAMPLE_RATE, sample_rate_);
    return true;
}

void WasapiPlayback::start(std::atomic<bool>& running, Callback cb) {
    if (!audio_ || !render_) return;

    HRESULT hr = audio_->Start();
    if (FAILED(hr)) {
        std::cerr << "[WASAPI] Playback Start failed\n";
        return;
    }

    while (running) {
        DWORD wait = WaitForSingleObject(event_, 2000);
        if (wait != WAIT_OBJECT_0) continue;

        UINT32 padding = 0;
        audio_->GetCurrentPadding(&padding);
        UINT32 frames_available = buffer_frames_ > padding ? (buffer_frames_ - padding) : 0;
        if (frames_available == 0) continue;

        BYTE* data = nullptr;
        hr = render_->GetBuffer(frames_available, &data);
        if (FAILED(hr)) continue;

        fill_output(data, (int)frames_available, cb);

        render_->ReleaseBuffer(frames_available, 0);
    }

    release_resources();
    CoUninitialize();
}

void WasapiPlayback::stop() {
    if (audio_) {
        audio_->Stop();
        audio_->Reset();
    }
}

void WasapiPlayback::fill_output(uint8_t* out, int frames, Callback& cb) {
    std::cout << "[PLAY] WASAPI requesting " << frames
              << " frames | pending samples=" << pending_.size() << std::endl;
    if (sample_rate_ == SAMPLE_RATE) {
        while ((int)pending_.size() < frames) {
            std::vector<int16_t> temp(FRAME_SIZE);
            cb(temp.data(), FRAME_SIZE);
            pending_.insert(pending_.end(), temp.begin(), temp.end());
        }

        if (is_float_) {
            float* f = reinterpret_cast<float*>(out);
            for (int i = 0; i < frames; ++i) {
                float sample = pending_[i] / 32768.0f;
                if (channels_ == 1) {
                    f[i] = sample;
                } else {
                    f[i * 2] = sample;
                    f[i * 2 + 1] = sample;
                }
            }
        } else {
            int16_t* s = reinterpret_cast<int16_t*>(out);
            for (int i = 0; i < frames; ++i) {
                int16_t sample = pending_[i];
                if (channels_ == 1) {
                    s[i] = sample;
                } else {
                    s[i * 2] = sample;
                    s[i * 2 + 1] = sample;
                }
            }
        }

        pending_.erase(pending_.begin(), pending_.begin() + frames);
        std::cout << "[PLAY] Successfully wrote " << frames << " frames to speaker" << std::endl;
        return;
    }

    // Resample from 48k to device rate
    std::vector<int16_t> resampled;
    while (!resampler_.pop(frames, resampled)) {
        std::vector<int16_t> temp(FRAME_SIZE);
        cb(temp.data(), FRAME_SIZE);
        resampler_.push(temp.data(), FRAME_SIZE);
    }

    if (is_float_) {
        float* f = reinterpret_cast<float*>(out);
        for (int i = 0; i < frames; ++i) {
            float sample = resampled[i] / 32768.0f;
            if (channels_ == 1) {
                f[i] = sample;
            } else {
                f[i * 2] = sample;
                f[i * 2 + 1] = sample;
            }
        }
    } else {
        int16_t* s = reinterpret_cast<int16_t*>(out);
        for (int i = 0; i < frames; ++i) {
            int16_t sample = resampled[i];
            if (channels_ == 1) {
                s[i] = sample;
            } else {
                s[i * 2] = sample;
                s[i * 2 + 1] = sample;
            }
        }
    }
    std::cout << "[PLAY] Successfully wrote " << frames << " frames to speaker" << std::endl;
}

#endif
