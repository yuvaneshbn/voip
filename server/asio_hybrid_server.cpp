#include <array>
#include <deque>
#include <iostream>
#include <memory>
#include <string>
#include <cstring>
#include <unordered_map>

#include "shared/asio/net_compat.h"

using tcp = noxasio::ip::tcp;
using udp = noxasio::ip::udp;

struct ClientInfo {
    std::shared_ptr<tcp::socket> controlSocket;
    udp::endpoint mediaEndpoint;
    bool hasMediaEndpoint = false;
};

class AsioHybridServer {
public:
    AsioHybridServer(noxasio::io_context &io, uint16_t port)
        : io_(io),
          acceptor_(io, tcp::endpoint(tcp::v4(), port)),
          mediaSocket_(io, udp::endpoint(udp::v4(), port)) {
        acceptControl();
        receiveMedia();
        std::cout << "Asio hybrid server running on TCP+UDP port " << port << "\n";
    }

private:
    void acceptControl() {
        auto socket = std::make_shared<tcp::socket>(io_);
        acceptor_.async_accept(*socket, [this, socket](const std::error_code &ec) {
            if (!ec) {
                const uint32_t id = ++nextId_;
                clients_[id].controlSocket = socket;
                std::cout << "control client connected id=" << id << "\n";
                readControlLine(id);
            }
            acceptControl();
        });
    }

    void readControlLine(uint32_t id) {
        auto it = clients_.find(id);
        if (it == clients_.end() || !it->second.controlSocket) {
            return;
        }
        auto socket = it->second.controlSocket;
        noxasio::async_read_until(*socket, controlBuffers_[id], '\n',
                                  [this, id, socket](const std::error_code &ec, std::size_t) {
                                      if (ec) {
                                          clients_.erase(id);
                                          controlBuffers_.erase(id);
                                          return;
                                      }
                                      std::istream is(&controlBuffers_[id]);
                                      std::string line;
                                      std::getline(is, line);
                                      if (!line.empty()) {
                                          if (line == "PING") {
                                              static const std::string pong = "PONG\n";
                                              noxasio::async_write(*socket, noxasio::buffer(pong),
                                                                   [](const std::error_code &, std::size_t) {});
                                          }
                                      }
                                      readControlLine(id);
                                  });
    }

    void receiveMedia() {
        mediaSocket_.async_receive_from(noxasio::buffer(mediaBuffer_), remoteMediaSender_,
                                        [this](const std::error_code &ec, std::size_t bytes) {
                                            if (!ec && bytes > 0) {
                                                // First 4 bytes: sender id (host order for local testing).
                                                if (bytes >= 4) {
                                                    uint32_t senderId = 0;
                                                    std::memcpy(&senderId, mediaBuffer_.data(), sizeof(uint32_t));
                                                    auto it = clients_.find(senderId);
                                                    if (it != clients_.end()) {
                                                        it->second.mediaEndpoint = remoteMediaSender_;
                                                        it->second.hasMediaEndpoint = true;
                                                    }
                                                }

                                                for (auto &kv : clients_) {
                                                    if (!kv.second.hasMediaEndpoint) {
                                                        continue;
                                                    }
                                                    mediaSocket_.async_send_to(
                                                        noxasio::buffer(mediaBuffer_.data(), bytes),
                                                        kv.second.mediaEndpoint,
                                                        [](const std::error_code &, std::size_t) {});
                                                }
                                            }
                                            receiveMedia();
                                        });
    }

    noxasio::io_context &io_;
    tcp::acceptor acceptor_;
    udp::socket mediaSocket_;
    udp::endpoint remoteMediaSender_;
    std::array<char, 2048> mediaBuffer_{};
    uint32_t nextId_ = 1000;
    std::unordered_map<uint32_t, ClientInfo> clients_;
    std::unordered_map<uint32_t, noxasio::streambuf> controlBuffers_;
};

int main(int argc, char **argv) {
    uint16_t port = 45454;
    if (argc > 1) {
        port = static_cast<uint16_t>(std::stoi(argv[1]));
    }
    noxasio::io_context io;
    AsioHybridServer server(io, port);
    io.run();
    return 0;
}
