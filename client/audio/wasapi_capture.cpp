// 📁 audio/wasapi_capture.cpp
// WASAPI CAPTURE IMPLEMENTATION
#ifdef _WIN32

#include "wasapi_capture.h"
#include "constants.h"
#include <windows.h>
#include <mmreg.h>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>


WasapiCapture::WasapiCapture()
    : audio_(nullptr),
      capture_(nullptr),
      event_(nullptr),
      fmt_(nullptr),
      channels_(0),
      sample_rate_(0),
      is_float_(false) {}

WasapiCapture::~WasapiCapture() {
    stop();
    release_resources();
}

void WasapiCapture::release_resources() {
    if (capture_) {
        capture_->Release();
        capture_ = nullptr;
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

bool WasapiCapture::init() {
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

    hr = enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device);
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
        std::cout << "[WASAPI] Resampling capture from " << sample_rate_
                  << " to " << SAMPLE_RATE << "\n";
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
        // fallback to default buffer duration
        hr = audio_->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                0, 0, fmt_, nullptr);
    }
    if (FAILED(hr)) {
        std::cerr << "[WASAPI] IAudioClient::Initialize failed\n";
        if (com_inited) CoUninitialize();
        return false;
    }

    hr = audio_->GetService(IID_PPV_ARGS(&capture_));
    if (FAILED(hr)) {
        std::cerr << "[WASAPI] GetService(IAudioCaptureClient) failed\n";
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

    resampler_.set_rates(sample_rate_, SAMPLE_RATE);
    return true;
}

void WasapiCapture::start(std::atomic<bool>& running, Callback cb) {
    if (!audio_ || !capture_) return;
    stream_started_ = false;

    while (running) {
        const bool should_capture = enabled_.load();
        if (should_capture != stream_started_) {
            if (should_capture) {
                HRESULT hr = audio_->Start();
                if (FAILED(hr)) {
                    std::cerr << "[WASAPI] Capture Start failed\n";
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    continue;
                }
                stream_started_ = true;
            } else {
                audio_->Stop();
                audio_->Reset();
                pending_.clear();
                stream_started_ = false;
            }
        }

        if (!stream_started_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        DWORD wait = WaitForSingleObject(event_, 2000);
        if (wait != WAIT_OBJECT_0) continue;

        UINT32 packet_frames = 0;
        capture_->GetNextPacketSize(&packet_frames);

        while (packet_frames > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;

            HRESULT hr = capture_->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
            if (FAILED(hr)) break;

            if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                std::vector<int16_t> zeros(frames, 0);
                convert_and_queue(reinterpret_cast<uint8_t*>(zeros.data()), (int)frames);
            } else {
                convert_and_queue(data, (int)frames);
            }

            capture_->ReleaseBuffer(frames);
            capture_->GetNextPacketSize(&packet_frames);
        }

        if (sample_rate_ == SAMPLE_RATE) {
            while ((int)pending_.size() >= FRAME_SIZE) {
                cb(pending_.data(), FRAME_SIZE);
                pending_.erase(pending_.begin(), pending_.begin() + FRAME_SIZE);
            }
        } else {
            // Resample to 48k before feeding engine
            std::vector<int16_t> resampled;
            while (resampler_.pop(FRAME_SIZE, resampled)) {
                cb(resampled.data(), FRAME_SIZE);
            }
        }
    }

    if (stream_started_) {
        audio_->Stop();
        audio_->Reset();
        stream_started_ = false;
    }

    release_resources();
    CoUninitialize();
}

void WasapiCapture::stop() {
    if (audio_) {
        audio_->Stop();
        audio_->Reset();
    }
    stream_started_ = false;
}

void WasapiCapture::set_enabled(bool enabled) {
    enabled_.store(enabled);
}

bool WasapiCapture::is_enabled() const {
    return enabled_.load();
}

void WasapiCapture::convert_and_queue(const uint8_t* data, int frames) {
    if (frames <= 0) return;

    if (is_float_) {
        const float* f = reinterpret_cast<const float*>(data);
        for (int i = 0; i < frames; ++i) {
            float sample = 0.0f;
            if (channels_ == 1) {
                sample = f[i];
            } else {
                sample = 0.5f * (f[i * 2] + f[i * 2 + 1]);
            }
            float clamped = std::max(-1.0f, std::min(1.0f, sample));
            pending_.push_back(static_cast<int16_t>(clamped * 32767.0f));
        }
    } else {
        const int16_t* s = reinterpret_cast<const int16_t*>(data);
        for (int i = 0; i < frames; ++i) {
            int16_t sample = 0;
            if (channels_ == 1) {
                sample = s[i];
            } else {
                int32_t mix = (int32_t)s[i * 2] + (int32_t)s[i * 2 + 1];
                sample = static_cast<int16_t>(mix / 2);
            }
            pending_.push_back(sample);
        }
    }

    if (sample_rate_ != SAMPLE_RATE) {
        resampler_.push(pending_.data(), static_cast<int>(pending_.size()));
        pending_.clear();
    }
}

#endif
