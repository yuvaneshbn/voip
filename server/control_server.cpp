#include "control_server.h"

#include <QDateTime>
#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>

#include "shared/protocol/control_protocol.h"

namespace {
constexpr qint64 kStaleMs = 15000;
}

ControlServer::ControlServer(QObject *parent)
    : QObject(parent) {
    pruneTimer_.setInterval(2000);
    QObject::connect(&pruneTimer_, &QTimer::timeout, this, &ControlServer::onPruneTick);
}

bool ControlServer::start(quint16 port) {
    QObject::connect(&socket_, &QUdpSocket::readyRead,
                     this, &ControlServer::onReadyRead,
                     Qt::UniqueConnection);

    const bool ok = socket_.bind(QHostAddress::AnyIPv4, port, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    if (ok) {
        pruneTimer_.start();
    }
    return ok;
}

void ControlServer::onReadyRead() {
    while (socket_.hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(static_cast<int>(socket_.pendingDatagramSize()));

        QHostAddress sender;
        quint16 senderPort = 0;
        socket_.readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);

        QJsonObject msg;
        if (!ctrlproto::decode(datagram, msg)) {
            continue;
        }

        const QString type = msg.value(QStringLiteral("type")).toString();
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

        if (type == QStringLiteral("ping")) {
            for (auto it = users_.begin(); it != users_.end(); ++it) {
                if (it->online && it->addr == sender && it->port == senderPort) {
                    it->lastSeenMs = nowMs;
                }
            }

            QJsonObject reply;
            reply.insert(QStringLiteral("type"), QStringLiteral("pong"));
            reply.insert(QStringLiteral("ping_id"), msg.value(QStringLiteral("ping_id")).toDouble(0));
            sendTo(reply, sender, senderPort);
            continue;
        }

        if (type == QStringLiteral("join")) {
            const uint32_t ssrc = static_cast<uint32_t>(msg.value(QStringLiteral("ssrc")).toDouble(0));
            if (ssrc == 0) {
                continue;
            }

            UserState &u = users_[ssrc];
            u.ssrc = ssrc;
            u.name = msg.value(QStringLiteral("name")).toString();
            u.online = true;
            u.addr = sender;
            u.port = senderPort;
            u.lastSeenMs = nowMs;

            broadcastUsers();
            continue;
        }

        if (type == QStringLiteral("leave")) {
            const uint32_t ssrc = static_cast<uint32_t>(msg.value(QStringLiteral("ssrc")).toDouble(0));
            auto it = users_.find(ssrc);
            if (it != users_.end()) {
                it->online = false;
                it->lastSeenMs = nowMs;
                broadcastUsers();
            }
            continue;
        }

        if (type == QStringLiteral("talk")) {
            const uint32_t ssrc = static_cast<uint32_t>(msg.value(QStringLiteral("ssrc")).toDouble(0));
            auto it = users_.find(ssrc);
            if (it != users_.end()) {
                it->targets.clear();

                const QJsonArray targets = msg.value(QStringLiteral("targets")).toArray();
                for (const QJsonValue &v : targets) {
                    it->targets.push_back(static_cast<uint32_t>(v.toDouble(0)));
                }

                it->online = true;
                it->addr = sender;
                it->port = senderPort;
                it->lastSeenMs = nowMs;
            }
            continue;
        }

        if (type == QStringLiteral("voice")) {
            const uint32_t ssrc = static_cast<uint32_t>(msg.value(QStringLiteral("ssrc")).toDouble(0));
            auto it = users_.find(ssrc);
            if (it == users_.end()) {
                continue;
            }

            it->online = true;
            it->addr = sender;
            it->port = senderPort;
            it->lastSeenMs = nowMs;

            const QString pcmB64 = msg.value(QStringLiteral("pcm")).toString();
            if (pcmB64.isEmpty()) {
                continue;
            }

            QJsonObject forward;
            forward.insert(QStringLiteral("type"), QStringLiteral("voice"));
            forward.insert(QStringLiteral("ssrc"), static_cast<double>(ssrc));
            forward.insert(QStringLiteral("pcm"), pcmB64);

            if (it->targets.isEmpty()) {
                for (const UserState &u : users_) {
                    if (!u.online || u.ssrc == ssrc || u.addr.isNull() || u.port == 0) {
                        continue;
                    }
                    qDebug() << "Forwarding voice from" << ssrc << "to" << u.ssrc;
                    sendTo(forward, u.addr, u.port);
                }
            } else {
                for (uint32_t targetSsrc : it->targets) {
                    auto targetIt = users_.find(targetSsrc);
                    if (targetIt == users_.end()) {
                        continue;
                    }
                    const UserState &u = *targetIt;
                    if (!u.online || u.ssrc == ssrc || u.addr.isNull() || u.port == 0) {
                        continue;
                    }
                    qDebug() << "Forwarding voice from" << ssrc << "to" << u.ssrc;
                    sendTo(forward, u.addr, u.port);
                }
            }
            continue;
        }

        if (type == QStringLiteral("list")) {
            for (auto it = users_.begin(); it != users_.end(); ++it) {
                if (it->online && it->addr == sender && it->port == senderPort) {
                    it->lastSeenMs = nowMs;
                }
            }

            QJsonObject reply;
            reply.insert(QStringLiteral("type"), QStringLiteral("users"));
            reply.insert(QStringLiteral("users"), onlineUsersAsJson());
            sendTo(reply, sender, senderPort);
            continue;
        }
    }
}

void ControlServer::onPruneTick() {
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    bool changed = false;

    for (auto it = users_.begin(); it != users_.end(); ++it) {
        if (it->online && (nowMs - it->lastSeenMs) > kStaleMs) {
            it->online = false;
            changed = true;
        }
    }

    if (changed) {
        broadcastUsers();
    }
}

void ControlServer::sendTo(const QJsonObject &obj, const QHostAddress &addr, quint16 port) {
    socket_.writeDatagram(ctrlproto::encode(obj), addr, port);
}

void ControlServer::broadcastUsers() {
    QJsonObject packet;
    packet.insert(QStringLiteral("type"), QStringLiteral("users"));
    packet.insert(QStringLiteral("users"), onlineUsersAsJson());

    for (const UserState &u : users_) {
        if (u.online && !u.addr.isNull() && u.port != 0) {
            sendTo(packet, u.addr, u.port);
        }
    }
}

QJsonArray ControlServer::onlineUsersAsJson() const {
    QJsonArray usersJson;
    for (const UserState &u : users_) {
        if (!u.online) {
            continue;
        }

        QJsonObject item;
        item.insert(QStringLiteral("ssrc"), static_cast<double>(u.ssrc));
        item.insert(QStringLiteral("name"), u.name);
        item.insert(QStringLiteral("online"), 1);
        usersJson.push_back(item);
    }
    return usersJson;
}

