#pragma once

#include <cstdint>
#include <vector>

#include "constants.h"

class EchoCanceller {
public:
    EchoCanceller();
    ~EchoCanceller();

    void process_playout(int16_t* pcm);
    void process_capture(int16_t* pcm);

private:
    static constexpr int FILTER_LEN = 128;
    static constexpr int ECHO_BUFFER_SIZE = FRAME_SIZE * 20;

    float adaptive_filter_at(int ref_start, float mic_sample);

    std::vector<int16_t> echo_ref_;
    std::vector<float> filter_;
    int echo_write_pos_;
    int echo_read_pos_;
    float mu_;
};
