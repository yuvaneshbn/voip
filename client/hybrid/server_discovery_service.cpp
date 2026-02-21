#include "server_discovery_service.h"

#include <QDateTime>
#include <QJsonObject>

#include <algorithm>

#include "shared/protocol/control_protocol.h"

namespace {
constexpr qint64 kStaleMs = 6000;
}

ServerDiscoveryService::ServerDiscoveryService(QObject *parent)
    : QObject(parent) {
    pruneTimer_.setInterval(1000);
    QObject::connect(&pruneTimer_, &QTimer::timeout, this, &ServerDiscoveryService::onPruneTick);
}

bool ServerDiscoveryService::start(quint16 listenPort) {
    stop();
    listenPort_ = listenPort;
    QObject::connect(&socket_, &QUdpSocket::readyRead, this, &ServerDiscoveryService::onReadyRead, Qt::UniqueConnection);
    const bool ok = socket_.bind(QHostAddress::AnyIPv4, listenPort_, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    if (ok) {
        pruneTimer_.start();
    }
    return ok;
}

void ServerDiscoveryService::stop() {
    pruneTimer_.stop();
    socket_.close();
    servers_.clear();
}

QVector<ServerDiscoveryService::DiscoveredServer> ServerDiscoveryService::servers() const {
    return servers_;
}

void ServerDiscoveryService::onReadyRead() {
    bool changed = false;
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
        if (msg.value(QStringLiteral("type")).toString() != QStringLiteral("server_announce")) {
            continue;
        }

        const quint16 port = static_cast<quint16>(msg.value(QStringLiteral("port")).toInt(senderPort));
        if (port == 0 || sender.isNull()) {
            continue;
        }

        const QString name = msg.value(QStringLiteral("name")).toString(QStringLiteral("Nox Server"));
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

        bool found = false;
        for (DiscoveredServer &s : servers_) {
            if (s.address == sender && s.port == port) {
                s.lastSeenMs = nowMs;
                s.name = name;
                found = true;
                break;
            }
        }
        if (!found) {
            servers_.push_back(DiscoveredServer{sender, port, name, nowMs});
            changed = true;
        }
    }

    if (changed) {
        emit discoveredListChanged(servers_);
    }
}

void ServerDiscoveryService::onPruneTick() {
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const int before = servers_.size();
    servers_.erase(std::remove_if(servers_.begin(), servers_.end(),
                                  [nowMs](const DiscoveredServer &s) {
                                      return (nowMs - s.lastSeenMs) > kStaleMs;
                                  }),
                   servers_.end());
    if (servers_.size() != before) {
        emit discoveredListChanged(servers_);
    }
}
