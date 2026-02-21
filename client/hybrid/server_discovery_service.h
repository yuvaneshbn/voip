#pragma once

#include <QObject>
#include <QHostAddress>
#include <QTimer>
#include <QUdpSocket>
#include <QVector>

class ServerDiscoveryService : public QObject {
    Q_OBJECT

public:
    struct DiscoveredServer {
        QHostAddress address;
        quint16 port = 0;
        QString name;
        qint64 lastSeenMs = 0;
    };

    explicit ServerDiscoveryService(QObject *parent = nullptr);
    bool start(quint16 listenPort);
    void stop();
    QVector<DiscoveredServer> servers() const;

signals:
    void discoveredListChanged(const QVector<ServerDiscoveryService::DiscoveredServer> &servers);

private slots:
    void onReadyRead();
    void onPruneTick();

private:
    QUdpSocket socket_;
    QTimer pruneTimer_;
    QVector<DiscoveredServer> servers_;
    quint16 listenPort_ = 0;
};

