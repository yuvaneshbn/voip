#pragma once

#include <QObject>
#include <QHash>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonObject>
#include <QSslCertificate>
#include <QSslKey>
#include <QSslSocket>
#include <QTcpServer>
#include <QTimer>
#include <QUdpSocket>
#include <QVector>

#include <cstdint>

#include "server/hybrid/client_registry.h"

class ControlServer : public QObject {
    Q_OBJECT

public:
    explicit ControlServer(QObject *parent = nullptr);

    bool start(quint16 port);

private slots:
    void onMediaReadyRead();
    void onControlNewConnection();
    void onControlSocketReadyRead();
    void onControlSocketDisconnected();
    void onPruneTick();
    void broadcastPresence();

private:
    bool loadTlsConfiguration();
    void handleControlMessage(QSslSocket *socket, const QJsonObject &msg, qint64 nowMs);
    void sendToControlSocket(const QJsonObject &obj, QSslSocket *socket);
    uint8_t preferredLayerForReceiver(const ClientRegistry::ClientState &receiver) const;
    bool shouldForwardToReceiver(const ClientRegistry::ClientState &source, const ClientRegistry::ClientState &receiver, qint64 nowMs) const;
    QVector<uint32_t> topForwardableSourcesForReceiver(const ClientRegistry::ClientState &receiver, qint64 nowMs) const;
    QJsonObject makeServerAnnounce() const;
    void sendRaw(const QByteArray &payload, const QHostAddress &addr, quint16 port);
    void broadcastUsers();
    QJsonArray onlineUsersAsJson() const;

    QUdpSocket mediaSocket_;
    QTcpServer controlServer_;
    QHash<QSslSocket *, QByteArray> controlBuffers_;
    QTimer pruneTimer_;
    QTimer presenceTimer_;
    quint16 listenPort_ = 0;
    QSslCertificate tlsCertificate_;
    QSslKey tlsPrivateKey_;
    QByteArray mediaSessionKeyRaw_;
    QString mediaSessionKeyB64_;
    ClientRegistry registry_;
};

