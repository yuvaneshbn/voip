#include "connection.h"

#include <QtCore/QtEndian>

#if __has_include(<google/protobuf/message.h>)
#	include <google/protobuf/message.h>
#	define NOX_HAS_PROTOBUF_MESSAGE 1
#else
#	define NOX_HAS_PROTOBUF_MESSAGE 0
#endif

Connection::Connection(QObject *p, QSslSocket *qtsSock) : QObject(p), qtsSocket(qtsSock) {
	if (qtsSocket) {
		qtsSocket->setParent(this);
		connect(qtsSocket, &QSslSocket::errorOccurred, this, &Connection::socketError);
		connect(qtsSocket, &QSslSocket::encrypted, this, &Connection::encrypted);
		connect(qtsSocket, &QSslSocket::readyRead, this, &Connection::socketRead);
		connect(qtsSocket, &QSslSocket::disconnected, this, &Connection::socketDisconnected);
		connect(qtsSocket, &QSslSocket::sslErrors, this, &Connection::socketSslErrors);
		qtLastPacket.start();
	}
}

Connection::~Connection() = default;

void Connection::encrypted() {}
void Connection::connectionClosed(QAbstractSocket::SocketError, const QString &) {}
void Connection::message(Nox::Protocol::TCPMessageType, const QByteArray &) {}
void Connection::handleSslErrors(const QList< QSslError > &) {}

void Connection::socketRead() {
	if (!qtsSocket) {
		return;
	}

	while (true) {
		qint64 available = qtsSocket->bytesAvailable();
		if (iPacketLength == -1) {
			if (available < 6) {
				return;
			}

			unsigned char buffer[6];
			if (qtsSocket->read(reinterpret_cast< char * >(buffer), 6) != 6) {
				return;
			}

			m_type        = static_cast< Nox::Protocol::TCPMessageType >(qFromBigEndian< quint16 >(&buffer[0]));
			iPacketLength = qFromBigEndian< int >(&buffer[2]);
			available -= 6;
		}

		if ((iPacketLength == -1) || (available < iPacketLength)) {
			return;
		}

		if (iPacketLength > 0x7fffff) {
			qWarning() << "Host tried to send huge packet";
			disconnectSocket(true);
			return;
		}

		QByteArray payload = qtsSocket->read(iPacketLength);
		if (payload.size() != iPacketLength) {
			return;
		}

		iPacketLength = -1;
		emit message(m_type, payload);
	}
}

void Connection::socketError(QAbstractSocket::SocketError err) {
	if (!qtsSocket) {
		emit connectionClosed(err, QString());
		return;
	}
	emit connectionClosed(err, qtsSocket->errorString());
}

void Connection::socketDisconnected() {
	emit connectionClosed(QAbstractSocket::UnknownSocketError, QString());
}

void Connection::socketSslErrors(const QList< QSslError > &errors) {
	emit handleSslErrors(errors);
}

void Connection::proceedAnyway() {
	if (qtsSocket) {
		qtsSocket->ignoreSslErrors();
	}
}

void Connection::messageToNetwork(const ::google::protobuf::Message &msg, Nox::Protocol::TCPMessageType msgType,
								  QByteArray &cache) {
#if NOX_HAS_PROTOBUF_MESSAGE
#if GOOGLE_PROTOBUF_VERSION >= 3004000
	const std::size_t len = msg.ByteSizeLong();
#else
	// ByteSize() has been deprecated as of protobuf v3.4
	const std::size_t len = msg.ByteSize();
#endif
	if (len > 0x7fffff) {
		return;
	}

	cache.resize(static_cast< int >(len + 6));
	unsigned char *data = reinterpret_cast< unsigned char * >(cache.data());
	qToBigEndian< quint16 >(static_cast< quint16 >(msgType), &data[0]);
	qToBigEndian< quint32 >(static_cast< quint32 >(len), &data[2]);

	msg.SerializeToArray(data + 6, static_cast< int >(len));
#else
	Q_UNUSED(msg);
	cache.resize(6);
	unsigned char *data = reinterpret_cast< unsigned char * >(cache.data());
	qToBigEndian< quint16 >(static_cast< quint16 >(msgType), &data[0]);
	qToBigEndian< quint32 >(0, &data[2]);
#endif
}

void Connection::sendMessage(const ::google::protobuf::Message &msg, Nox::Protocol::TCPMessageType msgType,
							 QByteArray &cache) {
	if (cache.isEmpty()) {
		messageToNetwork(msg, msgType, cache);
	}
	sendMessage(cache);
}

void Connection::sendMessage(const QByteArray &qbaMsg) {
	if (qtsSocket && !qbaMsg.isEmpty()) {
		qtsSocket->write(qbaMsg);
	}
}

void Connection::disconnectSocket(bool force) {
	if (!qtsSocket) {
		return;
	}
	if (force) {
		qtsSocket->abort();
	} else {
		qtsSocket->disconnectFromHost();
	}
}

void Connection::forceFlush() {
	if (qtsSocket) {
		qtsSocket->flush();
	}
}

qint64 Connection::activityTime() const {
	return qtLastPacket.isValid() ? qtLastPacket.elapsed() : 0;
}

void Connection::resetActivityTime() {
	qtLastPacket.restart();
}

QList< QSslCertificate > Connection::peerCertificateChain() const {
	return qtsSocket ? qtsSocket->peerCertificateChain() : QList< QSslCertificate >();
}

QSslCipher Connection::sessionCipher() const {
	return qtsSocket ? qtsSocket->sessionCipher() : QSslCipher();
}

QSsl::SslProtocol Connection::sessionProtocol() const {
	return qtsSocket ? qtsSocket->sessionProtocol() : QSsl::UnknownProtocol;
}

QString Connection::sessionProtocolString() const {
	return QString::number(static_cast< int >(sessionProtocol()));
}

QHostAddress Connection::peerAddress() const {
	return qtsSocket ? qtsSocket->peerAddress() : QHostAddress();
}

quint16 Connection::peerPort() const {
	return qtsSocket ? qtsSocket->peerPort() : 0;
}

QHostAddress Connection::localAddress() const {
	return qtsSocket ? qtsSocket->localAddress() : QHostAddress();
}

quint16 Connection::localPort() const {
	return qtsSocket ? qtsSocket->localPort() : 0;
}

void Connection::setToS() {
}

#ifdef Q_OS_WIN
HANDLE Connection::hQoS = nullptr;

void Connection::setQoS(HANDLE hParentQoS) {
	hQoS = hParentQoS;
}
#endif
