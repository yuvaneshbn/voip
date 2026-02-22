// 📁 network/control_client.h
// CONTROL CLIENT (UDP control plane)
#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "constants.h"
#include "shared/protocol/control_protocol.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketHandle = SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
#define closesocket close
#endif

class ControlClient {
public:
    using UserListCallback = std::function<void(const std::vector<CtrlUserInfo>& users)>;
    using TalkUpdateCallback = std::function<void(uint32_t from, const std::vector<uint32_t>& targets)>;

    ControlClient();
    ~ControlClient();

    bool initialize(const std::string& server_ip,
                    uint16_t control_port = DEFAULT_CONTROL_PORT);
    void start();
    void stop();

    bool ping_server(int timeout_ms = 500);
    bool join(uint32_t ssrc, const std::string& name);
    void leave(uint32_t ssrc);
    bool talk(uint32_t from, const std::vector<uint32_t>& targets);
    bool mute(uint32_t from, uint32_t target);
    bool unmute(uint32_t from, uint32_t target);
    bool set_channel(uint32_t ssrc, uint32_t channel_id);
    bool request_user_list();
    uint64_t get_last_pong_ms() const;
    uint64_t get_last_pong_age_ms() const;
    uint32_t get_last_rtt_ms() const;

    void set_user_list_callback(UserListCallback cb);
    void set_talk_update_callback(TalkUpdateCallback cb);

private:
    SocketHandle socket_;
    sockaddr_in server_addr_;
    std::atomic<bool> running_;
    std::thread recv_thread_;
    std::thread heartbeat_thread_;
    std::atomic<uint64_t> last_pong_ms_;
    std::atomic<uint64_t> last_ping_sent_ms_;
    std::atomic<uint32_t> last_rtt_ms_;
    std::mutex ping_mutex_;
    std::mutex cb_mutex_;
    UserListCallback user_cb_;
    TalkUpdateCallback talk_cb_;

    bool send_packet(const void* data, size_t size);
    void recv_loop();
    void heartbeat_loop();
    uint64_t now_ms() const;
};
