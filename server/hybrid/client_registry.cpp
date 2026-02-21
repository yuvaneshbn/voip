#include "client_registry.h"

#include <algorithm>

uint32_t ClientRegistry::assignOrReuseId(QTcpSocket *socket) {
    if (!socket) {
        return 0;
    }
    auto it = socketToId_.find(socket);
    if (it != socketToId_.end()) {
        return *it;
    }
    const uint32_t id = nextClientId_++;
    socketToId_[socket] = id;
    byId_[id].clientId = id;
    byId_[id].controlSocket = socket;
    return id;
}

void ClientRegistry::updateJoin(uint32_t clientId, const QString &name, const QString &room,
                                const QHostAddress &addr, quint16 mediaPort, QTcpSocket *socket, qint64 nowMs) {
    if (clientId == 0) {
        return;
    }
    ClientState &s = byId_[clientId];
    s.clientId = clientId;
    s.name = name;
    s.room = room.trimmed().isEmpty() ? QStringLiteral("default") : room.trimmed();
    s.online = true;
    s.mediaAddress = addr;
    s.mediaPort = mediaPort;
    s.controlSocket = socket;
    s.lastSeenMs = nowMs;
    if (socket) {
        socketToId_[socket] = clientId;
    }
}

void ClientRegistry::markOfflineBySocket(QTcpSocket *socket) {
    auto it = socketToId_.find(socket);
    if (it == socketToId_.end()) {
        return;
    }
    markOfflineById(*it);
    socketToId_.erase(it);
}

void ClientRegistry::markOfflineById(uint32_t clientId) {
    auto it = byId_.find(clientId);
    if (it == byId_.end()) {
        return;
    }
    it->online = false;
    it->controlSocket = nullptr;
}

void ClientRegistry::touch(uint32_t clientId, qint64 nowMs) {
    auto it = byId_.find(clientId);
    if (it == byId_.end()) {
        return;
    }
    it->lastSeenMs = nowMs;
}

void ClientRegistry::setTalkTargets(uint32_t clientId, const QVector<uint32_t> &targets) {
    auto it = byId_.find(clientId);
    if (it == byId_.end()) {
        return;
    }
    it->targets = targets;
}

void ClientRegistry::setSubscriptions(uint32_t clientId, const QSet<uint32_t> &sources, int maxStreams, bool filterEnabled, const QString &layer) {
    auto it = byId_.find(clientId);
    if (it == byId_.end()) {
        return;
    }
    it->subscriptions = sources;
    it->maxStreams = std::clamp(maxStreams, 1, 16);
    it->subscriptionFilterEnabled = filterEnabled;
    const QString normalized = layer.trimmed().toLower();
    if (normalized == QStringLiteral("low") || normalized == QStringLiteral("high")) {
        it->preferredLayer = normalized;
    } else {
        it->preferredLayer = QStringLiteral("auto");
    }
}

ClientRegistry::ClientState *ClientRegistry::find(uint32_t clientId) {
    auto it = byId_.find(clientId);
    return (it == byId_.end()) ? nullptr : &it.value();
}

const ClientRegistry::ClientState *ClientRegistry::find(uint32_t clientId) const {
    auto it = byId_.find(clientId);
    return (it == byId_.end()) ? nullptr : &it.value();
}

ClientRegistry::ClientState *ClientRegistry::findBySocket(QTcpSocket *socket) {
    auto sid = socketToId_.find(socket);
    if (sid == socketToId_.end()) {
        return nullptr;
    }
    return find(*sid);
}

QVector<ClientRegistry::ClientState> ClientRegistry::onlineClients() const {
    QVector<ClientState> out;
    out.reserve(byId_.size());
    for (auto it = byId_.cbegin(); it != byId_.cend(); ++it) {
        if (it->online) {
            out.push_back(*it);
        }
    }
    return out;
}

