#include "control_server.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QRandomGenerator>
#include <QSslConfiguration>
#include <QTcpSocket>

#include <algorithm>

#include "shared/protocol/control_protocol.h"
#include "shared/protocol/control_wire.h"
#include "shared/hybrid/control_messages.h"

namespace {
constexpr qint64 kStaleMs = 15000;
constexpr qint64 kActiveSpeakerWindowMs = 2500;
constexpr qint64 kServerPingIntervalMs = 5000;
constexpr qint64 kKeepaliveMissWindowMs = (kServerPingIntervalMs * 2) + 500;
}

ControlServer::ControlServer(QObject *parent)
    : QObject(parent) {
    pruneTimer_.setInterval(2000);
    QObject::connect(&pruneTimer_, &QTimer::timeout, this, &ControlServer::onPruneTick);
    presenceTimer_.setInterval(1000);
    QObject::connect(&presenceTimer_, &QTimer::timeout, this, &ControlServer::broadcastPresence);
}

bool ControlServer::start(quint16 port) {
    if (!loadTlsConfiguration()) {
        qWarning() << "TLS configuration failed. Control channel will not start.";
        return false;
    }

    mediaSessionKeyRaw_.resize(32);
    for (int i = 0; i < mediaSessionKeyRaw_.size(); ++i) {
        mediaSessionKeyRaw_[i] = static_cast<char>(QRandomGenerator::system()->bounded(0, 256));
    }
    mediaSessionKeyB64_ = QString::fromLatin1(mediaSessionKeyRaw_.toBase64());

    listenPort_ = port;
    QObject::connect(&mediaSocket_, &QUdpSocket::readyRead, this, &ControlServer::onMediaReadyRead, Qt::UniqueConnection);
    QObject::connect(&controlServer_, &QTcpServer::newConnection, this, &ControlServer::onControlNewConnection, Qt::UniqueConnection);

    const bool mediaOk = mediaSocket_.bind(QHostAddress::AnyIPv4, port, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    const bool controlOk = controlServer_.listen(QHostAddress::AnyIPv4, port);
    if (mediaOk && controlOk) {
        pruneTimer_.start();
        presenceTimer_.start();
        return true;
    }
    if (!mediaOk) {
        qWarning() << "Failed to bind UDP media socket on port" << port;
    }
    if (!controlOk) {
        qWarning() << "Failed to listen TCP control socket on port" << port << controlServer_.errorString();
    }
    return false;
}

void ControlServer::onMediaReadyRead() {
    while (mediaSocket_.hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(static_cast<int>(mediaSocket_.pendingDatagramSize()));

        QHostAddress sender;
        quint16 senderPort = 0;
        mediaSocket_.readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

        ctrlproto::VoicePacket voicePacket;
        if (ctrlproto::decode_voice_packet(datagram, voicePacket)) {
            auto *source = registry_.find(voicePacket.ssrc);
            if (!source) {
                continue;
            }

            source->online = true;
            source->mediaAddress = sender;
            source->mediaPort = senderPort;
            source->lastSeenMs = nowMs;
            source->lastAudioMs = nowMs;

            const uint8_t sourceLayer = ctrlproto::voice_layer_from_flags(voicePacket.flags);
            const QVector<ClientRegistry::ClientState> online = registry_.onlineClients();
            for (const auto &receiver : online) {
                if (!shouldForwardToReceiver(*source, receiver, nowMs)) {
                    continue;
                }
                if (sourceLayer != preferredLayerForReceiver(receiver)) {
                    continue;
                }
                sendRaw(datagram, receiver.mediaAddress, receiver.mediaPort);
            }
            continue;
        }

        QJsonObject msg;
        if (!ctrlproto::decode(datagram, msg)) {
            continue;
        }

        if (msg.value(QStringLiteral("type")).toString() == QStringLiteral("discover_request")) {
            mediaSocket_.writeDatagram(ctrlproto::encode(makeServerAnnounce()), sender, senderPort);
        }
    }
}

void ControlServer::onControlNewConnection() {
    while (controlServer_.hasPendingConnections()) {
        QTcpSocket *plainSocket = controlServer_.nextPendingConnection();
        if (!plainSocket) {
            continue;
        }

        const qintptr descriptor = plainSocket->socketDescriptor();
        plainSocket->deleteLater();

        auto *socket = new QSslSocket(this);
        if (!socket->setSocketDescriptor(descriptor)) {
            qWarning() << "Failed to attach TLS socket descriptor";
            socket->deleteLater();
            continue;
        }
        socket->setPrivateKey(tlsPrivateKey_);
        socket->setLocalCertificate(tlsCertificate_);
        socket->setProtocol(QSsl::TlsV1_2OrLater);
        socket->startServerEncryption();

        controlBuffers_.insert(socket, QByteArray{});
        QObject::connect(socket, &QSslSocket::readyRead, this, &ControlServer::onControlSocketReadyRead, Qt::UniqueConnection);
        QObject::connect(socket, &QSslSocket::disconnected, this, &ControlServer::onControlSocketDisconnected, Qt::UniqueConnection);
        QObject::connect(socket, &QSslSocket::sslErrors, this,
                         [socket](const QList<QSslError> &errors) {
                             qWarning() << "Control TLS sslErrors:" << errors;
                             socket->ignoreSslErrors();
                         },
                         Qt::UniqueConnection);
    }
}

void ControlServer::onControlSocketReadyRead() {
    auto *socket = qobject_cast<QSslSocket *>(sender());
    if (!socket) {
        return;
    }

    QByteArray &buffer = controlBuffers_[socket];
    buffer.append(socket->readAll());
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

    while (true) {
        const int newline = buffer.indexOf('\n');
        if (newline < 0) {
            break;
        }
        const QByteArray line = buffer.left(newline).trimmed();
        buffer.remove(0, newline + 1);
        if (line.isEmpty()) {
            continue;
        }
        ControlWireMessage wire;
        if (!controlwire::decode(line, wire)) {
            continue;
        }
        const QJsonObject msg = wire.json;
        handleControlMessage(socket, msg, nowMs);
    }
}

void ControlServer::onControlSocketDisconnected() {
    auto *socket = qobject_cast<QSslSocket *>(sender());
    if (!socket) {
        return;
    }
    registry_.markOfflineBySocket(socket);
    controlBuffers_.remove(socket);
    socket->deleteLater();
    broadcastUsers();
}

void ControlServer::handleControlMessage(QSslSocket *socket, const QJsonObject &msg, qint64 nowMs) {
    const QString type = msg.value(QStringLiteral("type")).toString();
    const QHostAddress senderAddr = socket->peerAddress();

    if (type == QStringLiteral("hello")) {
        const int protocolVersion = msg.value(QStringLiteral("protocol_version")).toInt(-1);
        if (protocolVersion != hybridctrl::kProtocolVersion) {
            QJsonObject error;
            error.insert(QStringLiteral("type"), QStringLiteral("error"));
            error.insert(QStringLiteral("reason"), QStringLiteral("protocol_version_mismatch"));
            error.insert(QStringLiteral("expected"), hybridctrl::kProtocolVersion);
            error.insert(QStringLiteral("received"), protocolVersion);
            sendToControlSocket(error, socket);
            socket->disconnectFromHost();
            return;
        }
        const uint32_t assignedId = registry_.assignOrReuseId(socket);
        hybridctrl::HelloAck ack;
        ack.clientId = assignedId;
        ack.mediaSessionKeyB64 = mediaSessionKeyB64_;
        sendToControlSocket(hybridctrl::to_json(ack), socket);
        return;
    }

    if (type == QStringLiteral("ping")) {
        if (auto *u = registry_.findBySocket(socket)) {
            u->lastSeenMs = nowMs;
        }
        QJsonObject reply;
        reply.insert(QStringLiteral("type"), QStringLiteral("pong"));
        reply.insert(QStringLiteral("ping_id"), msg.value(QStringLiteral("ping_id")).toDouble(0));
        sendToControlSocket(reply, socket);
        return;
    }

    if (type == QStringLiteral("pong")) {
        if (auto *u = registry_.findBySocket(socket)) {
            u->lastSeenMs = nowMs;
        }
        return;
    }

    if (type == QStringLiteral("join")) {
        const uint32_t ssrc = static_cast<uint32_t>(msg.value(QStringLiteral("ssrc")).toDouble(0));
        if (ssrc == 0) {
            return;
        }
        for (const auto &existing : registry_.onlineClients()) {
            if (existing.clientId == ssrc && existing.controlSocket.data() != socket) {
                QJsonObject error;
                error.insert(QStringLiteral("type"), QStringLiteral("error"));
                error.insert(QStringLiteral("reason"), QStringLiteral("duplicate_ssrc"));
                error.insert(QStringLiteral("ssrc"), static_cast<double>(ssrc));
                sendToControlSocket(error, socket);
                return;
            }
        }
        const QString room = msg.value(QStringLiteral("room")).toString(QStringLiteral("default"));
        registry_.updateJoin(ssrc,
                             msg.value(QStringLiteral("name")).toString(),
                             room,
                             senderAddr,
                             static_cast<quint16>(msg.value(QStringLiteral("udp_port")).toInt(0)),
                             socket,
                             nowMs);
        QJsonObject ack;
        ack.insert(QStringLiteral("type"), QStringLiteral("join_ack"));
        ack.insert(QStringLiteral("ok"), true);
        ack.insert(QStringLiteral("ssrc"), static_cast<double>(ssrc));
        ack.insert(QStringLiteral("room"), room);
        sendToControlSocket(ack, socket);
        broadcastUsers();
        return;
    }

    if (type == QStringLiteral("leave")) {
        const uint32_t ssrc = static_cast<uint32_t>(msg.value(QStringLiteral("ssrc")).toDouble(0));
        registry_.markOfflineById(ssrc);
        QJsonObject ack;
        ack.insert(QStringLiteral("type"), QStringLiteral("leave_ack"));
        ack.insert(QStringLiteral("ok"), true);
        ack.insert(QStringLiteral("ssrc"), static_cast<double>(ssrc));
        sendToControlSocket(ack, socket);
        broadcastUsers();
        return;
    }

    auto *user = registry_.findBySocket(socket);
    if (!user) {
        return;
    }
    user->lastSeenMs = nowMs;

    if (type == QStringLiteral("talk")) {
        QVector<uint32_t> targets;
        for (const QJsonValue &v : msg.value(QStringLiteral("targets")).toArray()) {
            const uint32_t t = static_cast<uint32_t>(v.toDouble(0));
            if (t != 0) {
                targets.push_back(t);
            }
        }
        registry_.setTalkTargets(user->clientId, targets);
        QJsonObject ack;
        ack.insert(QStringLiteral("type"), QStringLiteral("talk_ack"));
        ack.insert(QStringLiteral("ok"), true);
        ack.insert(QStringLiteral("ssrc"), static_cast<double>(user->clientId));
        QJsonArray targetsArr;
        for (uint32_t target : targets) {
            targetsArr.push_back(static_cast<double>(target));
        }
        ack.insert(QStringLiteral("targets"), targetsArr);
        sendToControlSocket(ack, socket);
        return;
    }

    if (type == QStringLiteral("subscribe")) {
        QSet<uint32_t> sources;
        for (const QJsonValue &v : msg.value(QStringLiteral("sources")).toArray()) {
            const uint32_t src = static_cast<uint32_t>(v.toDouble(0));
            if (src != 0 && src != user->clientId) {
                sources.insert(src);
            }
        }
        registry_.setSubscriptions(user->clientId,
                                   sources,
                                   msg.value(QStringLiteral("max_streams")).toInt(user->maxStreams),
                                   msg.value(QStringLiteral("filter_enabled")).toBool(false),
                                   msg.value(QStringLiteral("preferred_layer")).toString());
        QJsonObject ack;
        ack.insert(QStringLiteral("type"), QStringLiteral("subscribe_ack"));
        ack.insert(QStringLiteral("ok"), true);
        ack.insert(QStringLiteral("ssrc"), static_cast<double>(user->clientId));
        ack.insert(QStringLiteral("max_streams"), user->maxStreams);
        ack.insert(QStringLiteral("filter_enabled"), user->subscriptionFilterEnabled);
        ack.insert(QStringLiteral("preferred_layer"), user->preferredLayer);
        sendToControlSocket(ack, socket);
        return;
    }

    if (type == QStringLiteral("voice_feedback")) {
        const uint32_t sourceSsrc = static_cast<uint32_t>(msg.value(QStringLiteral("source_ssrc")).toDouble(0));
        auto *source = registry_.find(sourceSsrc);
        if (!source || !source->online || source->controlSocket.isNull()) {
            return;
        }

        user->rxLossEwma = (user->rxLossEwma * 0.8) + (static_cast<double>(msg.value(QStringLiteral("loss_pct")).toInt(0)) * 0.2);
        user->rxJitterEwma = (user->rxJitterEwma * 0.8) + (static_cast<double>(msg.value(QStringLiteral("jitter_ms")).toInt(0)) * 0.2);
        user->rxRttEwma = (user->rxRttEwma * 0.8) + (static_cast<double>(msg.value(QStringLiteral("rtt_ms")).toInt(0)) * 0.2);

        QJsonObject feedback;
        feedback.insert(QStringLiteral("type"), QStringLiteral("voice_feedback"));
        feedback.insert(QStringLiteral("reporter_ssrc"), static_cast<double>(user->clientId));
        feedback.insert(QStringLiteral("source_ssrc"), static_cast<double>(sourceSsrc));
        feedback.insert(QStringLiteral("loss_pct"), msg.value(QStringLiteral("loss_pct")).toInt(0));
        feedback.insert(QStringLiteral("jitter_ms"), msg.value(QStringLiteral("jitter_ms")).toInt(0));
        feedback.insert(QStringLiteral("plc_pct"), msg.value(QStringLiteral("plc_pct")).toInt(0));
        feedback.insert(QStringLiteral("fec_pct"), msg.value(QStringLiteral("fec_pct")).toInt(0));
        feedback.insert(QStringLiteral("rtt_ms"), msg.value(QStringLiteral("rtt_ms")).toInt(0));
        sendToControlSocket(feedback, source->controlSocket.data());
        return;
    }

    if (type == QStringLiteral("list")) {
        QJsonObject reply;
        reply.insert(QStringLiteral("type"), QStringLiteral("users"));
        reply.insert(QStringLiteral("users"), onlineUsersAsJson());
        sendToControlSocket(reply, socket);
    }
}

void ControlServer::onPruneTick() {
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    bool changed = false;
    for (const auto &u : registry_.onlineClients()) {
        if ((nowMs - u.lastSeenMs) > kKeepaliveMissWindowMs) {
            registry_.markOfflineById(u.clientId);
            changed = true;
            continue;
        }
        if ((nowMs - u.lastSeenMs) > kStaleMs) {
            registry_.markOfflineById(u.clientId);
            changed = true;
        }
    }

    static qint64 lastServerPingMs = 0;
    if ((nowMs - lastServerPingMs) >= kServerPingIntervalMs) {
        lastServerPingMs = nowMs;
        QJsonObject ping;
        ping.insert(QStringLiteral("type"), QStringLiteral("ping"));
        ping.insert(QStringLiteral("ping_id"), static_cast<double>(nowMs & 0x7FFFFFFF));
        for (const auto &u : registry_.onlineClients()) {
            if (!u.controlSocket.isNull()) {
                sendToControlSocket(ping, u.controlSocket.data());
            }
        }
    }

    if (changed) {
        broadcastUsers();
    }
}

void ControlServer::sendToControlSocket(const QJsonObject &obj, QSslSocket *socket) {
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }
    QByteArray payload = controlwire::encode(
        obj,
#if defined(NOX_HAS_PROTOBUF_CONTROL)
        ControlWireFormat::Protobuf
#else
        ControlWireFormat::Json
#endif
    );
    payload.append('\n');
    socket->write(payload);
}

bool ControlServer::loadTlsConfiguration() {
    const QString certEnv = qEnvironmentVariable("NOX_TLS_CERT");
    const QString keyEnv = qEnvironmentVariable("NOX_TLS_KEY");

    const QString certPath = certEnv.isEmpty()
                                 ? QCoreApplication::applicationDirPath() + QStringLiteral("/server.crt")
                                 : certEnv;
    const QString keyPath = keyEnv.isEmpty()
                                ? QCoreApplication::applicationDirPath() + QStringLiteral("/server.key")
                                : keyEnv;

    QFile certFile(certPath);
    QFile keyFile(keyPath);
    if (!certFile.open(QIODevice::ReadOnly) || !keyFile.open(QIODevice::ReadOnly)) {
        qWarning() << "TLS cert/key not found. Expected cert:" << certPath << "key:" << keyPath;
        return false;
    }

    const QByteArray certPem = certFile.readAll();
    const QByteArray keyPem = keyFile.readAll();

    tlsCertificate_ = QSslCertificate(certPem, QSsl::Pem);
    tlsPrivateKey_ = QSslKey(keyPem, QSsl::Rsa, QSsl::Pem);
    if (tlsCertificate_.isNull() || tlsPrivateKey_.isNull()) {
        qWarning() << "Failed parsing TLS cert/key.";
        return false;
    }
    return true;
}

uint8_t ControlServer::preferredLayerForReceiver(const ClientRegistry::ClientState &receiver) const {
    if (receiver.preferredLayer == QStringLiteral("low")) {
        return ctrlproto::kVoiceLayerLow;
    }
    if (receiver.preferredLayer == QStringLiteral("high")) {
        return ctrlproto::kVoiceLayerHigh;
    }
    if (receiver.rxLossEwma > 8.0 || receiver.rxJitterEwma > 35.0 || receiver.rxRttEwma > 160.0) {
        return ctrlproto::kVoiceLayerLow;
    }
    return ctrlproto::kVoiceLayerHigh;
}

bool ControlServer::shouldForwardToReceiver(const ClientRegistry::ClientState &source, const ClientRegistry::ClientState &receiver, qint64 nowMs) const {
    if (!receiver.online || receiver.clientId == source.clientId || receiver.mediaAddress.isNull() || receiver.mediaPort == 0) {
        return false;
    }
    if (source.room != receiver.room) {
        return false;
    }
    if (!source.targets.isEmpty() && !source.targets.contains(receiver.clientId)) {
        return false;
    }
    if (receiver.subscriptionFilterEnabled && !receiver.subscriptions.contains(source.clientId)) {
        return false;
    }
    if (receiver.maxStreams <= 0) {
        return true;
    }
    const QVector<uint32_t> topSources = topForwardableSourcesForReceiver(receiver, nowMs);
    return topSources.contains(source.clientId);
}

QVector<uint32_t> ControlServer::topForwardableSourcesForReceiver(const ClientRegistry::ClientState &receiver, qint64 nowMs) const {
    QVector<ClientRegistry::ClientState> candidates;
    for (const auto &src : registry_.onlineClients()) {
        if (!src.online || src.clientId == receiver.clientId || src.mediaAddress.isNull() || src.mediaPort == 0) {
            continue;
        }
        if (src.room != receiver.room) {
            continue;
        }
        if ((nowMs - src.lastAudioMs) > kActiveSpeakerWindowMs) {
            continue;
        }
        if (!src.targets.isEmpty() && !src.targets.contains(receiver.clientId)) {
            continue;
        }
        if (receiver.subscriptionFilterEnabled && !receiver.subscriptions.contains(src.clientId)) {
            continue;
        }
        candidates.push_back(src);
    }

    std::sort(candidates.begin(), candidates.end(), [](const auto &a, const auto &b) {
        if (a.lastAudioMs != b.lastAudioMs) {
            return a.lastAudioMs > b.lastAudioMs;
        }
        return a.clientId < b.clientId;
    });

    QVector<uint32_t> result;
    const int n = std::min(receiver.maxStreams, static_cast<int>(candidates.size()));
    result.reserve(n);
    for (int i = 0; i < n; ++i) {
        result.push_back(candidates[i].clientId);
    }
    return result;
}

QJsonObject ControlServer::makeServerAnnounce() const {
    QJsonObject announce;
    announce.insert(QStringLiteral("type"), QStringLiteral("server_announce"));
    announce.insert(QStringLiteral("port"), static_cast<int>(listenPort_));
    announce.insert(QStringLiteral("name"), QStringLiteral("Nox SFU"));
    announce.insert(QStringLiteral("control"), QStringLiteral("tcp"));
    announce.insert(QStringLiteral("media"), QStringLiteral("udp"));
    announce.insert(QStringLiteral("protocol"), QStringLiteral("json"));
    announce.insert(QStringLiteral("proto_version"), hybridctrl::kProtocolVersion);
    return announce;
}

void ControlServer::sendRaw(const QByteArray &payload, const QHostAddress &addr, quint16 port) {
    mediaSocket_.writeDatagram(payload, addr, port);
}

void ControlServer::broadcastUsers() {
    QJsonObject packet;
    packet.insert(QStringLiteral("type"), QStringLiteral("users"));
    packet.insert(QStringLiteral("users"), onlineUsersAsJson());
    for (const auto &u : registry_.onlineClients()) {
        if (!u.controlSocket.isNull()) {
            sendToControlSocket(packet, u.controlSocket.data());
        }
    }
}

QJsonArray ControlServer::onlineUsersAsJson() const {
    QJsonArray usersJson;
    for (const auto &u : registry_.onlineClients()) {
        QJsonObject item;
        item.insert(QStringLiteral("ssrc"), static_cast<double>(u.clientId));
        item.insert(QStringLiteral("name"), u.name);
        item.insert(QStringLiteral("online"), 1);
        item.insert(QStringLiteral("room"), u.room);
        usersJson.push_back(item);
    }
    return usersJson;
}

void ControlServer::broadcastPresence() {
    if (listenPort_ == 0) {
        return;
    }
    mediaSocket_.writeDatagram(ctrlproto::encode(makeServerAnnounce()), QHostAddress::Broadcast, listenPort_);
}
