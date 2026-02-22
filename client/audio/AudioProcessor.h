// 📁 client/audio/AudioProcessor.h
// AUDIO PROCESSOR (Integrates codec, jitter buffer, echo cancellation)
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include "codec_opus.h"
#include "engine_jitter.h"
#include "NoiseSuppressor.h"
#include "AudioPreprocessor.h"
#include "constants.h"

#ifdef _WIN32
#include "wasapi_capture.h"
#include "wasapi_playback.h"
#endif

struct SpeexEchoState_;

class AudioProcessor {
public:
    // Constructor: Initialize with network mode (LAN/Wi-Fi)
    explicit AudioProcessor(NetworkMode mode = NetworkMode::LAN);
    ~AudioProcessor();

    // Start audio capture and playback threads
    void start();

    // Stop audio threads
    void stop();

    // === RECEIVE SIDE ===
    // Called from network layer with incoming packets
    void on_packet(uint32_t ssrc, uint16_t seq, const uint8_t* data, size_t len,
                   bool has_positional = false, const float* position = nullptr);

    // === SEND SIDE ===
    // Called by audio I/O to get encoded audio
    int get_encoded_audio(uint8_t* out_buffer);
    
    // Set callback to send encoded audio to network
    using SendCallback = std::function<bool(uint16_t seq, uint32_t timestamp,
                                            const uint8_t* data, size_t len,
                                            bool has_positional, const float* position)>;
    void set_send_callback(SendCallback cb);

    // === PLAYBACK SIDE ===
    // Called by audio I/O to get audio for speaker
    void get_decoded_audio(int16_t* pcm_out);

    // Diagnostics
    int get_jitter_buffer_size() const;
    int get_packet_loss() const;

    // Positional audio
    void enable_positional_audio(bool enabled);
    void set_positional_data(float x, float y, float z);

    // Loopback (local test)
    void enable_loopback(bool enabled);
    void set_microphone_enabled(bool enabled);
    bool is_microphone_enabled() const;

private:
    OpusCodec codec_;
    JitterBuffer jitter_;
    SpeexEchoState_* aec_state_ = nullptr;
    std::array<int16_t, FRAME_SIZE> aec_capture_out_{};
    NoiseSuppressor ns_;
    AudioPreprocessor preprocessor_;
    bool preprocessor_enabled_ = false;

    std::atomic<bool> running_;
    std::thread capture_thread_;
    std::thread playback_thread_;

    NetworkMode mode_;
    SendCallback send_cb_;
    uint16_t seq_counter_;
    uint32_t timestamp_;

    std::atomic<bool> positional_enabled_;
    std::array<float, 3> positional_data_;
    std::mutex positional_mutex_;
    std::array<float, 3> last_pos_;
    bool last_pos_valid_ = false;
    std::atomic<uint32_t> current_remote_ssrc_{0};

    std::atomic<bool> loopback_enabled_ = false;
    std::atomic<bool> microphone_enabled_{false};
    std::array<int16_t, FRAME_SIZE> loopback_buffer_{};
    std::mutex loopback_mutex_;
    bool loopback_has_data_ = false;

#ifdef _WIN32
    std::unique_ptr<WasapiCapture> wasapi_capture_;
    std::unique_ptr<WasapiPlayback> wasapi_playback_;
#endif

    // Audio I/O (stubs - platform-specific implementation needed)
    void capture_loop();
    void playback_loop();
    
    // Platform-specific audio I/O (to be implemented)
    bool read_microphone(int16_t* pcm, int samples);
    bool write_speaker(const int16_t* pcm, int samples);
};
