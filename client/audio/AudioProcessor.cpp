// 📁 audio/audio_engine.cpp
// AUDIO PROCESSOR IMPLEMENTATION
#include "AudioProcessor.h"
#include <iostream>
#include <chrono>
#include <cstring>
#include <cmath>
#include <speex/speex_echo.h>

namespace {
// Temporary safety switch: disable aggressive input DSP that can suppress
// near-end speech in same-machine dual-client testing.
constexpr bool kEnableInputProcessing = false;
}

AudioProcessor::AudioProcessor(NetworkMode mode)
    : codec_(mode == NetworkMode::WIFI || mode == NetworkMode::MOBILE),
      jitter_(mode == NetworkMode::LAN ? INITIAL_BUFFER_LAN : INITIAL_BUFFER_PKT,
              mode == NetworkMode::LAN ? MAX_BUFFER_LAN : MAX_BUFFER_PKT),
      running_(false),
      mode_(mode),
      seq_counter_(0),
      timestamp_(0),
      positional_enabled_(false),
      positional_data_{ 0.0f, 0.0f, 0.0f },
      last_pos_{ 0.0f, 0.0f, 0.0f },
      last_pos_valid_(false) {
    std::cout << "[AudioProcessor] Created with mode " << 
              (mode == NetworkMode::LAN ? "LAN" : "Wi-Fi") << "\n";

    preprocessor_enabled_ = preprocessor_.init(SAMPLE_RATE, FRAME_SIZE);
    if (preprocessor_enabled_) {
        preprocessor_.setDenoise(true);
        preprocessor_.setNoiseSuppress(-20);
    }

    // 100 ms tail at 48 kHz (tune 2048..8192 if needed).
    const int tail_length = SAMPLE_RATE / 10;
    aec_state_ = speex_echo_state_init_mc(FRAME_SIZE, tail_length, 1, 1);
    if (aec_state_) {
        int sr = SAMPLE_RATE;
        speex_echo_ctl(aec_state_, SPEEX_ECHO_SET_SAMPLING_RATE, &sr);
    } else {
        std::cerr << "[AudioProcessor] Speex AEC init failed\n";
    }
}

AudioProcessor::~AudioProcessor() {
    stop();
    if (aec_state_) {
        speex_echo_state_destroy(aec_state_);
        aec_state_ = nullptr;
    }
}

void AudioProcessor::start() {
    if (running_) return;
    
    running_ = true;
    std::cout << "[AudioProcessor] Starting threads...\n";

#ifdef _WIN32
    wasapi_capture_ = std::make_unique<WasapiCapture>();
    wasapi_playback_ = std::make_unique<WasapiPlayback>();

    capture_thread_ = std::thread([this]() {
        if (!wasapi_capture_->init()) {
            std::cerr << "[AudioProcessor] WASAPI capture init failed\n";
            running_ = false;
            return;
        }
        wasapi_capture_->set_enabled(microphone_enabled_.load());
        wasapi_capture_->start(running_, [this](const int16_t* pcm, int frames) {
            if (!running_ || frames != FRAME_SIZE) return;

            int16_t local[FRAME_SIZE];
            std::memcpy(local, pcm, sizeof(local));
            if (kEnableInputProcessing && aec_state_) {
                speex_echo_capture(aec_state_, local, aec_capture_out_.data());
                std::memcpy(local, aec_capture_out_.data(), sizeof(local));
            }
            if (kEnableInputProcessing) {
                ns_.process(local, FRAME_SIZE, 1);
            }
            if (kEnableInputProcessing && preprocessor_enabled_) {
                preprocessor_.run(local);
            }

            if (loopback_enabled_.load()) {
                std::lock_guard<std::mutex> lock(loopback_mutex_);
                std::memcpy(loopback_buffer_.data(), local, sizeof(local));
                loopback_has_data_ = true;
            }

            uint8_t encoded[OPUS_MAX_PAYLOAD];
            int encoded_len = codec_.encode(local, encoded);
            if (encoded_len > 0) {
                if (send_cb_) {
                    const uint16_t seq = seq_counter_;
                    const uint32_t ts = timestamp_;
                    std::cout << "[Client] Encoded " << encoded_len
                              << " bytes, sending seq=" << seq << "\n";
                    std::array<float, 3> pos{};
                    bool has_pos = positional_enabled_.load();
                    if (has_pos) {
                        std::lock_guard<std::mutex> lock(positional_mutex_);
                        pos = positional_data_;
                    }
                    if (send_cb_(seq, ts, encoded, static_cast<size_t>(encoded_len), has_pos, pos.data())) {
                        ++seq_counter_;
                        timestamp_ += FRAME_SIZE;
                    }
                }
            }
        });
    });

    playback_thread_ = std::thread([this]() {
        if (!wasapi_playback_->init()) {
            std::cerr << "[AudioProcessor] WASAPI playback init failed\n";
            running_ = false;
            return;
        }
        wasapi_playback_->start(running_, [this](int16_t* out, int frames) {
            if (!running_ || frames != FRAME_SIZE) return;
            get_decoded_audio(out);
        });
    });
#else
    // Note: In a real implementation, these would be tied to audio device callbacks
    // For now, they're demonstration threads
    capture_thread_ = std::thread(&AudioProcessor::capture_loop, this);
    playback_thread_ = std::thread(&AudioProcessor::playback_loop, this);
#endif
}

void AudioProcessor::stop() {
    running_ = false;
    
#ifdef _WIN32
    if (wasapi_capture_) wasapi_capture_->stop();
    if (wasapi_playback_) wasapi_playback_->stop();
#endif

    if (capture_thread_.joinable()) {
        capture_thread_.join();
    }
    if (playback_thread_.joinable()) {
        playback_thread_.join();
    }
    
    std::cout << "[AudioProcessor] Stopped\n";
}

void AudioProcessor::set_send_callback(SendCallback cb) {
    send_cb_ = std::move(cb);
}

void AudioProcessor::on_packet(uint32_t ssrc, uint16_t seq, const uint8_t* data, size_t len,
                               bool has_positional, const float* position) {
    if (!data || len == 0) return;

    const uint32_t prev = current_remote_ssrc_.load();
    if (prev == 0) {
        current_remote_ssrc_.store(ssrc);
    } else if (prev != ssrc) {
        // New talker/source: reset jitter sequence timeline to avoid stale seq mismatch.
        jitter_.reset();
        current_remote_ssrc_.store(ssrc);
    }
    
    // Push to jitter buffer from network
    jitter_.push(seq, data, len);

    if (has_positional && position) {
        std::lock_guard<std::mutex> lock(positional_mutex_);
        last_pos_[0] = position[0];
        last_pos_[1] = position[1];
        last_pos_[2] = position[2];
        last_pos_valid_ = true;
    }
}

int AudioProcessor::get_encoded_audio(uint8_t* out_buffer) {
    if (!out_buffer) return -1;
    
    int16_t pcm[FRAME_SIZE];
    
    // Read from microphone (stub - platform-specific)
    if (!read_microphone(pcm, FRAME_SIZE)) {
        return -1;
    }
    
    // Apply echo cancellation (remove speaker audio from mic)
    if (aec_state_) {
        speex_echo_capture(aec_state_, pcm, aec_capture_out_.data());
        std::memcpy(pcm, aec_capture_out_.data(), sizeof(int16_t) * FRAME_SIZE);
    }
    
    // Encode to Opus
    int encoded = codec_.encode(pcm, out_buffer);
    
    if (encoded < 0) {
        std::cerr << "[AudioProcessor] Encode failed\n";
        return 0;
    }
    
    return encoded;
}

void AudioProcessor::get_decoded_audio(int16_t* pcm_out) {
    if (!pcm_out) return;
    
    std::vector<uint8_t> encoded_pkt;
    
    if (jitter_.pop(encoded_pkt)) {
        std::cout << "[RX-JB] Popped! len=" << encoded_pkt.size()
                  << " | buffer now=" << jitter_.get_buffer_size()
                  << " | lost so far=" << jitter_.get_lost_packets() << std::endl;
        int decoded = codec_.decode(encoded_pkt.data(), encoded_pkt.size(), pcm_out);
        if (decoded < 0) {
            std::cout << "[RX-DECODE] FAILED! error=" << decoded << " -> silence" << std::endl;
            std::fill(pcm_out, pcm_out + FRAME_SIZE, 0);
        } else {
            std::cout << "[RX-DECODE] SUCCESS - decoded " << decoded << " samples" << std::endl;
        }
    } else {
        std::cout << "[RX-JB] STARVED! No packet available (PLC/silence) | buffer="
                  << jitter_.get_buffer_size()
                  << " lost=" << jitter_.get_lost_packets() << std::endl;
        codec_.decode(nullptr, 0, pcm_out);
    }

    float rms = 0.0f;
    for (int i = 0; i < FRAME_SIZE; ++i) {
        float s = static_cast<float>(pcm_out[i]) / 32768.0f;
        rms += s * s;
    }
    rms = std::sqrt(rms / FRAME_SIZE);
    if (rms > 0.001f) {
        std::cout << "[PLAY-PCM] Non-silent frame! RMS = " << rms << std::endl;
    } else if (rms > 0.00001f) {
        std::cout << "[PLAY-PCM] Very quiet frame RMS = " << rms << std::endl;
    }
    
    // Simple distance attenuation for positional audio
    bool has_pos = false;
    std::array<float, 3> pos{};
    {
        std::lock_guard<std::mutex> lock(positional_mutex_);
        has_pos = last_pos_valid_;
        pos = last_pos_;
    }

    if (has_pos) {
        float dist = std::sqrt(pos[0] * pos[0] +
                               pos[1] * pos[1] +
                               pos[2] * pos[2]);
        float gain = 1.0f / (1.0f + dist);
        for (int i = 0; i < FRAME_SIZE; ++i) {
            pcm_out[i] = static_cast<int16_t>(pcm_out[i] * gain);
        }
    }

    // Process playback through AEC (store as echo reference)
    if (aec_state_) {
        speex_echo_playback(aec_state_, pcm_out);
    }
}

void AudioProcessor::capture_loop() {
    std::cout << "[AudioProcessor] Capture loop started\n";
    
    while (running_) {
        int16_t pcm[FRAME_SIZE];
        uint8_t encoded[OPUS_MAX_PAYLOAD];
        
        // Read microphone
        if (!read_microphone(pcm, FRAME_SIZE)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(FRAME_MS));
            continue;
        }
        
        // Echo cancellation
        if (kEnableInputProcessing && aec_state_) {
            speex_echo_capture(aec_state_, pcm, aec_capture_out_.data());
            std::memcpy(pcm, aec_capture_out_.data(), sizeof(int16_t) * FRAME_SIZE);
        }
        if (kEnableInputProcessing) {
            ns_.process(pcm, FRAME_SIZE, 1);
        }
        if (kEnableInputProcessing && preprocessor_enabled_) {
            preprocessor_.run(pcm);
        }
        
        // Encode
        int encoded_len = codec_.encode(pcm, encoded);
        
        if (encoded_len > 0) {
            if (send_cb_) {
                const uint16_t seq = seq_counter_;
                const uint32_t ts = timestamp_;
                std::cout << "[Client] Encoded " << encoded_len
                          << " bytes, sending seq=" << seq << "\n";
                std::array<float, 3> pos{};
                bool has_pos = positional_enabled_.load();
                if (has_pos) {
                    std::lock_guard<std::mutex> lock(positional_mutex_);
                    pos = positional_data_;
                }
                if (send_cb_(seq, ts, encoded, static_cast<size_t>(encoded_len), has_pos, pos.data())) {
                    ++seq_counter_;
                    timestamp_ += FRAME_SIZE;
                }
            }
        }
        
        // Maintain 20ms frame timing
        std::this_thread::sleep_for(std::chrono::milliseconds(FRAME_MS));
    }
}

void AudioProcessor::playback_loop() {
    std::cout << "[AudioProcessor] Playback loop started\n";
    
    while (running_) {
        int16_t pcm[FRAME_SIZE];
        
        // Get decoded audio (with PLC if packet lost)
        get_decoded_audio(pcm);
        
        // Write to speaker
        if (!write_speaker(pcm, FRAME_SIZE)) {
            std::cerr << "[AudioProcessor] Speaker write failed\n";
        }
        
        // Maintain 20ms frame timing
        std::this_thread::sleep_for(std::chrono::milliseconds(FRAME_MS));
    }
}

// === PLATFORM-SPECIFIC STUBS ===
// These would be implemented for Windows (WASAPI), Linux (ALSA), macOS (CoreAudio)

bool AudioProcessor::read_microphone(int16_t* pcm, int samples) {
    #ifdef _WIN32
    if (wasapi_capture_ && wasapi_capture_->is_enabled()) {
        // This is handled by the capture thread, so just return true.
        // The callback will fill the buffer.
        return true;
    }
    return false;
    #else
    // STUB: Replace with actual ALSA / CoreAudio
    std::fill(pcm, pcm + samples, 0);
    return true;
    #endif
}

bool AudioProcessor::write_speaker(const int16_t* pcm, int samples) {
    #ifdef _WIN32
    if (wasapi_playback_) {
        // The playback thread handles writing to the speaker via callback.
        // Nothing to do here, just return true.
        return true;
    }
    return false;
    #else
    // STUB: Replace with actual ALSA / CoreAudio
    (void)pcm;
    (void)samples;
    return true;
    #endif
}

int AudioProcessor::get_jitter_buffer_size() const {
    return jitter_.get_buffer_size();
}

int AudioProcessor::get_packet_loss() const {
    return jitter_.get_lost_packets();
}

void AudioProcessor::enable_positional_audio(bool enabled) {
    positional_enabled_.store(enabled);
    if (!enabled) {
        std::lock_guard<std::mutex> lock(positional_mutex_);
        last_pos_valid_ = false;
    }
}

void AudioProcessor::set_positional_data(float x, float y, float z) {
    std::lock_guard<std::mutex> lock(positional_mutex_);
    positional_data_ = { x, y, z };
}

void AudioProcessor::enable_loopback(bool enabled) {
    loopback_enabled_.store(enabled);
    if (!enabled) {
        loopback_has_data_ = false;
    }
}

void AudioProcessor::set_microphone_enabled(bool enabled) {
    microphone_enabled_.store(enabled);
#ifdef _WIN32
    if (wasapi_capture_) {
        wasapi_capture_->set_enabled(enabled);
    }
#endif
}

bool AudioProcessor::is_microphone_enabled() const {
    return microphone_enabled_.load();
}
