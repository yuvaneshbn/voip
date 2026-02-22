// 📁 audio/resampler.cpp
// Simple linear resampler for PCM16 mono streams
#include "resampler.h"
#include <algorithm>

LinearResampler::LinearResampler()
    : ratio_(1.0),
      pos_(0.0) {}

void LinearResampler::set_rates(int in_rate, int out_rate) {
    if (in_rate <= 0 || out_rate <= 0) {
        ratio_ = 1.0;
        return;
    }
    ratio_ = static_cast<double>(in_rate) / static_cast<double>(out_rate);
}

void LinearResampler::reset() {
    in_.clear();
    pos_ = 0.0;
}

void LinearResampler::push(const int16_t* in, int frames) {
    if (!in || frames <= 0) return;
    in_.insert(in_.end(), in, in + frames);
}

bool LinearResampler::pop(int out_frames, std::vector<int16_t>& out) {
    if (out_frames <= 0) return true;
    if (in_.size() < 2) return false;

    double last_pos = pos_ + ratio_ * out_frames;
    if (last_pos >= static_cast<double>(in_.size() - 1)) {
        return false;
    }

    out.resize(out_frames);
    for (int i = 0; i < out_frames; ++i) {
        int idx = static_cast<int>(pos_);
        double frac = pos_ - idx;
        int16_t s0 = in_[idx];
        int16_t s1 = in_[idx + 1];
        double sample = (1.0 - frac) * s0 + frac * s1;
        sample = std::max(-32768.0, std::min(32767.0, sample));
        out[i] = static_cast<int16_t>(sample);
        pos_ += ratio_;
    }

    int consume = static_cast<int>(pos_);
    if (consume > 0) {
        in_.erase(in_.begin(), in_.begin() + consume);
        pos_ -= consume;
    }
    return true;
}
