// 📁 network/network.h
// UDP NETWORK LAYER (RTP-like packet transport)
#pragma once

#include <cstdint>
#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <functional>
#include "shared/protocol/network_packet.h"

#ifdef _WIN32
#include <winsock2.h>
using SocketHandle = SOCKET;
#else
using SocketHandle = int;
#endif

class NetworkEngine {
public:
    using PacketCallback = std::function<void(uint32_t ssrc,
                                              uint16_t seq,
                                              const uint8_t* data,
                                              size_t len,
                                              bool has_positional,
                                              const float* position)>;

    NetworkEngine();
    ~NetworkEngine();

    // Initialize UDP socket
    bool initialize(uint16_t local_port = DEFAULT_AUDIO_PORT);

    // Connect to remote peer
    bool connect(const std::string& remote_ip, uint16_t remote_port);

    // Set callback for incoming packets
    void set_packet_callback(PacketCallback cb);

    // Send audio packet
    bool send_audio(uint16_t seq, uint32_t timestamp,
                   const uint8_t* payload, size_t len,
                   uint32_t ssrc = 0,
                   bool has_positional = false,
                   const float* position = nullptr);

    // Send a header-only UDP probe so server learns this client's audio endpoint.
    bool send_probe(uint32_t ssrc = 0);

    // Start receive thread
    void start();

    // Stop receive thread
    void stop();

    // Diagnostics
    int get_packets_sent() const;
    int get_packets_recv() const;
    int get_bytes_sent() const;
    int get_bytes_recv() const;

private:
    SocketHandle socket_;
    std::string remote_ip_;
    uint16_t remote_port_;
    uint16_t local_port_;
    
    std::atomic<bool> running_;
    std::thread recv_thread_;
    
    PacketCallback packet_cb_;
    std::mutex cb_mutex_;

    // Statistics
    std::atomic<int> pkts_sent_;
    std::atomic<int> pkts_recv_;
    std::atomic<int> bytes_sent_;
    std::atomic<int> bytes_recv_;

    // Receive loop
    void receive_loop();

    // RTP timestamp management
    uint32_t ts_clock_;
    uint16_t seq_num_;
};
