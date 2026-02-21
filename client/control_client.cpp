#include "control_client.h"

#include <QDateTime>
#include <QDebug>
#include <QEventLoop>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostInfo>
#include <QRandomGenerator>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <cmath>

#include "shared/protocol/control_wire.h"

ControlClient::ControlClient(QObject *parent)
    : QObject(parent) {
    playoutTimer_.setInterval(10);
    QObject::connect(&playoutTimer_, &QTimer::timeout, this, &ControlClient::onPlayoutTick);
    feedbackTimer_.setInterval(1000);
    QObject::connect(&feedbackTimer_, &QTimer::timeout, this, &ControlClient::onFeedbackTick);
    playoutTimer_.start();
    feedbackTimer_.start();

    QObject::connect(&mediaSocket_, &QUdpSocket::readyRead,
                     this, &ControlClient::onUdpReadyRead, Qt::UniqueConnection);
    QObject::connect(&controlSocket_, &QTcpSocket::readyRead,
                     this, &ControlClient::onControlReadyRead, Qt::UniqueConnection);
    QObject::connect(&controlSocket_, &QTcpSocket::connected,
                     this, &ControlClient::onControlConnected, Qt::UniqueConnection);
    QObject::connect(&controlSocket_, &QTcpSocket::disconnected,
                     this, &ControlClient::onControlDisconnected, Qt::UniqueConnection);
}

bool ControlClient::initialize(const std::string &serverIp, quint16 port) {
    const QString target = QString::fromStdString(serverIp).trimmed();
    serverPort_ = port;
    discoveryMode_ = false;
    joiningSent_ = false;
    controlReadBuffer_.clear();
    pendingControlWrites_.clear();

    if (target.isEmpty() || target.compare(QStringLiteral("auto"), Qt::CaseInsensitive) == 0) {
        serverAddress_ = QHostAddress();
        discoveryMode_ = true;
    } else {
        QHostAddress parsed;
        if (!parsed.setAddress(target)) {
            const QHostInfo info = QHostInfo::fromName(target);
            for (const QHostAddress &addr : info.addresses()) {
                if (addr.protocol() == QAbstractSocket::IPv4Protocol) {
                    parsed = addr;
                    break;
                }
            }
            if (parsed.isNull()) {
                return false;
            }
        }
        serverAddress_ = parsed;
    }

    if (mediaSocket_.state() != QAbstractSocket::BoundState) {
        if (!mediaSocket_.bind(QHostAddress::AnyIPv4, 0, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
            return false;
        }
    }
    return true;
}

QString ControlClient::server_ip() const {
    return serverAddress_.toString();
}

uint32_t ControlClient::assigned_client_id() const {
    return assignedClientId_;
}

void ControlClient::set_client_label(const QString &name) {
    const QString trimmed = name.trimmed();
    if (!trimmed.isEmpty()) {
        clientLabel_ = trimmed;
    }
}

void ControlClient::start() {
    ensureControlConnected(800);
}

void ControlClient::stop() {
    mediaSocket_.close();
    controlSocket_.disconnectFromHost();
    if (controlSocket_.state() != QAbstractSocket::UnconnectedState) {
        controlSocket_.abort();
    }
    pendingControlWrites_.clear();
    controlReadBuffer_.clear();
}

void ControlClient::update_rtt_estimate(int rttMs) {
    rttEstimateMs_ = std::clamp(rttMs, 1, 2000);
}

bool ControlClient::ensureControlConnected(int timeoutMs) {
    if (serverPort_ == 0) {
        return false;
    }

    if (controlSocket_.state() == QAbstractSocket::ConnectedState) {
        return true;
    }

    if (serverAddress_.isNull() && discoveryMode_) {
        QJsonObject discover;
        discover.insert(QStringLiteral("type"), QStringLiteral("discover_request"));
        const QByteArray payload = ctrlproto::encode(discover);
        mediaSocket_.writeDatagram(payload, QHostAddress::Broadcast, serverPort_);

        QElapsedTimer timer;
        timer.start();
        while (serverAddress_.isNull() && timer.elapsed() < timeoutMs) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
            QThread::msleep(10);
        }
    }

    if (serverAddress_.isNull()) {
        return false;
    }

    if (controlSocket_.state() == QAbstractSocket::ConnectingState) {
        return controlSocket_.waitForConnected(timeoutMs);
    }

    controlSocket_.connectToHost(serverAddress_, serverPort_);
    return controlSocket_.waitForConnected(timeoutMs);
}

bool ControlClient::ping_server(int timeoutMs) {
    if (!ensureControlConnected(timeoutMs)) {
        return false;
    }

    const quint64 pingId = nextPingId_++;
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    bool gotPong = false;
    const QMetaObject::Connection c = QObject::connect(
        this, &ControlClient::pongReceived, this,
        [&loop, &gotPong, pingId](quint64 receivedId) {
            if (receivedId == pingId) {
                gotPong = true;
                loop.quit();
            }
        },
        Qt::QueuedConnection);

    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("ping"));
    request.insert(QStringLiteral("ping_id"), static_cast<double>(pingId));
    sendPacket(request);

    timer.start(timeoutMs);
    loop.exec();
    QObject::disconnect(c);
    return gotPong;
}

void ControlClient::join(uint32_t ssrc, const std::string &name) {
    localSsrc_ = ssrc;
    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("join"));
    request.insert(QStringLiteral("ssrc"), static_cast<double>(ssrc));
    request.insert(QStringLiteral("name"), QString::fromStdString(name));
    request.insert(QStringLiteral("udp_port"), static_cast<int>(mediaSocket_.localPort()));
    sendPacket(request);
    joiningSent_ = true;
}

void ControlClient::leave(uint32_t ssrc) {
    if (localSsrc_ == ssrc) {
        localSsrc_ = 0;
        joiningSent_ = false;
    }
    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("leave"));
    request.insert(QStringLiteral("ssrc"), static_cast<double>(ssrc));
    sendPacket(request);
}

void ControlClient::talk(uint32_t ssrc, const std::vector<uint32_t> &targets) {
    QJsonArray targetArray;
    for (uint32_t t : targets) {
        targetArray.push_back(static_cast<double>(t));
    }

    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("talk"));
    request.insert(QStringLiteral("ssrc"), static_cast<double>(ssrc));
    request.insert(QStringLiteral("targets"), targetArray);
    sendPacket(request);
}

void ControlClient::set_receive_policy(uint32_t ssrc, const std::vector<uint32_t> &sources, int maxStreams, bool filterEnabled, const QString &preferredLayer) {
    QJsonArray sourceArray;
    for (uint32_t src : sources) {
        sourceArray.push_back(static_cast<double>(src));
    }

    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("subscribe"));
    request.insert(QStringLiteral("ssrc"), static_cast<double>(ssrc));
    request.insert(QStringLiteral("sources"), sourceArray);
    request.insert(QStringLiteral("max_streams"), std::clamp(maxStreams, 1, 16));
    request.insert(QStringLiteral("filter_enabled"), filterEnabled);
    request.insert(QStringLiteral("preferred_layer"), preferredLayer);
    sendPacket(request);
}

void ControlClient::send_voice(uint32_t ssrc, const QByteArray &pcm16le) {
    if (pcm16le.isEmpty() || serverAddress_.isNull() || serverPort_ == 0) {
        return;
    }

    const uint32_t ts = static_cast<uint32_t>(QDateTime::currentMSecsSinceEpoch() & 0xFFFFFFFFULL);

    QByteArray lowPayload;
    ctrlproto::VoicePacket lowPacket;
    lowPacket.ssrc = ssrc;
    lowPacket.sequence = nextVoiceSeqLow_++;
    lowPacket.timestampMs = ts;
    lowPacket.flags = ctrlproto::voice_flags_with_layer(ctrlproto::kVoiceFlagOpus, ctrlproto::kVoiceLayerLow);
    if (opusCodec_.encodeFrameLow(pcm16le, lowPayload)) {
        lowPacket.payload = lowPayload;
        mediaSocket_.writeDatagram(ctrlproto::encode_voice_packet(lowPacket), serverAddress_, serverPort_);
    }

    QByteArray highPayload;
    ctrlproto::VoicePacket highPacket;
    highPacket.ssrc = ssrc;
    highPacket.sequence = nextVoiceSeqHigh_++;
    highPacket.timestampMs = ts;
    highPacket.flags = ctrlproto::voice_flags_with_layer(ctrlproto::kVoiceFlagOpus, ctrlproto::kVoiceLayerHigh);
    if (opusCodec_.encodeFrameHigh(pcm16le, highPayload)) {
        highPacket.payload = highPayload;
        mediaSocket_.writeDatagram(ctrlproto::encode_voice_packet(highPacket), serverAddress_, serverPort_);
    }
}

void ControlClient::request_user_list() {
    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("list"));
    sendPacket(request);
}

void ControlClient::set_user_list_callback(std::function<void(const std::vector<CtrlUserInfo> &)> cb) {
    userListCallback_ = std::move(cb);
}

void ControlClient::set_voice_callback(std::function<void(uint32_t, const QByteArray &)> cb) {
    voiceCallback_ = std::move(cb);
}

void ControlClient::onUdpReadyRead() {
    while (mediaSocket_.hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(static_cast<int>(mediaSocket_.pendingDatagramSize()));

        QHostAddress sender;
        quint16 senderPort = 0;
        mediaSocket_.readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);

        ctrlproto::VoicePacket voicePacket;
        if (ctrlproto::decode_voice_packet(datagram, voicePacket)) {
            const bool acceptedSource = (!serverAddress_.isNull() && sender == serverAddress_ && senderPort == serverPort_);
            if (acceptedSource) {
                handleIncomingVoice(voicePacket);
            }
            continue;
        }

        QJsonObject msg;
        if (!ctrlproto::decode(datagram, msg)) {
            continue;
        }

        if (msg.value(QStringLiteral("type")).toString() == QStringLiteral("server_announce")
            && sender.protocol() == QAbstractSocket::IPv4Protocol) {
            serverAddress_ = sender;
            if (discoveryMode_ && controlSocket_.state() == QAbstractSocket::UnconnectedState) {
                controlSocket_.connectToHost(serverAddress_, serverPort_);
            }
        }
    }
}

void ControlClient::onControlReadyRead() {
    controlReadBuffer_.append(controlSocket_.readAll());
    while (true) {
        const int newline = controlReadBuffer_.indexOf('\n');
        if (newline < 0) {
            break;
        }

        const QByteArray line = controlReadBuffer_.left(newline).trimmed();
        controlReadBuffer_.remove(0, newline + 1);
        if (line.isEmpty()) {
            continue;
        }
        ControlWireMessage wire;
        if (!controlwire::decode(line, wire)) {
            continue;
        }
        const QJsonObject msg = wire.json;

        const QString type = msg.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("hello_ack")) {
            assignedClientId_ = static_cast<uint32_t>(msg.value(QStringLiteral("client_id")).toDouble(0));
            continue;
        }
        if (type == QStringLiteral("pong")) {
            const quint64 pingId = static_cast<quint64>(msg.value(QStringLiteral("ping_id")).toDouble(0));
            emit pongReceived(pingId);
            continue;
        }
        if (type == QStringLiteral("users") && userListCallback_) {
            userListCallback_(ctrlproto::users_from_json(msg.value(QStringLiteral("users")).toArray()));
            continue;
        }
        if (type == QStringLiteral("voice_feedback")) {
            const int lossPct = msg.value(QStringLiteral("loss_pct")).toInt(0);
            const int rttMs = msg.value(QStringLiteral("rtt_ms")).toInt(rttEstimateMs_);
            const int jitterMs = msg.value(QStringLiteral("jitter_ms")).toInt(0);
            const int plcPct = msg.value(QStringLiteral("plc_pct")).toInt(0);
            const int fecPct = msg.value(QStringLiteral("fec_pct")).toInt(0);
            applyAdaptiveBitrateFromFeedback(lossPct, rttMs, jitterMs, plcPct, fecPct);
        }
    }
}

void ControlClient::onControlConnected() {
    if (discoveryMode_) {
        discoveryMode_ = false;
    }
    QJsonObject hello;
    hello.insert(QStringLiteral("type"), QStringLiteral("hello"));
    hello.insert(QStringLiteral("name"), clientLabel_);
    hello.insert(QStringLiteral("udp_port"), static_cast<int>(mediaSocket_.localPort()));
    hello.insert(QStringLiteral("protocol_version"), 2);
    QByteArray helloPayload = controlwire::encode(
        hello,
#if defined(NOX_HAS_PROTOBUF_CONTROL)
        ControlWireFormat::Protobuf
#else
        ControlWireFormat::Json
#endif
    );
    helloPayload.append('\n');
    controlSocket_.write(helloPayload);

    flushPendingControlWrites();

    if (localSsrc_ != 0 && !joiningSent_) {
        QJsonObject request;
        request.insert(QStringLiteral("type"), QStringLiteral("join"));
        request.insert(QStringLiteral("ssrc"), static_cast<double>(localSsrc_));
        request.insert(QStringLiteral("udp_port"), static_cast<int>(mediaSocket_.localPort()));
        sendPacket(request);
        joiningSent_ = true;
    }
}

void ControlClient::onControlDisconnected() {
    joiningSent_ = false;
}

void ControlClient::handleIncomingVoice(const ctrlproto::VoicePacket &packet) {
    if (!voiceCallback_ || packet.ssrc == 0 || packet.payload.isEmpty()) {
        return;
    }

    VoiceJitterState &state = jitterBySsrc_[packet.ssrc];
    const uint8_t layer = ctrlproto::voice_layer_from_flags(packet.flags);
    if (state.initialized && state.activeLayer != layer) {
        state = VoiceJitterState{};
    }
    if (!state.initialized) {
        state.initialized = true;
        state.activeLayer = layer;
        state.expectedSeq = packet.sequence;
        state.nextExpectedTsMs = packet.timestampMs;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (state.havePrevTiming) {
        const qint64 arrivalDelta = nowMs - state.prevArrivalMs;
        const qint64 remoteDelta = static_cast<qint64>(static_cast<qint32>(packet.timestampMs - state.prevRemoteTsMs));
        const double d = std::abs(static_cast<double>(arrivalDelta - remoteDelta));
        state.jitterMs += (d - state.jitterMs) / 16.0;
    }
    state.havePrevTiming = true;
    state.prevArrivalMs = nowMs;
    state.prevRemoteTsMs = packet.timestampMs;

    if (!state.pendingFrames.contains(packet.sequence)) {
        state.pendingFrames.insert(packet.sequence, QueuedVoiceFrame{
            packet.flags, packet.timestampMs, nowMs, packet.payload});
    }
    flushJitterBuffer(packet.ssrc, state, nowMs);
}

void ControlClient::flushJitterBuffer(uint32_t ssrc, VoiceJitterState &state, qint64 nowMs) {
    auto emitDecoded = [this, ssrc](const QueuedVoiceFrame &frame) {
        QByteArray pcm;
        const bool isOpus = (frame.flags & ctrlproto::kVoiceFlagOpus) != 0;
        if (isOpus) {
            if (!opusCodec_.decodeFrame(ssrc, frame.payload, pcm)) {
                return;
            }
        } else {
            pcm = frame.payload;
        }
        if (!pcm.isEmpty()) {
            voiceCallback_(ssrc, pcm);
        }
    };

    if (!state.playoutAnchored) {
        auto first = state.pendingFrames.find(state.expectedSeq);
        if (first != state.pendingFrames.end()) {
            state.playoutAnchored = true;
            state.anchorRemoteTsMs = first->timestampMs;
            state.anchorLocalMs = nowMs + 40;
            state.nextExpectedTsMs = first->timestampMs;
        }
    }

    while (true) {
        auto it = state.pendingFrames.find(state.expectedSeq);
        if (it == state.pendingFrames.end() || !state.playoutAnchored) {
            break;
        }

        const qint64 dueMs = state.anchorLocalMs + static_cast<qint64>(
            static_cast<qint32>(it->timestampMs - state.anchorRemoteTsMs));
        if (nowMs + 2 < dueMs) {
            break;
        }

        emitDecoded(it.value());
        state.pendingFrames.erase(it);
        ++state.receivedFramesWindow;
        ++state.expectedFramesWindow;
        state.expectedSeq = static_cast<uint16_t>(state.expectedSeq + 1);
        state.nextExpectedTsMs = state.nextExpectedTsMs + 20;
    }

    if (!state.playoutAnchored) {
        return;
    }

    const qint64 missingDueMs = state.anchorLocalMs + static_cast<qint64>(
        static_cast<qint32>(state.nextExpectedTsMs - state.anchorRemoteTsMs));
    if (nowMs > (missingDueMs + 20)) {
        const uint16_t nextSeq = static_cast<uint16_t>(state.expectedSeq + 1);
        auto nextIt = state.pendingFrames.find(nextSeq);
        if (nextIt != state.pendingFrames.end()
            && (nextIt->flags & ctrlproto::kVoiceFlagOpus) != 0
            && nextIt->timestampMs >= state.nextExpectedTsMs) {
            QByteArray fec;
            if (opusCodec_.decodeFecFromNext(ssrc, nextIt->payload, fec) && !fec.isEmpty()) {
                voiceCallback_(ssrc, fec);
                ++state.fecRecoveredFramesWindow;
                ++state.expectedFramesWindow;
                state.expectedSeq = static_cast<uint16_t>(state.expectedSeq + 1);
                state.nextExpectedTsMs = state.nextExpectedTsMs + 20;
                return;
            }
        }

        QByteArray plc;
        if (opusCodec_.decodePlc(ssrc, plc) && !plc.isEmpty()) {
            voiceCallback_(ssrc, plc);
        } else {
            QByteArray comfortNoise(1920, 0);
            for (int i = 0; i < comfortNoise.size(); i += 2) {
                const int n = QRandomGenerator::global()->bounded(-10, 11);
                comfortNoise[i] = static_cast<char>(n & 0xFF);
                comfortNoise[i + 1] = static_cast<char>((n >> 8) & 0xFF);
            }
            voiceCallback_(ssrc, comfortNoise);
        }
        ++state.plcFramesWindow;
        ++state.expectedFramesWindow;
        state.expectedSeq = static_cast<uint16_t>(state.expectedSeq + 1);
        state.nextExpectedTsMs = state.nextExpectedTsMs + 20;
    }
}

void ControlClient::onPlayoutTick() {
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    for (auto it = jitterBySsrc_.begin(); it != jitterBySsrc_.end(); ++it) {
        flushJitterBuffer(it.key(), it.value(), nowMs);
    }
}

void ControlClient::onFeedbackTick() {
    if (localSsrc_ == 0 || serverPort_ == 0 || serverAddress_.isNull()) {
        return;
    }

    for (auto it = jitterBySsrc_.begin(); it != jitterBySsrc_.end(); ++it) {
        VoiceJitterState &state = it.value();
        if (state.expectedFramesWindow < 10) {
            continue;
        }

        const int missing = std::max(0, state.expectedFramesWindow - state.receivedFramesWindow);
        const int lossPct = (missing * 100) / std::max(1, state.expectedFramesWindow);
        const int plcPct = (state.plcFramesWindow * 100) / std::max(1, state.expectedFramesWindow);
        const int fecPct = (state.fecRecoveredFramesWindow * 100) / std::max(1, state.expectedFramesWindow);
        const int jitterMs = static_cast<int>(std::clamp(state.jitterMs, 0.0, 500.0));
        sendVoiceFeedback(it.key(), lossPct, jitterMs, plcPct, fecPct);

        state.expectedFramesWindow = 0;
        state.receivedFramesWindow = 0;
        state.fecRecoveredFramesWindow = 0;
        state.plcFramesWindow = 0;
    }
}

void ControlClient::sendVoiceFeedback(uint32_t sourceSsrc, int lossPct, int jitterMs, int plcPct, int fecPct) {
    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("voice_feedback"));
    request.insert(QStringLiteral("reporter_ssrc"), static_cast<double>(localSsrc_));
    request.insert(QStringLiteral("source_ssrc"), static_cast<double>(sourceSsrc));
    request.insert(QStringLiteral("loss_pct"), lossPct);
    request.insert(QStringLiteral("jitter_ms"), jitterMs);
    request.insert(QStringLiteral("plc_pct"), plcPct);
    request.insert(QStringLiteral("fec_pct"), fecPct);
    request.insert(QStringLiteral("rtt_ms"), rttEstimateMs_);
    sendPacket(request);
}

void ControlClient::applyAdaptiveBitrateFromFeedback(int lossPct, int rttMs, int jitterMs, int plcPct, int fecPct) {
    feedbackLossEwma_ = (feedbackLossEwma_ * 0.8) + (static_cast<double>(lossPct) * 0.2);
    feedbackRttEwma_ = (feedbackRttEwma_ * 0.8) + (static_cast<double>(rttMs) * 0.2);
    feedbackJitterEwma_ = (feedbackJitterEwma_ * 0.8) + (static_cast<double>(jitterMs) * 0.2);
    feedbackPlcEwma_ = (feedbackPlcEwma_ * 0.8) + (static_cast<double>(plcPct) * 0.2);
    feedbackFecEwma_ = (feedbackFecEwma_ * 0.8) + (static_cast<double>(fecPct) * 0.2);

    int target = 32000;
    if (feedbackPlcEwma_ > 12.0 || feedbackLossEwma_ > 20.0 || feedbackRttEwma_ > 250.0 || feedbackJitterEwma_ > 80.0) {
        target = 16000;
    } else if (feedbackPlcEwma_ > 6.0 || feedbackLossEwma_ > 10.0 || feedbackRttEwma_ > 180.0 || feedbackJitterEwma_ > 45.0) {
        target = 22000;
    } else if (feedbackLossEwma_ < 3.0 && feedbackPlcEwma_ < 2.0 && feedbackRttEwma_ < 80.0 && feedbackJitterEwma_ < 20.0) {
        target = 40000;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (std::abs(target - currentTargetBitrate_) < 2000) {
        return;
    }
    if (lastBitrateAdjustMs_ != 0 && (nowMs - lastBitrateAdjustMs_) < 2000) {
        return;
    }

    const double expectedLoss = std::clamp(feedbackLossEwma_ + (feedbackPlcEwma_ * 0.6) - (feedbackFecEwma_ * 0.2), 0.0, 40.0);
    if (opusCodec_.setBitrate(target, static_cast<int>(expectedLoss))) {
        currentTargetBitrate_ = target;
        lastBitrateAdjustMs_ = nowMs;
    }
}

void ControlClient::flushPendingControlWrites() {
    if (controlSocket_.state() != QAbstractSocket::ConnectedState) {
        return;
    }
    for (const QByteArray &pending : pendingControlWrites_) {
        controlSocket_.write(pending);
    }
    pendingControlWrites_.clear();
}

void ControlClient::sendPacket(const QJsonObject &obj) {
    QByteArray payload = controlwire::encode(
        obj,
#if defined(NOX_HAS_PROTOBUF_CONTROL)
        ControlWireFormat::Protobuf
#else
        ControlWireFormat::Json
#endif
    );
    payload.append('\n');

    if (controlSocket_.state() == QAbstractSocket::ConnectedState) {
        controlSocket_.write(payload);
        return;
    }

    pendingControlWrites_.push_back(payload);
    ensureControlConnected(800);
}
