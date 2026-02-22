// 📁 engine/jitter_buffer.h
// ADAPTIVE JITTER BUFFER (Manages reordering and loss)
#pragma once

#include <map>
#include <mutex>
#include <vector>
#include <cstdint>
#include "constants.h"

class JitterBuffer {
public:
    // Constructor: Configure for LAN or Wi-Fi
    // initial_packets = packets to buffer before playback starts
    // max_packets = maximum allowed packets (drops old ones when exceeded)
    JitterBuffer(int initial_packets, int max_packets);

    // Push incoming packet from network
    // seq = sequence number
    // data = encoded audio payload
    // len = payload size in bytes
    void push(uint16_t seq, const uint8_t* data, size_t len);

    // Pop packet for decoding
    // Returns true if packet available, false if PLC needed
    bool pop(std::vector<uint8_t>& out);

    // Diagnostics
    int get_buffer_size() const;
    int get_late_packets() const;
    int get_lost_packets() const;
    void reset();

private:
    // Packet storage: map by sequence number (auto-sorted)
    std::map<uint16_t, std::vector<uint8_t>> buffer_;
    mutable std::mutex mtx_;

    uint16_t expected_seq_;      // Next sequence to pop
    int target_packets_;         // Initial buffering target
    int max_packets_;            // Maximum before dropping
    bool started_;               // Playback started?

    int late_pkts_;              // Packets arrived too late
    int lost_pkts_;              // Detected missing packets
};
