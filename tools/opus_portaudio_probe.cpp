#include <atomic>
#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <iostream>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "opus.h"
#include "portaudio.h"

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {
constexpr int kSampleRate = 48000;
constexpr int kChannels = 1;
constexpr int kFrameMs = 20;
constexpr int kFrameSamples = (kSampleRate * kFrameMs) / 1000;
constexpr int kMaxOpusPacket = 1500;

struct Config {
    bool relayMode = false;
    std::string peerIp = "127.0.0.1";
    uint16_t peerPort = 0;
    uint16_t listenPort = 0;
    int bitrate = 32000;
};

struct AudioState {
    OpusEncoder *encoder = nullptr;
    OpusDecoder *localDecoder = nullptr;
    OpusDecoder *remoteDecoder = nullptr;
    std::atomic<bool> running{true};
    bool relayMode = false;
    int udpSocket = -1;
    sockaddr_in peerAddr{};
    std::mutex remoteMutex;
    std::deque<std::vector<opus_int16>> remoteFrames;
};

std::atomic<bool> gStop{false};

void print_usage() {
    std::cout
        << "Usage:\n"
        << "  opus-portaudio-probe loopback [--bitrate 32000]\n"
        << "  opus-portaudio-probe relay --listen <port> --peer <ip:port> [--bitrate 32000]\n";
}

bool parse_u16(const std::string &s, uint16_t &out) {
    try {
        const unsigned long v = std::stoul(s);
        if (v > std::numeric_limits<uint16_t>::max()) {
            return false;
        }
        out = static_cast<uint16_t>(v);
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_peer(const std::string &s, std::string &ip, uint16_t &port) {
    const std::size_t colon = s.rfind(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 >= s.size()) {
        return false;
    }
    ip = s.substr(0, colon);
    return parse_u16(s.substr(colon + 1), port);
}

bool parse_args(int argc, char **argv, Config &cfg) {
    if (argc < 2) {
        return false;
    }

    const std::string mode = argv[1];
    if (mode == "loopback") {
        cfg.relayMode = false;
    } else if (mode == "relay") {
        cfg.relayMode = true;
    } else {
        return false;
    }

    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--bitrate" && i + 1 < argc) {
            cfg.bitrate = std::max(6000, std::min(64000, std::stoi(argv[++i])));
            continue;
        }
        if (arg == "--listen" && i + 1 < argc) {
            if (!parse_u16(argv[++i], cfg.listenPort)) {
                return false;
            }
            continue;
        }
        if (arg == "--peer" && i + 1 < argc) {
            if (!parse_peer(argv[++i], cfg.peerIp, cfg.peerPort)) {
                return false;
            }
            continue;
        }
        return false;
    }

    if (cfg.relayMode && (cfg.listenPort == 0 || cfg.peerPort == 0)) {
        return false;
    }

    return true;
}

void signal_handler(int) {
    gStop = true;
}

void close_socket(int &fd) {
    if (fd < 0) {
        return;
    }
#if defined(_WIN32)
    closesocket(static_cast<SOCKET>(fd));
#else
    close(fd);
#endif
    fd = -1;
}

bool make_udp_socket(AudioState &state, uint16_t listenPort, const std::string &peerIp, uint16_t peerPort) {
    int fd = static_cast<int>(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (fd < 0) {
        std::cerr << "Failed to create UDP socket\n";
        return false;
    }

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(listenPort);
    local.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd, reinterpret_cast<const sockaddr *>(&local), sizeof(local)) != 0) {
        std::cerr << "Failed to bind UDP listen port " << listenPort << "\n";
        close_socket(fd);
        return false;
    }

#if defined(_WIN32)
    u_long mode = 1;
    ioctlsocket(static_cast<SOCKET>(fd), FIONBIO, &mode);
#else
    const int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif

    sockaddr_in peer{};
    peer.sin_family = AF_INET;
    peer.sin_port = htons(peerPort);
    if (inet_pton(AF_INET, peerIp.c_str(), &peer.sin_addr) != 1) {
        std::cerr << "Invalid peer IP: " << peerIp << "\n";
        close_socket(fd);
        return false;
    }

    state.udpSocket = fd;
    state.peerAddr = peer;
    return true;
}

void udp_receive_loop(AudioState *state) {
    std::vector<unsigned char> packet(kMaxOpusPacket);
    std::vector<opus_int16> pcm(kFrameSamples * kChannels);
    while (state->running.load()) {
        sockaddr_in from{};
#if defined(_WIN32)
        int fromLen = sizeof(from);
#else
        socklen_t fromLen = sizeof(from);
#endif
        const int n = recvfrom(state->udpSocket,
                               reinterpret_cast<char *>(packet.data()),
                               static_cast<int>(packet.size()),
                               0,
                               reinterpret_cast<sockaddr *>(&from),
                               &fromLen);
        if (n <= 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }

        const int dec = opus_decode(state->remoteDecoder,
                                    packet.data(),
                                    n,
                                    pcm.data(),
                                    kFrameSamples,
                                    0);
        if (dec <= 0) {
            continue;
        }

        std::vector<opus_int16> frame(static_cast<std::size_t>(dec) * kChannels);
        std::memcpy(frame.data(), pcm.data(), frame.size() * sizeof(opus_int16));
        std::lock_guard<std::mutex> lock(state->remoteMutex);
        state->remoteFrames.push_back(std::move(frame));
        if (state->remoteFrames.size() > 50) {
            state->remoteFrames.pop_front();
        }
    }
}

int audio_callback(const void *input, void *output, unsigned long frameCount,
                   const PaStreamCallbackTimeInfo *, PaStreamCallbackFlags, void *userData) {
    auto *state = static_cast<AudioState *>(userData);
    auto *out = static_cast<opus_int16 *>(output);
    const auto *in = static_cast<const opus_int16 *>(input);

    if (!state->running.load()) {
        std::memset(out, 0, frameCount * sizeof(opus_int16));
        return paComplete;
    }

    if (frameCount != static_cast<unsigned long>(kFrameSamples)) {
        std::memset(out, 0, frameCount * sizeof(opus_int16));
        return paContinue;
    }

    std::vector<opus_int16> capture(kFrameSamples, 0);
    if (in) {
        std::memcpy(capture.data(), in, kFrameSamples * sizeof(opus_int16));
    }

    unsigned char encoded[kMaxOpusPacket];
    const int encBytes = opus_encode(state->encoder, capture.data(), kFrameSamples, encoded, kMaxOpusPacket);
    if (encBytes <= 0) {
        std::memset(out, 0, kFrameSamples * sizeof(opus_int16));
        return paContinue;
    }

    std::vector<opus_int16> localPcm(kFrameSamples, 0);
    const int localDec = opus_decode(state->localDecoder, encoded, encBytes, localPcm.data(), kFrameSamples, 0);
    if (localDec <= 0) {
        std::memset(out, 0, kFrameSamples * sizeof(opus_int16));
        return paContinue;
    }

    if (state->relayMode && state->udpSocket >= 0) {
        sendto(state->udpSocket,
               reinterpret_cast<const char *>(encoded),
               encBytes,
               0,
               reinterpret_cast<const sockaddr *>(&state->peerAddr),
               sizeof(state->peerAddr));
    }

    std::vector<opus_int16> remote(kFrameSamples, 0);
    if (state->relayMode) {
        std::lock_guard<std::mutex> lock(state->remoteMutex);
        if (!state->remoteFrames.empty()) {
            remote = std::move(state->remoteFrames.front());
            state->remoteFrames.pop_front();
            if (remote.size() < static_cast<std::size_t>(kFrameSamples)) {
                remote.resize(kFrameSamples, 0);
            }
        }
    }

    for (int i = 0; i < kFrameSamples; ++i) {
        int mixed = localPcm[i];
        if (!remote.empty()) {
            mixed += remote[i] / 2;
        }
        mixed = std::max(-32768, std::min(32767, mixed));
        out[i] = static_cast<opus_int16>(mixed);
    }
    return paContinue;
}

} // namespace

int main(int argc, char **argv) {
    Config cfg;
    if (!parse_args(argc, argv, cfg)) {
        print_usage();
        return 1;
    }

#if defined(_WIN32)
    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }
#endif

    std::signal(SIGINT, signal_handler);

    int err = 0;
    OpusEncoder *encoder = opus_encoder_create(kSampleRate, kChannels, OPUS_APPLICATION_VOIP, &err);
    if (!encoder || err != OPUS_OK) {
        std::cerr << "opus_encoder_create failed: " << err << "\n";
        return 1;
    }
    OpusDecoder *localDecoder = opus_decoder_create(kSampleRate, kChannels, &err);
    if (!localDecoder || err != OPUS_OK) {
        std::cerr << "opus_decoder_create(local) failed: " << err << "\n";
        opus_encoder_destroy(encoder);
        return 1;
    }
    OpusDecoder *remoteDecoder = opus_decoder_create(kSampleRate, kChannels, &err);
    if (!remoteDecoder || err != OPUS_OK) {
        std::cerr << "opus_decoder_create(remote) failed: " << err << "\n";
        opus_decoder_destroy(localDecoder);
        opus_encoder_destroy(encoder);
        return 1;
    }

    opus_encoder_ctl(encoder, OPUS_SET_BITRATE(cfg.bitrate));
    opus_encoder_ctl(encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    opus_encoder_ctl(encoder, OPUS_SET_INBAND_FEC(1));
    opus_encoder_ctl(encoder, OPUS_SET_PACKET_LOSS_PERC(10));

    if (Pa_Initialize() != paNoError) {
        std::cerr << "Pa_Initialize failed\n";
        opus_decoder_destroy(remoteDecoder);
        opus_decoder_destroy(localDecoder);
        opus_encoder_destroy(encoder);
        return 1;
    }

    AudioState state;
    state.encoder = encoder;
    state.localDecoder = localDecoder;
    state.remoteDecoder = remoteDecoder;
    state.relayMode = cfg.relayMode;

    if (cfg.relayMode) {
        if (!make_udp_socket(state, cfg.listenPort, cfg.peerIp, cfg.peerPort)) {
            Pa_Terminate();
            opus_decoder_destroy(remoteDecoder);
            opus_decoder_destroy(localDecoder);
            opus_encoder_destroy(encoder);
#if defined(_WIN32)
            WSACleanup();
#endif
            return 1;
        }
        std::cout << "Relay mode: listen " << cfg.listenPort
                  << " -> peer " << cfg.peerIp << ":" << cfg.peerPort << "\n";
    } else {
        std::cout << "Loopback mode\n";
    }
    std::cout << "Press Ctrl+C to stop.\n";

    PaStream *stream = nullptr;
    const PaError paErr = Pa_OpenDefaultStream(&stream,
                                               1,
                                               1,
                                               paInt16,
                                               kSampleRate,
                                               kFrameSamples,
                                               audio_callback,
                                               &state);
    if (paErr != paNoError || !stream) {
        std::cerr << "Pa_OpenDefaultStream failed: " << Pa_GetErrorText(paErr) << "\n";
        close_socket(state.udpSocket);
        Pa_Terminate();
        opus_decoder_destroy(remoteDecoder);
        opus_decoder_destroy(localDecoder);
        opus_encoder_destroy(encoder);
#if defined(_WIN32)
        WSACleanup();
#endif
        return 1;
    }

    std::thread recvThread;
    if (cfg.relayMode) {
        recvThread = std::thread(udp_receive_loop, &state);
    }

    Pa_StartStream(stream);
    while (!gStop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    state.running = false;
    Pa_StopStream(stream);
    Pa_CloseStream(stream);

    if (recvThread.joinable()) {
        recvThread.join();
    }

    close_socket(state.udpSocket);
    Pa_Terminate();
    opus_decoder_destroy(remoteDecoder);
    opus_decoder_destroy(localDecoder);
    opus_encoder_destroy(encoder);

#if defined(_WIN32)
    WSACleanup();
#endif
    return 0;
}
