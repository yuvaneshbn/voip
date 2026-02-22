#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#include "shared/asio/net_compat.h"

using tcp = noxasio::ip::tcp;
using udp = noxasio::ip::udp;

int main(int argc, char **argv) {
    if (argc < 4) {
        std::cerr << "Usage: nox-asio-client <server-ip> <port> <client-id>\n";
        return 1;
    }

    const std::string host = argv[1];
    const uint16_t port = static_cast<uint16_t>(std::stoi(argv[2]));
    const uint32_t clientId = static_cast<uint32_t>(std::stoul(argv[3]));

    noxasio::io_context io;

    tcp::socket control(io);
    tcp::resolver resolver(io);
    noxasio::connect(control, resolver.resolve(host, std::to_string(port)));
    const std::string ping = "PING\n";
    noxasio::write(control, noxasio::buffer(ping));
    std::cout << "TCP control connected.\n";

    udp::socket media(io, udp::endpoint(udp::v4(), 0));
    udp::endpoint serverEp(noxasio::ip::make_address(host), port);

    std::array<char, 512> pkt{};
    std::memcpy(pkt.data(), &clientId, sizeof(clientId));

    std::thread sender([&]() {
        while (true) {
            media.send_to(noxasio::buffer(pkt.data(), pkt.size()), serverEp);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    });
    sender.detach();

    std::array<char, 2048> rx{};
    udp::endpoint from;
    while (true) {
        const std::size_t n = media.receive_from(noxasio::buffer(rx), from);
        if (n > 0) {
            std::cout << "UDP media packet from " << from.address().to_string() << ":" << from.port() << " bytes=" << n << "\n";
        }
    }
}

