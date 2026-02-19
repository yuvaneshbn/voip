// 📁 client/EchoCanceller.cpp
// ECHO CANCELLATION IMPLEMENTATION
#include "EchoCanceller.h"
#include <cstring>
#include <cmath>
#include <iostream>

EchoCanceller::EchoCanceller()
    : echo_write_pos_(0),
      echo_read_pos_(0),
      mu_(0.001f)  // Adaptive step size
{
    echo_ref_.resize(ECHO_BUFFER_SIZE, 0);
    filter_.resize(FILTER_LEN, 0.0f);
    std::cout << "[EchoCanceller] Initialized\n";
}

EchoCanceller::~EchoCanceller() = default;

void EchoCanceller::process_playout(int16_t* pcm) {
    if (!pcm) return;

    // Store speaker output as echo reference
    for (int i = 0; i < FRAME_SIZE; i++) {
        echo_ref_[echo_write_pos_] = pcm[i];
        echo_write_pos_ = (echo_write_pos_ + 1) % ECHO_BUFFER_SIZE;
    }
}

void EchoCanceller::process_capture(int16_t* pcm) {
    if (!pcm) return;

    int16_t filtered[FRAME_SIZE];
    
    for (int i = 0; i < FRAME_SIZE; i++) {
        float mic_sample = static_cast<float>(pcm[i]);
        float echo_est = adaptive_filter_at(echo_read_pos_, mic_sample);
        float out_sample = mic_sample - echo_est;
        
        // Clamp to int16 range
        if (out_sample > 32767.0f) out_sample = 32767.0f;
        if (out_sample < -32768.0f) out_sample = -32768.0f;
        
        filtered[i] = static_cast<int16_t>(out_sample);
        echo_read_pos_ = (echo_read_pos_ + 1) % ECHO_BUFFER_SIZE;
    }

    // Copy filtered result back
    std::memcpy(pcm, filtered, FRAME_SIZE * sizeof(int16_t));
}

float EchoCanceller::adaptive_filter_at(int ref_start, float mic_sample) {
    // Normalized LMS: estimate echo from reference, update using mic error
    float output = 0.0f;
    float input_power = 1.0f + 1e-6f;

    for (int i = 0; i < FILTER_LEN; i++) {
        int idx = (ref_start + i) % ECHO_BUFFER_SIZE;
        float ref_sample = static_cast<float>(echo_ref_[idx]);
        output += filter_[i] * ref_sample;
        input_power += ref_sample * ref_sample;
    }

    float error = mic_sample - output;
    float step = mu_ / (input_power + 1e-6f);
    for (int i = 0; i < FILTER_LEN; i++) {
        int idx = (ref_start + i) % ECHO_BUFFER_SIZE;
        float ref_sample = static_cast<float>(echo_ref_[idx]);
        filter_[i] += step * error * ref_sample;
    }

    return output;
}
