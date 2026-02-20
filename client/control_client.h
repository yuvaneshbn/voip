#pragma once

#include <QUdpSocket>


#include "shared/protocol/control_protocol.h"

class ControlClient : public QObject {
    Q_OBJECT

public:
    explicit ControlClient(QObject *parent = nullptr);

    bool initialize(const std::string &serverIp, quint16 port);
    void start();
    void stop();

    bool ping_server(int timeoutMs);

    void join(uint32_t ssrc, const std::string &name);
    void leave(uint32_t ssrc);
    void talk(uint32_t ssrc, const std::vector<uint32_t> &targets);
    void send_voice(uint32_t ssrc, const QByteArray &pcm16le);
    void request_user_list();

    void set_user_list_callback(std::function<void(const std::vector<CtrlUserInfo> &)> cb);
    void set_voice_callback(std::function<void(uint32_t, const QByteArray &)> cb);

signals:
    void pongReceived(quint64 pingId);

private slots:
    void onReadyRead();

private:
    void sendPacket(const QJsonObject &obj);

    QUdpSocket socket_;
    QHostAddress serverAddress_;
    quint16 serverPort_ = 0;
    std::function<void(const std::vector<CtrlUserInfo> &)> userListCallback_;
    std::function<void(uint32_t, const QByteArray &)> voiceCallback_;
    quint64 nextPingId_ = 1;
};

