// 📁 server/sfu.cpp
// SIMPLE FORWARDING UNIT (Selective Forwarding)
// Receives audio packets and forwards them to connected peers
// NO decoding/encoding - raw UDP forwarding for efficiency
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <chrono>
#include <mutex>
#include <vector>
#include <cstring>
#include <set>
#include <cstdio>
#include <algorithm>
#include <unordered_map>

#include "constants.h"
#include "shared/protocol/control_protocol.h"
#include "shared/protocol/network_packet.h"
#include "permission/permission_manager.h"
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#ifdef _MSC_VER
    #pragma comment(lib, "ws2_32.lib")
#endif
    typedef int socklen_t;
    using SocketHandle = SOCKET;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <fcntl.h>
    #include <unistd.h>
    #define closesocket close
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
    using SocketHandle = int;
#endif

#define AUDIO_PORT 5004
#define MAX_PEERS 100
#define RECV_BUFFER_SIZE 8192

struct Peer {
    sockaddr_in addr;
    sockaddr_in control_addr;
    uint32_t ssrc;           // Unique identifier
    uint64_t last_packet_ms; // For timeout detection
    uint64_t last_control_ms; // Control heartbeat
    uint64_t join_ms;        // Join time (for strict mode)
    std::string name;
    bool has_control = false;
};

namespace {
bool has_audio_endpoint(const Peer& peer) {
    return peer.addr.sin_family == AF_INET && peer.addr.sin_port != 0;
}

std::string ascii_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        if (c >= 'A' && c <= 'Z') {
            return static_cast<char>(c - 'A' + 'a');
        }
        return static_cast<char>(c);
    });
    return s;
}
}

class SFU {
public:
    SFU();
    ~SFU();

    bool initialize(uint16_t audio_port = AUDIO_PORT);
    void start();
    void stop();

private:
    SocketHandle audio_socket_;
    SocketHandle control_socket_;
    std::map<uint32_t, Peer> peers_;
    struct RouteInfo {
        bool broadcast = true;
        std::set<uint32_t> targets;
    };
    std::map<uint32_t, RouteInfo> routes_;
    std::mutex peers_mutex_;
    bool running_;
    std::thread audio_thread_;
    std::thread control_thread_;
    PermissionManager permissions_;

    void audio_loop();
    void control_loop();
    uint64_t now_ms();
    void cleanup_inactive_peers();
    void broadcast_user_list();
    void broadcast_talk_update(uint32_t from, const std::set<uint32_t>& targets);
    void send_user_list_to(const sockaddr_in& addr);
};

SFU::SFU()
    : audio_socket_(INVALID_SOCKET),
      control_socket_(INVALID_SOCKET),
      running_(false) {}

SFU::~SFU() {
    stop();
    if (audio_socket_ != INVALID_SOCKET) {
        closesocket(audio_socket_);
    }
    if (control_socket_ != INVALID_SOCKET) {
        closesocket(control_socket_);
    }
}

uint64_t SFU::now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool SFU::initialize(uint16_t audio_port) {
#ifdef _WIN32
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        std::cerr << "[SFU] WSAStartup failed\n";
        return false;
    }
#endif

    // Create UDP socket for audio
    audio_socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (audio_socket_ == INVALID_SOCKET) {
        std::cerr << "[SFU] Socket creation failed\n";
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(audio_port);

    if (bind(audio_socket_, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "[SFU] Bind failed on port " << audio_port << "\n";
        closesocket(audio_socket_);
        audio_socket_ = INVALID_SOCKET;
        return false;
    }

    // Create UDP socket for control
    control_socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (control_socket_ == INVALID_SOCKET) {
        std::cerr << "[SFU] Control socket creation failed\n";
        return false;
    }

    sockaddr_in ctrl_addr{};
    ctrl_addr.sin_family = AF_INET;
    ctrl_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    ctrl_addr.sin_port = htons(DEFAULT_CONTROL_PORT);

    if (bind(control_socket_, (sockaddr*)&ctrl_addr, sizeof(ctrl_addr)) == SOCKET_ERROR) {
        std::cerr << "[SFU] Control bind failed on port " << DEFAULT_CONTROL_PORT << "\n";
        closesocket(control_socket_);
        control_socket_ = INVALID_SOCKET;
        return false;
    }

    // Set non-blocking mode
#ifdef _WIN32
    u_long non_blocking = 1;
    ioctlsocket(audio_socket_, FIONBIO, &non_blocking);
    ioctlsocket(control_socket_, FIONBIO, &non_blocking);
#else
    int flags = fcntl(audio_socket_, F_GETFL, 0);
    fcntl(audio_socket_, F_SETFL, flags | O_NONBLOCK);
    int cflags = fcntl(control_socket_, F_GETFL, 0);
    fcntl(control_socket_, F_SETFL, cflags | O_NONBLOCK);
#endif

    std::cout << "[SFU] Initialized on port " << audio_port << "\n";
    std::cout << "[SFU] Control port " << DEFAULT_CONTROL_PORT << "\n";
    return true;
}

void SFU::cleanup_inactive_peers() {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    
    uint64_t now = now_ms();
    const uint64_t TIMEOUT_MS = 10000;  // 10 seconds (audio)
    const uint64_t CTRL_TIMEOUT_MS = 10000;  // 10 seconds (control)

    std::vector<uint32_t> to_remove;
    
    for (auto& [ssrc, peer] : peers_) {
        bool audio_stale = (now - peer.last_packet_ms > TIMEOUT_MS);
        bool control_stale = peer.has_control && (now - peer.last_control_ms > CTRL_TIMEOUT_MS);
        bool join_stale = (!peer.has_control) && (now - peer.join_ms > CTRL_TIMEOUT_MS);
        if ((peer.has_control && control_stale) || (!peer.has_control && join_stale) ||
            (!peer.has_control && audio_stale)) {
            to_remove.push_back(ssrc);
        }
    }

    for (uint32_t ssrc : to_remove) {
        std::cout << "[SFU] Removing inactive peer SSRC=" << ssrc << "\n";
        peers_.erase(ssrc);
        routes_.erase(ssrc);
        permissions_.remove_user(ssrc);
    }
}

void SFU::broadcast_user_list() {
    std::vector<sockaddr_in> addrs;
    {
        std::lock_guard<std::mutex> lock(peers_mutex_);
        for (auto& [_, peer] : peers_) {
            if (!peer.has_control) continue;
            addrs.push_back(peer.control_addr);
        }
    }

    for (const auto& addr : addrs) {
        send_user_list_to(addr);
    }
}

void SFU::send_user_list_to(const sockaddr_in& addr) {
    std::vector<CtrlUserInfo> users;
    {
        std::lock_guard<std::mutex> lock(peers_mutex_);
        for (auto& [ssrc, peer] : peers_) {
            CtrlUserInfo info{};
            info.ssrc = ssrc;
            std::snprintf(info.name, sizeof(info.name), "%s", peer.name.c_str());
            info.online = peer.has_control ? 1 : 0;
            users.push_back(info);
        }
    }

    CtrlHeader hdr{};
    hdr.type = CtrlType::USER_LIST;
    hdr.size = static_cast<uint16_t>(sizeof(CtrlUserList) +
                                     users.size() * sizeof(CtrlUserInfo));

    std::vector<uint8_t> pkt(sizeof(CtrlHeader) + hdr.size);
    std::memcpy(pkt.data(), &hdr, sizeof(hdr));

    CtrlUserList list{};
    list.count = static_cast<uint32_t>(users.size());
    std::memcpy(pkt.data() + sizeof(hdr), &list, sizeof(list));
    if (!users.empty()) {
        std::memcpy(pkt.data() + sizeof(hdr) + sizeof(list),
                    users.data(),
                    users.size() * sizeof(CtrlUserInfo));
    }

    sendto(control_socket_, (const char*)pkt.data(), (int)pkt.size(), 0,
           (sockaddr*)&addr, sizeof(addr));
}

void SFU::broadcast_talk_update(uint32_t from, const std::set<uint32_t>& targets) {
    CtrlHeader hdr{};
    hdr.type = CtrlType::TALK;
    hdr.size = static_cast<uint16_t>(sizeof(CtrlTalk) + targets.size() * sizeof(uint32_t));

    std::vector<uint8_t> pkt(sizeof(CtrlHeader) + hdr.size);
    std::memcpy(pkt.data(), &hdr, sizeof(hdr));

    CtrlTalk talk{};
    talk.from = from;
    talk.count = static_cast<uint16_t>(targets.size());
    talk.reserved = 0;
    std::memcpy(pkt.data() + sizeof(hdr), &talk, sizeof(talk));

    if (!targets.empty()) {
        uint8_t* p = pkt.data() + sizeof(hdr) + sizeof(talk);
        for (uint32_t t : targets) {
            std::memcpy(p, &t, sizeof(t));
            p += sizeof(t);
        }
    }

    std::vector<sockaddr_in> addrs;
    {
        std::lock_guard<std::mutex> lock(peers_mutex_);
        for (const auto& [_, peer] : peers_) {
            if (!peer.has_control) continue;
            addrs.push_back(peer.control_addr);
        }
    }

    for (const auto& addr : addrs) {
        sendto(control_socket_, (const char*)pkt.data(), (int)pkt.size(), 0,
               (sockaddr*)&addr, sizeof(addr));
    }
}
void SFU::audio_loop() {
    uint8_t buffer[RECV_BUFFER_SIZE];
    sockaddr_in sender{};
    socklen_t sender_len = sizeof(sender);
    uint64_t forwarded_packets = 0;
    uint64_t dropped_no_target = 0;
    uint64_t dropped_truncated = 0;
    std::unordered_map<uint32_t, uint64_t> recv_audio_by_ssrc;

    std::cout << "[SFU] Audio forwarding loop started\n";

    while (running_) {
        // Receive packet
        int recv_len = recvfrom(audio_socket_, (char*)buffer, RECV_BUFFER_SIZE, 0,
                               (sockaddr*)&sender, &sender_len);

        if (recv_len > 0) {
            const int header_size = static_cast<int>(offsetof(AudioPacket, payload));
            if (recv_len < header_size) {
                continue;
            }

            AudioPacket hdr{};
            std::memcpy(&hdr, buffer, header_size);
            const uint32_t sender_ssrc = hdr.ssrc;
            const uint16_t payload_len = hdr.payload_len;
            if (payload_len > OPUS_MAX_PAYLOAD || recv_len < header_size + payload_len) {
                ++dropped_truncated;
                if ((dropped_truncated % 100) == 1) {
                    std::cerr << "[SFU] Drop malformed audio packet: recv_len=" << recv_len
                              << " payload_len=" << payload_len << "\n";
                }
                continue;
            }

            // Register/update sender
            {
                std::lock_guard<std::mutex> lock(peers_mutex_);
                
                auto it = peers_.find(sender_ssrc);
                if (it == peers_.end()) {
                    std::cout << "[SFU] New peer: SSRC=" << sender_ssrc 
                              << " IP=" << inet_ntoa(sender.sin_addr) 
                              << ":" << ntohs(sender.sin_port) << "\n";
                }

                auto& peer = peers_[sender_ssrc];
                peer.addr = sender;
                peer.ssrc = sender_ssrc;
                peer.last_packet_ms = now_ms();

                if (!peer.has_control) {
                    // Strict mode: cache audio endpoint, but do not forward until JOIN+PING.
                    continue;
                }
            }

            // Keepalive/probe packets are used to learn the audio endpoint.
            // They should not be forwarded as media.
            if (payload_len == 0) {
                continue;
            }

            uint64_t& recv_count = recv_audio_by_ssrc[sender_ssrc];
            ++recv_count;
            if ((recv_count % 100) == 1) {
                std::cout << "[SFU] Audio in: SSRC=" << sender_ssrc
                          << " payload=" << payload_len
                          << " recv_count=" << recv_count << "\n";
            }

            // Forward based on routing table
            {
                std::lock_guard<std::mutex> lock(peers_mutex_);

                if (peers_.size() < 2) {
                    // Need at least 2 active peers to forward
                    continue;
                }

                auto route_it = routes_.find(sender_ssrc);
                bool broadcast = true;
                std::set<uint32_t> targets;
                if (route_it != routes_.end()) {
                    broadcast = route_it->second.broadcast;
                    targets = route_it->second.targets;
                }

                int forwarded_this_packet = 0;
                if (broadcast || targets.empty()) {
                    for (auto& [other_ssrc, other_peer] : peers_) {
                        if (other_ssrc != sender_ssrc) {
                            if (!has_audio_endpoint(other_peer)) {
                                continue;
                            }
                            if (!permissions_.can_receive(other_ssrc, sender_ssrc)) {
                                continue;
                            }
                            int sent = sendto(audio_socket_, (const char*)buffer, recv_len, 0,
                                              (sockaddr*)&other_peer.addr, sender_len);
                            if (sent == SOCKET_ERROR) {
                                std::cerr << "[SFU] Forward failed\n";
                            } else {
                                ++forwarded_this_packet;
                            }
                        }
                    }
                } else {
                    for (uint32_t target : targets) {
                        auto it = peers_.find(target);
                        if (it == peers_.end()) continue;
                        if (!has_audio_endpoint(it->second)) {
                            continue;
                        }
                        if (!permissions_.can_receive(target, sender_ssrc)) {
                            continue;
                        }
                        int sent = sendto(audio_socket_, (const char*)buffer, recv_len, 0,
                                          (sockaddr*)&it->second.addr, sender_len);
                        if (sent == SOCKET_ERROR) {
                            std::cerr << "[SFU] Forward failed\n";
                        } else {
                            ++forwarded_this_packet;
                        }
                    }
                }

                if (forwarded_this_packet == 0) {
                    ++dropped_no_target;
                    if ((dropped_no_target % 100) == 1) {
                        std::cout << "[SFU] Audio drop: no eligible target for SSRC="
                                  << sender_ssrc << " peers=" << peers_.size() << "\n";
                    }
                } else {
                    ++forwarded_packets;
                    if ((forwarded_packets % 100) == 1) {
                        std::cout << "[SFU] Audio forwarded: from=" << sender_ssrc
                                  << " copies=" << forwarded_this_packet
                                  << " total_forwarded=" << forwarded_packets << "\n";
                    }
                }
            }
        }

        // Periodic cleanup
        static uint64_t last_cleanup = 0;
        uint64_t now = now_ms();
        if (now - last_cleanup > 5000) {  // Every 5 seconds
            cleanup_inactive_peers();
            last_cleanup = now;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    std::cout << "[SFU] Audio loop stopped\n";
}

void SFU::control_loop() {
    uint8_t buffer[2048];
    sockaddr_in sender{};
    socklen_t sender_len = sizeof(sender);

    std::cout << "[SFU] Control loop started\n";

    while (running_) {
        int recv_len = recvfrom(control_socket_, (char*)buffer, sizeof(buffer), 0,
                               (sockaddr*)&sender, &sender_len);
        if (recv_len < (int)sizeof(CtrlHeader)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        CtrlHeader hdr{};
        std::memcpy(&hdr, buffer, sizeof(hdr));

        if (hdr.type == CtrlType::PING) {
            bool promoted = false;
            {
                std::lock_guard<std::mutex> lock(peers_mutex_);
                // Update last control on any ping from known sender
                for (auto& [ssrc, peer] : peers_) {
                    if (peer.has_control &&
                        peer.control_addr.sin_addr.s_addr == sender.sin_addr.s_addr &&
                        peer.control_addr.sin_port == sender.sin_port) {
                        peer.last_control_ms = now_ms();
                        break;
                    }
                }

                // Strict mode: promote pending JOIN to active on first PING
                for (auto& [ssrc, peer] : peers_) {
                    if (!peer.has_control &&
                        peer.control_addr.sin_addr.s_addr == sender.sin_addr.s_addr &&
                        peer.control_addr.sin_port == sender.sin_port) {
                        peer.has_control = true;
                        peer.last_control_ms = now_ms();
                        promoted = true;
                        break;
                    }
                }
            }
            CtrlHeader pong{CtrlType::PONG, 0};
            sendto(control_socket_, (const char*)&pong, sizeof(pong), 0,
                   (sockaddr*)&sender, sender_len);
            std::cout << "[SFU] PONG to " << inet_ntoa(sender.sin_addr)
                      << ":" << ntohs(sender.sin_port)
                      << (promoted ? " (promoted new peer)" : "") << "\n";
            if (promoted) {
                std::cout << "[SFU] Broadcasting user list after promotion\n";
                broadcast_user_list();
            }
            continue;
        }

        if (hdr.type == CtrlType::JOIN && hdr.size == sizeof(CtrlJoin)) {
            CtrlJoin join{};
            std::memcpy(&join, buffer + sizeof(hdr), sizeof(join));

            bool duplicate_name = false;
            {
                std::lock_guard<std::mutex> lock(peers_mutex_);
                const std::string wanted = ascii_lower(std::string(join.name));
                for (const auto& [ssrc, peer] : peers_) {
                    if (ssrc == join.ssrc) {
                        continue;
                    }
                    if (!peer.name.empty() && ascii_lower(peer.name) == wanted) {
                        duplicate_name = true;
                        break;
                    }
                }

                if (duplicate_name) {
                    std::cout << "[SFU] JOIN rejected (duplicate name): " << join.name
                              << " (" << join.ssrc << ")\n";
                } else {
                    auto& peer = peers_[join.ssrc];
                    // Preserve previously learned audio endpoint from probe/audio traffic.
                    if (!has_audio_endpoint(peer)) {
                        std::memset(&peer.addr, 0, sizeof(peer.addr));
                    }
                    peer.control_addr = sender;
                    peer.ssrc = join.ssrc;
                    peer.name = join.name;
                    peer.has_control = true;
                    peer.last_control_ms = now_ms();
                    peer.join_ms = now_ms();
                    routes_[join.ssrc].broadcast = true;
                    routes_[join.ssrc].targets.clear();
                    permissions_.set_channel(join.ssrc, 0);
                }
            }

            if (duplicate_name) {
                send_user_list_to(sender);
                continue;
            }

            std::cout << "[SFU] JOIN " << join.name << " (" << join.ssrc << ")\n";
            std::cout << "[SFU] Broadcasting user list after JOIN\n";
            broadcast_user_list();
            continue;
        }

        if (hdr.type == CtrlType::LEAVE && hdr.size == sizeof(CtrlLeave)) {
            CtrlLeave leave{};
            std::memcpy(&leave, buffer + sizeof(hdr), sizeof(leave));

            {
                std::lock_guard<std::mutex> lock(peers_mutex_);
                peers_.erase(leave.ssrc);
                routes_.erase(leave.ssrc);
                permissions_.remove_user(leave.ssrc);
            }

            std::cout << "[SFU] LEAVE " << leave.ssrc << "\n";
            broadcast_user_list();
            continue;
        }

        if (hdr.type == CtrlType::LIST && hdr.size == 0) {
            send_user_list_to(sender);
            continue;
        }

        if (hdr.type == CtrlType::TALK && hdr.size >= sizeof(CtrlTalk)) {
            CtrlTalk talk{};
            std::memcpy(&talk, buffer + sizeof(hdr), sizeof(talk));

            std::set<uint32_t> targets;
            size_t expected = sizeof(CtrlTalk) + talk.count * sizeof(uint32_t);
            if (talk.count > 0 && hdr.size >= expected &&
                recv_len >= (int)(sizeof(hdr) + expected)) {
                const uint8_t* p = buffer + sizeof(hdr) + sizeof(talk);
                for (uint16_t i = 0; i < talk.count; ++i) {
                    uint32_t target = 0;
                    std::memcpy(&target, p, sizeof(target));
                    targets.insert(target);
                    p += sizeof(target);
                }
            }

            {
                std::lock_guard<std::mutex> lock(peers_mutex_);

                // Remove previous reverse links to this sender.
                for (auto& [ssrc, route] : routes_) {
                    if (ssrc == talk.from) {
                        continue;
                    }
                    route.targets.erase(talk.from);
                    if (route.targets.empty()) {
                        route.broadcast = true;
                    }
                }

                auto& route = routes_[talk.from];
                route.targets = std::move(targets);
                route.broadcast = route.targets.empty();
                auto it = peers_.find(talk.from);
                if (it != peers_.end()) {
                    it->second.last_control_ms = now_ms();
                }

                // Selected-talk should be duplex: add reverse links from targets back to sender.
                if (!route.targets.empty()) {
                    for (uint32_t target : route.targets) {
                        auto& reverse = routes_[target];
                        reverse.targets.insert(talk.from);
                        reverse.broadcast = false;
                    }
                }
                targets = route.targets;
            }

            std::cout << "[SFU] TALK update from " << talk.from
                      << " (targets=" << talk.count << ")\n";
            broadcast_talk_update(talk.from, targets);
            continue;
        }

        if (hdr.type == CtrlType::MUTE && hdr.size == sizeof(CtrlMute)) {
            CtrlMute mute{};
            std::memcpy(&mute, buffer + sizeof(hdr), sizeof(mute));
            permissions_.mute(mute.from, mute.target);
            continue;
        }

        if (hdr.type == CtrlType::UNMUTE && hdr.size == sizeof(CtrlMute)) {
            CtrlMute mute{};
            std::memcpy(&mute, buffer + sizeof(hdr), sizeof(mute));
            permissions_.unmute(mute.from, mute.target);
            continue;
        }

        if (hdr.type == CtrlType::SET_CHANNEL && hdr.size == sizeof(CtrlSetChannel)) {
            CtrlSetChannel chan{};
            std::memcpy(&chan, buffer + sizeof(hdr), sizeof(chan));
            permissions_.set_channel(chan.ssrc, chan.channel_id);
            continue;
        }
    }
}

void SFU::start() {
    if (running_) return;

    running_ = true;
    audio_thread_ = std::thread(&SFU::audio_loop, this);
    control_thread_ = std::thread(&SFU::control_loop, this);
}

void SFU::stop() {
    running_ = false;

    if (audio_thread_.joinable()) {
        audio_thread_.join();
    }
    if (control_thread_.joinable()) {
        control_thread_.join();
    }

    std::cout << "[SFU] Stopped\n";
}

// === MAIN SFU SERVER ===
int main() {
    std::cout << "=== VoIP SFU Server ===\n";
    std::cout << "Listening on port " << AUDIO_PORT << "\n";
    std::cout << "Control port " << DEFAULT_CONTROL_PORT << "\n";
    std::cout << "Press Ctrl+C to stop\n\n";

    SFU server;

    if (!server.initialize(AUDIO_PORT)) {
        std::cerr << "Failed to initialize SFU\n";
        return 1;
    }

    server.start();

    // Keep running until interrupted
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    server.stop();
    return 0;
}
