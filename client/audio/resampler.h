// 📁 audio/resampler.h
// Simple linear resampler for PCM16 mono streams
#pragma once

#include <cstdint>
#include <vector>

class LinearResampler {
public:
    LinearResampler();
    void set_rates(int in_rate, int out_rate);
    void reset();

    // Append input samples (mono PCM16)
    void push(const int16_t* in, int frames);

    // Produce exactly out_frames; returns false if insufficient input
    bool pop(int out_frames, std::vector<int16_t>& out);

private:
    std::vector<int16_t> in_;
    double ratio_;  // input samples per output sample
    double pos_;
};
