#include "control_client.h"

#include <QEventLoop>
#include <QDebug>
#include <QJsonArray>
#include <QTimer>

ControlClient::ControlClient(QObject *parent)
    : QObject(parent) {
}

bool ControlClient::initialize(const std::string &serverIp, quint16 port) {
    QHostAddress parsed;
    if (!parsed.setAddress(QString::fromStdString(serverIp))) {
        return false;
    }

    serverAddress_ = parsed;
    serverPort_ = port;

    QObject::connect(&socket_, &QUdpSocket::readyRead,
                     this, &ControlClient::onReadyRead,
                     Qt::UniqueConnection);

    if (socket_.state() == QAbstractSocket::BoundState) {
        return true;
    }

    return socket_.bind(QHostAddress::AnyIPv4, 0, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
}

void ControlClient::start() {
    QObject::connect(&socket_, &QUdpSocket::readyRead,
                     this, &ControlClient::onReadyRead,
                     Qt::UniqueConnection);
}

void ControlClient::stop() {
    socket_.close();
}

bool ControlClient::ping_server(int timeoutMs) {
    if (serverPort_ == 0) {
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

    // Send after hooking the waiter to avoid missing very fast local replies.
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
    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("join"));
    request.insert(QStringLiteral("ssrc"), static_cast<double>(ssrc));
    request.insert(QStringLiteral("name"), QString::fromStdString(name));
    sendPacket(request);
}

void ControlClient::leave(uint32_t ssrc) {
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

void ControlClient::send_voice(uint32_t ssrc, const QByteArray &pcm16le) {
    if (pcm16le.isEmpty()) {
        return;
    }
#ifndef QT_NO_DEBUG
    qDebug() << "Sending voice packet size:" << pcm16le.size();
#endif

    QJsonObject request;
    request.insert(QStringLiteral("type"), QStringLiteral("voice"));
    request.insert(QStringLiteral("ssrc"), static_cast<double>(ssrc));
    request.insert(QStringLiteral("pcm"), QString::fromLatin1(pcm16le.toBase64()));
    sendPacket(request);
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

void ControlClient::onReadyRead() {
    while (socket_.hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(static_cast<int>(socket_.pendingDatagramSize()));

        QHostAddress sender;
        quint16 senderPort = 0;
        socket_.readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);

        if (sender != serverAddress_ || senderPort != serverPort_) {
            continue;
        }

        QJsonObject msg;
        if (!ctrlproto::decode(datagram, msg)) {
            continue;
        }

        const QString type = msg.value(QStringLiteral("type")).toString();

        if (type == QStringLiteral("pong")) {
            const quint64 pingId = static_cast<quint64>(msg.value(QStringLiteral("ping_id")).toDouble(0));
            emit pongReceived(pingId);
            continue;
        }

        if (type == QStringLiteral("users") && userListCallback_) {
            const QJsonArray usersJson = msg.value(QStringLiteral("users")).toArray();
            userListCallback_(ctrlproto::users_from_json(usersJson));
            continue;
        }

        if (type == QStringLiteral("voice") && voiceCallback_) {
            const uint32_t fromSsrc = static_cast<uint32_t>(msg.value(QStringLiteral("ssrc")).toDouble(0));
            const QByteArray pcm = QByteArray::fromBase64(msg.value(QStringLiteral("pcm")).toString().toLatin1());
#ifndef QT_NO_DEBUG
            qDebug() << "Received voice packet size:" << pcm.size() << "from:" << fromSsrc;
#endif
            if (fromSsrc != 0 && !pcm.isEmpty()) {
                voiceCallback_(fromSsrc, pcm);
            }
        }
    }
}

void ControlClient::sendPacket(const QJsonObject &obj) {
    if (serverPort_ == 0 || serverAddress_.isNull()) {
        return;
    }

    const QByteArray payload = ctrlproto::encode(obj);
    socket_.writeDatagram(payload, serverAddress_, serverPort_);
}

