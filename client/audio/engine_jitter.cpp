// 📁 engine/jitter_buffer.cpp
// JITTER BUFFER IMPLEMENTATION
#include "engine_jitter.h"
#include <iostream>

JitterBuffer::JitterBuffer(int initial_packets, int max_packets)
    : expected_seq_(0),
      target_packets_(initial_packets),
      max_packets_(max_packets),
      started_(false),
      late_pkts_(0),
      lost_pkts_(0) {
    std::cout << "[JitterBuffer] Init: target=" << initial_packets 
              << " max=" << max_packets << "\n";
}

void JitterBuffer::push(uint16_t seq, const uint8_t* data, size_t len) {
    if (!data || len == 0) return;

    std::lock_guard<std::mutex> lock(mtx_);

    // Store packet in sorted order (map does this automatically)
    buffer_[seq] = std::vector<uint8_t>(data, data + len);

    // Startup: Wait for initial buffer before playback
    if (!started_ && buffer_.size() >= static_cast<size_t>(target_packets_)) {
        expected_seq_ = buffer_.begin()->first;  // Start from oldest packet
        started_ = true;
        std::cout << "[JitterBuffer] Playback started, buffer=" 
                  << buffer_.size() << "\n";
    }

    // Overflow protection: Drop oldest packets beyond max
    while (static_cast<int>(buffer_.size()) > max_packets_) {
        auto oldest = buffer_.begin();
        // Check if we're dropping packets we haven't played yet
        if (!started_ || oldest->first < expected_seq_) {
            late_pkts_++;
        }
        buffer_.erase(oldest);
    }
}

bool JitterBuffer::pop(std::vector<uint8_t>& out) {
    std::lock_guard<std::mutex> lock(mtx_);

    if (!started_) {
        return false;  // Still buffering, trigger PLC
    }

    auto it = buffer_.find(expected_seq_);
    
    if (it == buffer_.end()) {
        // Packet loss detected
        lost_pkts_++;
        expected_seq_++;
        return false;  // Trigger PLC in audio engine
    }

    // Packet found: extract and remove
    out = it->second;
    buffer_.erase(it);
    expected_seq_++;

    return true;
}

int JitterBuffer::get_buffer_size() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return buffer_.size();
}

int JitterBuffer::get_late_packets() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return late_pkts_;
}

int JitterBuffer::get_lost_packets() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return lost_pkts_;
}

void JitterBuffer::reset() {
    std::lock_guard<std::mutex> lock(mtx_);
    buffer_.clear();
    expected_seq_ = 0;
    started_ = false;
}
