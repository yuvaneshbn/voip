#pragma once

#include <QHash>
#include <QHostAddress>
#include <QPointer>
#include <QSet>
#include <QTcpSocket>
#include <QString>
#include <QVector>

#include <cstdint>

class ClientRegistry {
public:
    struct ClientState {
        uint32_t clientId = 0;
        QString name;
        QString room = QStringLiteral("default");
        bool online = false;
        QHostAddress mediaAddress;
        quint16 mediaPort = 0;
        QPointer<QTcpSocket> controlSocket;
        qint64 lastSeenMs = 0;
        qint64 lastAudioMs = 0;
        QVector<uint32_t> targets;
        bool subscriptionFilterEnabled = false;
        QSet<uint32_t> subscriptions;
        int maxStreams = 4;
        QString preferredLayer = QStringLiteral("auto");
        double rxLossEwma = 0.0;
        double rxJitterEwma = 0.0;
        double rxRttEwma = 80.0;
    };

    uint32_t assignOrReuseId(QTcpSocket *socket);
    void updateJoin(uint32_t clientId, const QString &name, const QString &room,
                    const QHostAddress &addr, quint16 mediaPort, QTcpSocket *socket, qint64 nowMs);
    void markOfflineBySocket(QTcpSocket *socket);
    void markOfflineById(uint32_t clientId);
    void touch(uint32_t clientId, qint64 nowMs);
    void setTalkTargets(uint32_t clientId, const QVector<uint32_t> &targets);
    void setSubscriptions(uint32_t clientId, const QSet<uint32_t> &sources, int maxStreams, bool filterEnabled, const QString &layer);

    ClientState *find(uint32_t clientId);
    const ClientState *find(uint32_t clientId) const;
    ClientState *findBySocket(QTcpSocket *socket);
    QVector<ClientState> onlineClients() const;

private:
    uint32_t nextClientId_ = 1000;
    QHash<uint32_t, ClientState> byId_;
    QHash<QTcpSocket *, uint32_t> socketToId_;
};
