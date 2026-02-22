#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

typedef struct SpeexEchoState_ SpeexEchoState;
typedef struct SpeexPreprocessState_ SpeexPreprocessState;

namespace webrtc {
class AudioTransport;
}

class AudioPipeline {
public:
    struct Config {
        uint32_t sample_rate = 48000;
        size_t channels = 1;
        size_t frame_size = 960;
    };

    AudioPipeline();
    explicit AudioPipeline(const Config& cfg);
    ~AudioPipeline();

    void set_audio_transport(webrtc::AudioTransport* transport);
    void set_loopback_enabled(bool enabled);

    void process_playout(int16_t* pcm, size_t frames, size_t channels);
    void process_capture(int16_t* pcm, size_t frames, size_t channels);

    void reset();

private:
    Config cfg_;
    webrtc::AudioTransport* transport_ = nullptr;
    bool loopback_enabled_ = false;

    std::vector<int16_t> last_playout_;

    SpeexEchoState* speex_echo_state_ = nullptr;
    SpeexPreprocessState* speex_preprocess_ = nullptr;
};
