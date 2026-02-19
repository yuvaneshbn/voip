#pragma once

#include <QObject>
#include <QHash>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>
#include <QUdpSocket>
#include <QVector>

#include <cstdint>

class ControlServer : public QObject {
    Q_OBJECT

public:
    explicit ControlServer(QObject *parent = nullptr);

    bool start(quint16 port);

private slots:
    void onReadyRead();
    void onPruneTick();

private:
    struct UserState {
        uint32_t ssrc = 0;
        QString name;
        bool online = false;
        QHostAddress addr;
        quint16 port = 0;
        qint64 lastSeenMs = 0;
        QVector<uint32_t> targets;
    };

    void sendTo(const QJsonObject &obj, const QHostAddress &addr, quint16 port);
    void broadcastUsers();
    QJsonArray onlineUsersAsJson() const;

    QUdpSocket socket_;
    QTimer pruneTimer_;
    QHash<uint32_t, UserState> users_;
};

