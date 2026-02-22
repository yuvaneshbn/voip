// 📁 network/control_client.cpp
// CONTROL CLIENT IMPLEMENTATION
#include "control_client.h"
#include <cstring>
#include <iostream>
#include <chrono>
#include <cstdio>
#include <limits>
#ifndef _WIN32
#include <cerrno>
#endif

#ifdef _WIN32
#ifdef _MSC_VER
    #pragma comment(lib, "ws2_32.lib")
#endif
    typedef int socklen_t;
#else
    #include <fcntl.h>
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
#endif

ControlClient::ControlClient()
    : socket_(INVALID_SOCKET),
      running_(false),
      last_pong_ms_(0),
      last_ping_sent_ms_(0),
      last_rtt_ms_(0) {
    std::memset(&server_addr_, 0, sizeof(server_addr_));
}

ControlClient::~ControlClient() {
    stop();
    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
    }
}

bool ControlClient::initialize(const std::string& server_ip, uint16_t control_port) {
#ifdef _WIN32
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        std::cerr << "[Control] WSAStartup failed\n";
        return false;
    }
#endif

    socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ == INVALID_SOCKET) {
        std::cerr << "[Control] Socket creation failed\n";
        return false;
    }

    std::memset(&server_addr_, 0, sizeof(server_addr_));
    server_addr_.sin_family = AF_INET;
    server_addr_.sin_port = htons(control_port);
    inet_pton(AF_INET, server_ip.c_str(), &server_addr_.sin_addr);

    // Non-blocking for receive thread
#ifdef _WIN32
    u_long non_blocking = 1;
    ioctlsocket(socket_, FIONBIO, &non_blocking);
#else
    int flags = fcntl(socket_, F_GETFL, 0);
    fcntl(socket_, F_SETFL, flags | O_NONBLOCK);
#endif

    std::cout << "[Control] Initialized to " << server_ip << ":" << control_port << "\n";
    return true;
}

void ControlClient::start() {
    if (running_) return;
    running_ = true;
    recv_thread_ = std::thread(&ControlClient::recv_loop, this);
    heartbeat_thread_ = std::thread(&ControlClient::heartbeat_loop, this);
}

void ControlClient::stop() {
    running_ = false;
    if (recv_thread_.joinable()) {
        recv_thread_.join();
    }
    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }
}

bool ControlClient::ping_server(int timeout_ms) {
    if (socket_ == INVALID_SOCKET) return false;
    std::lock_guard<std::mutex> lock(ping_mutex_);

    const uint64_t before = last_pong_ms_.load();

    CtrlHeader hdr{};
    hdr.type = CtrlType::PING;
    hdr.size = 0;

    if (!send_packet(&hdr, sizeof(hdr))) {
        return false;
    }

    const uint64_t sent_at = now_ms();
    last_ping_sent_ms_.store(sent_at);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (last_pong_ms_.load() > before) {
            return true;
        }

        // Startup probes call ping_server() before recv_loop() starts.
        // In that case, poll the socket directly so PONG can still be observed.
        if (!running_.load()) {
            uint8_t buf[sizeof(CtrlHeader)];
            sockaddr_in from{};
            socklen_t from_len = sizeof(from);
            int n = recvfrom(socket_, (char*)buf, sizeof(buf), 0,
                             (sockaddr*)&from, &from_len);
            if (n >= (int)sizeof(CtrlHeader)) {
                CtrlHeader rx_hdr{};
                std::memcpy(&rx_hdr, buf, sizeof(rx_hdr));
                if (rx_hdr.type == CtrlType::PONG) {
                    const uint64_t now = now_ms();
                    last_pong_ms_.store(now);
                    if (now >= sent_at) {
                        last_rtt_ms_.store(static_cast<uint32_t>(now - sent_at));
                    }
                    return true;
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    return false;
}

bool ControlClient::join(uint32_t ssrc, const std::string& name) {
    CtrlHeader hdr{};
    hdr.type = CtrlType::JOIN;
    hdr.size = sizeof(CtrlJoin);

    CtrlJoin join{};
    join.ssrc = ssrc;
    std::snprintf(join.name, sizeof(join.name), "%s", name.c_str());

    uint8_t pkt[sizeof(CtrlHeader) + sizeof(CtrlJoin)];
    std::memcpy(pkt, &hdr, sizeof(hdr));
    std::memcpy(pkt + sizeof(hdr), &join, sizeof(join));

    return send_packet(pkt, sizeof(pkt));
}

void ControlClient::leave(uint32_t ssrc) {
    CtrlHeader hdr{};
    hdr.type = CtrlType::LEAVE;
    hdr.size = sizeof(CtrlLeave);

    CtrlLeave leave{};
    leave.ssrc = ssrc;

    uint8_t pkt[sizeof(CtrlHeader) + sizeof(CtrlLeave)];
    std::memcpy(pkt, &hdr, sizeof(hdr));
    std::memcpy(pkt + sizeof(hdr), &leave, sizeof(leave));

    send_packet(pkt, sizeof(pkt));
}

bool ControlClient::talk(uint32_t from, const std::vector<uint32_t>& targets) {
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
        std::memcpy(pkt.data() + sizeof(hdr) + sizeof(talk),
                    targets.data(),
                    targets.size() * sizeof(uint32_t));
    }

    return send_packet(pkt.data(), pkt.size());
}

bool ControlClient::mute(uint32_t from, uint32_t target) {
    CtrlHeader hdr{};
    hdr.type = CtrlType::MUTE;
    hdr.size = sizeof(CtrlMute);

    CtrlMute mute{};
    mute.from = from;
    mute.target = target;

    uint8_t pkt[sizeof(CtrlHeader) + sizeof(CtrlMute)];
    std::memcpy(pkt, &hdr, sizeof(hdr));
    std::memcpy(pkt + sizeof(hdr), &mute, sizeof(mute));

    return send_packet(pkt, sizeof(pkt));
}

bool ControlClient::unmute(uint32_t from, uint32_t target) {
    CtrlHeader hdr{};
    hdr.type = CtrlType::UNMUTE;
    hdr.size = sizeof(CtrlMute);

    CtrlMute mute{};
    mute.from = from;
    mute.target = target;

    uint8_t pkt[sizeof(CtrlHeader) + sizeof(CtrlMute)];
    std::memcpy(pkt, &hdr, sizeof(hdr));
    std::memcpy(pkt + sizeof(hdr), &mute, sizeof(mute));

    return send_packet(pkt, sizeof(pkt));
}

bool ControlClient::set_channel(uint32_t ssrc, uint32_t channel_id) {
    CtrlHeader hdr{};
    hdr.type = CtrlType::SET_CHANNEL;
    hdr.size = sizeof(CtrlSetChannel);

    CtrlSetChannel chan{};
    chan.ssrc = ssrc;
    chan.channel_id = channel_id;

    uint8_t pkt[sizeof(CtrlHeader) + sizeof(CtrlSetChannel)];
    std::memcpy(pkt, &hdr, sizeof(hdr));
    std::memcpy(pkt + sizeof(hdr), &chan, sizeof(chan));

    return send_packet(pkt, sizeof(pkt));
}

bool ControlClient::request_user_list() {
    CtrlHeader hdr{};
    hdr.type = CtrlType::LIST;
    hdr.size = 0;
    return send_packet(&hdr, sizeof(hdr));
}

void ControlClient::set_user_list_callback(UserListCallback cb) {
    std::lock_guard<std::mutex> lock(cb_mutex_);
    user_cb_ = std::move(cb);
}

void ControlClient::set_talk_update_callback(TalkUpdateCallback cb) {
    std::lock_guard<std::mutex> lock(cb_mutex_);
    talk_cb_ = std::move(cb);
}

bool ControlClient::send_packet(const void* data, size_t size) {
    int sent = sendto(socket_, (const char*)data, (int)size, 0,
                      (sockaddr*)&server_addr_, sizeof(server_addr_));
    return sent != SOCKET_ERROR;
}

void ControlClient::recv_loop() {
    uint8_t buf[2048];
    sockaddr_in from{};
    socklen_t from_len = sizeof(from);

    while (running_) {
        int n = recvfrom(socket_, (char*)buf, sizeof(buf), 0,
                         (sockaddr*)&from, &from_len);
        if (n < (int)sizeof(CtrlHeader)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        CtrlHeader hdr{};
        std::memcpy(&hdr, buf, sizeof(hdr));
        if (hdr.type == CtrlType::PONG) {
            const uint64_t now = now_ms();
            const uint64_t sent_at = last_ping_sent_ms_.load();
            if (sent_at != 0 && now >= sent_at) {
                std::cout << "[Client] Received PONG, RTT=" << (now - sent_at) << " ms\n";
            } else {
                std::cout << "[Client] Received PONG\n";
            }
            last_pong_ms_.store(now);
            if (sent_at != 0 && now >= sent_at) {
                last_rtt_ms_.store(static_cast<uint32_t>(now - sent_at));
            }
            continue;
        }

        if (hdr.type == CtrlType::USER_LIST && hdr.size >= sizeof(CtrlUserList)) {
            const uint8_t* payload = buf + sizeof(hdr);
            CtrlUserList list{};
            std::memcpy(&list, payload, sizeof(list));

            std::vector<CtrlUserInfo> users;
            users.reserve(list.count);
            const uint8_t* p = payload + sizeof(list);
            size_t expected = sizeof(CtrlUserList) + list.count * sizeof(CtrlUserInfo);
            if (hdr.size < expected || n < (int)(sizeof(CtrlHeader) + expected)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            for (uint32_t i = 0; i < list.count; ++i) {
                CtrlUserInfo ui{};
                std::memcpy(&ui, p, sizeof(ui));
                users.push_back(ui);
                p += sizeof(ui);
            }

            std::lock_guard<std::mutex> lock(cb_mutex_);
            if (user_cb_) user_cb_(users);
        }

        if (hdr.type == CtrlType::TALK && hdr.size >= sizeof(CtrlTalk)) {
            CtrlTalk talk{};
            std::memcpy(&talk, buf + sizeof(hdr), sizeof(talk));

            std::vector<uint32_t> targets;
            const size_t expected = sizeof(CtrlTalk) + talk.count * sizeof(uint32_t);
            if (talk.count > 0 && hdr.size >= expected &&
                n >= static_cast<int>(sizeof(CtrlHeader) + expected)) {
                targets.resize(talk.count);
                std::memcpy(targets.data(),
                            buf + sizeof(hdr) + sizeof(talk),
                            talk.count * sizeof(uint32_t));
            }

            std::lock_guard<std::mutex> lock(cb_mutex_);
            if (talk_cb_) {
                talk_cb_(talk.from, targets);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void ControlClient::heartbeat_loop() {
    const int interval_ms = 1000;
    const int warn_ms = 8000;
    while (running_) {
        ping_server(500);

        uint64_t last = last_pong_ms_.load();
        if (last != 0 && (now_ms() - last) > warn_ms) {
            std::cerr << "[Control] Warning: server not responding to PONG\n";
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
    }
}

uint64_t ControlClient::now_ms() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

uint64_t ControlClient::get_last_pong_ms() const {
    return last_pong_ms_.load();
}

uint64_t ControlClient::get_last_pong_age_ms() const {
    const uint64_t last = last_pong_ms_.load();
    if (last == 0) {
        return (std::numeric_limits<uint64_t>::max)();
    }
    const uint64_t now = now_ms();
    return (now >= last) ? (now - last) : 0;
}

uint32_t ControlClient::get_last_rtt_ms() const {
    return last_rtt_ms_.load();
}
