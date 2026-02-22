// 📁 network/network.cpp
// UDP NETWORK IMPLEMENTATION (Cross-platform)
#include "network.h"
#include <iostream>
#include <cstring>
#include <cstddef>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#ifdef _MSC_VER
    #pragma comment(lib, "ws2_32.lib")
#endif
    typedef int socklen_t;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <fcntl.h>
    #include <unistd.h>
    #define closesocket close
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
#endif

#include <chrono>
#include <thread>

NetworkEngine::NetworkEngine()
    : socket_(INVALID_SOCKET),
      remote_port_(0),
      local_port_(0),
      running_(false),
      pkts_sent_(0),
      pkts_recv_(0),
      bytes_sent_(0),
      bytes_recv_(0),
      ts_clock_(0),
      seq_num_(0) {}

NetworkEngine::~NetworkEngine() {
    stop();
    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
    }
}

bool NetworkEngine::initialize(uint16_t local_port) {
    local_port_ = local_port;

#ifdef _WIN32
    // Initialize Winsock
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        std::cerr << "[Network] WSAStartup failed\n";
        return false;
    }
#endif

    // Create UDP socket
    socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ == INVALID_SOCKET) {
        std::cerr << "[Network] Socket creation failed\n";
        return false;
    }

    // Socket binding policy:
    // - Windows: prevent multiple clients from sharing one UDP port.
    // - POSIX: allow quick restart with SO_REUSEADDR.
#ifdef _WIN32
    BOOL exclusive = TRUE;
    setsockopt(socket_, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
               reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
#else
    int opt = 1;
    setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
#endif

    auto try_bind = [this](uint16_t port) -> bool {
        sockaddr_in local_addr{};
        local_addr.sin_family = AF_INET;
        local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        local_addr.sin_port = htons(port);
        return bind(socket_, (sockaddr*)&local_addr, sizeof(local_addr)) != SOCKET_ERROR;
    };

    // Prefer stable local UDP ports on Windows loopback (more reliable than
    // ephemeral ports for some firewall/policy setups). If the caller requests
    // a specific port, honor it.
    bool bound = false;
    if (local_port != 0) {
        bound = try_bind(local_port);
        if (!bound) {
            std::cerr << "[Network] Bind failed on requested port " << local_port << "\n";
            closesocket(socket_);
            socket_ = INVALID_SOCKET;
            return false;
        }
    } else {
        for (uint16_t port = 5006; port <= 5015; ++port) {
            if (try_bind(port)) {
                bound = true;
                local_port_ = port;
                break;
            }
        }
        if (!bound && try_bind(0)) {
            bound = true;
            local_port_ = 0;
        }
        if (!bound) {
            std::cerr << "[Network] Bind failed on preferred ports 5006-5015 and ephemeral fallback\n";
            closesocket(socket_);
            socket_ = INVALID_SOCKET;
            return false;
        }
    }

    // Set non-blocking mode
#ifdef _WIN32
    u_long non_blocking = 1;
    ioctlsocket(socket_, FIONBIO, &non_blocking);
#else
    int flags = fcntl(socket_, F_GETFL, 0);
    fcntl(socket_, F_SETFL, flags | O_NONBLOCK);
#endif

    // Resolve actual local port (especially when binding to port 0).
    sockaddr_in bound_addr{};
    socklen_t bound_len = sizeof(bound_addr);
    if (getsockname(socket_, (sockaddr*)&bound_addr, &bound_len) == 0) {
        local_port_ = ntohs(bound_addr.sin_port);
    }

    std::cout << "[Network] Initialized on port " << local_port_ << "\n";
    return true;
}

bool NetworkEngine::connect(const std::string& remote_ip, uint16_t remote_port) {
    remote_ip_ = remote_ip;
    remote_port_ = remote_port;
    
    std::cout << "[Network] Connected to " << remote_ip << ":" << remote_port << "\n";
    return true;
}

void NetworkEngine::set_packet_callback(PacketCallback cb) {
    std::lock_guard<std::mutex> lock(cb_mutex_);
    packet_cb_ = cb;
}

bool NetworkEngine::send_audio(uint16_t seq, uint32_t timestamp,
                              const uint8_t* payload, size_t len,
                              uint32_t ssrc,
                              bool has_positional,
                              const float* position) {
    if (socket_ == INVALID_SOCKET || !payload || len == 0) {
        return false;
    }

    // Build packet
    AudioPacket pkt{};
    pkt.ssrc = ssrc;
    pkt.seq = seq;
    pkt.timestamp = timestamp;
    pkt.payload_len = static_cast<uint16_t>(len);
    pkt.flags = has_positional ? AUDIO_FLAG_POSITIONAL : 0;
    if (has_positional && position) {
        pkt.position[0] = position[0];
        pkt.position[1] = position[1];
        pkt.position[2] = position[2];
    } else {
        pkt.position[0] = 0.0f;
        pkt.position[1] = 0.0f;
        pkt.position[2] = 0.0f;
    }
    
    if (len > OPUS_MAX_PAYLOAD) {
        std::cerr << "[Network] Payload too large: " << len << "\n";
        return false;
    }

    std::memcpy(pkt.payload, payload, len);

    // Send via UDP
    sockaddr_in remote_addr{};
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_port = htons(remote_port_);
    inet_pton(AF_INET, remote_ip_.c_str(), &remote_addr.sin_addr);

    const int header_size = static_cast<int>(offsetof(AudioPacket, payload));
    int packet_size = header_size + static_cast<int>(len);
    
    int sent = sendto(socket_, (const char*)&pkt, packet_size, 0,
                     (sockaddr*)&remote_addr, sizeof(remote_addr));

    if (sent == SOCKET_ERROR) {
        std::cerr << "[Network] Send failed\n";
        return false;
    }

    pkts_sent_++;
    bytes_sent_ += sent;
    return true;
}

bool NetworkEngine::send_probe(uint32_t ssrc) {
    if (socket_ == INVALID_SOCKET) {
        return false;
    }

    AudioPacket pkt{};
    pkt.ssrc = ssrc;
    pkt.seq = seq_num_++;
    pkt.timestamp = ts_clock_;
    pkt.payload_len = 0;
    pkt.flags = 0;
    pkt.position[0] = 0.0f;
    pkt.position[1] = 0.0f;
    pkt.position[2] = 0.0f;

    sockaddr_in remote_addr{};
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_port = htons(remote_port_);
    inet_pton(AF_INET, remote_ip_.c_str(), &remote_addr.sin_addr);

    const int header_size = static_cast<int>(offsetof(AudioPacket, payload));
    int sent = sendto(socket_, (const char*)&pkt, header_size, 0,
                      (sockaddr*)&remote_addr, sizeof(remote_addr));
    if (sent == SOCKET_ERROR) {
        return false;
    }

    std::cout << "[Network] Probe sent: ssrc=" << ssrc
              << " to " << remote_ip_ << ":" << remote_port_ << "\n";

    pkts_sent_++;
    bytes_sent_ += sent;
    return true;
}

void NetworkEngine::start() {
    if (running_) return;
    
    running_ = true;
    recv_thread_ = std::thread(&NetworkEngine::receive_loop, this);
    std::cout << "[Network] Receive thread started\n";
}

void NetworkEngine::stop() {
    running_ = false;
    
    if (recv_thread_.joinable()) {
        recv_thread_.join();
    }
    
    std::cout << "[Network] Stopped\n";
}

void NetworkEngine::receive_loop() {
    AudioPacket recv_pkt;
    sockaddr_in sender_addr{};
    socklen_t sender_len = sizeof(sender_addr);
    uint64_t audio_recv_count = 0;

    while (running_) {
        int recv_len = recvfrom(socket_, (char*)&recv_pkt, sizeof(recv_pkt), 0,
                               (sockaddr*)&sender_addr, &sender_len);
        if (recv_len > 0) {
            std::cout << "[RX-RAW] recvfrom got " << recv_len << " bytes from "
                      << inet_ntoa(sender_addr.sin_addr) << ":" << ntohs(sender_addr.sin_port)
                      << std::endl;
        }
#ifdef _WIN32
        else if (recv_len < 0) {
            const int wsa_err = WSAGetLastError();
            if (wsa_err != WSAEWOULDBLOCK) {
                std::cout << "[RX-RAW] recvfrom error: " << wsa_err << std::endl;
            }
        }
#endif

        const int header_size = static_cast<int>(offsetof(AudioPacket, payload));
        if (recv_len > header_size) {  // At least header
            // Extract payload
            uint16_t payload_len = recv_pkt.payload_len;
            
            if (payload_len > 0 && payload_len <= OPUS_MAX_PAYLOAD &&
                recv_len >= header_size + payload_len) {
                pkts_recv_++;
                bytes_recv_ += recv_len;
                ++audio_recv_count;
                if ((audio_recv_count % 100) == 1) {
                    std::cout << "[Network] Audio recv: ssrc=" << recv_pkt.ssrc
                              << " seq=" << recv_pkt.seq
                              << " payload=" << payload_len
                              << " total=" << audio_recv_count << "\n";
                }

                // Callback to audio engine
                {
                    std::lock_guard<std::mutex> lock(cb_mutex_);
                    if (packet_cb_) {
                        const bool has_pos = (recv_pkt.flags & AUDIO_FLAG_POSITIONAL) != 0;
                        packet_cb_(recv_pkt.ssrc, recv_pkt.seq, recv_pkt.payload, payload_len, has_pos, recv_pkt.position);
                        std::cout << "[RX-NET] PACKET ARRIVED! SSRC=" << recv_pkt.ssrc
                                  << " seq=" << recv_pkt.seq << " payload_len=" << payload_len
                                  << " has_pos=" << (has_pos ? "yes" : "no") << std::endl;
                    }
                }
            }
        }

        // Yield CPU
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

int NetworkEngine::get_packets_sent() const {
    return pkts_sent_;
}

int NetworkEngine::get_packets_recv() const {
    return pkts_recv_;
}

int NetworkEngine::get_bytes_sent() const {
    return bytes_sent_;
}

int NetworkEngine::get_bytes_recv() const {
    return bytes_recv_;
}
