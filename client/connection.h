#pragma once

#include "NoxProtocol.h"
#include "crypto/CryptState.h"

#include <QtCore/QElapsedTimer>
#include <QtCore/QList>
#include <QtCore/QMutex>
#include <QtCore/QObject>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QSslCertificate>
#include <QtNetwork/QSslCipher>
#include <QtNetwork/QSslError>
#include <QtNetwork/QSslSocket>

#include <memory>

#ifdef Q_OS_WIN
#include "shared/win.h"
#include <ws2tcpip.h>
#endif

namespace google {
namespace protobuf {
class Message;
} // namespace protobuf
} // namespace google

class Connection : public QObject {
	Q_DISABLE_COPY(Connection)

protected:
	QSslSocket *qtsSocket = nullptr;
	QElapsedTimer qtLastPacket;
	Nox::Protocol::TCPMessageType m_type = static_cast< Nox::Protocol::TCPMessageType >(0);
	int iPacketLength                    = -1;

#ifdef Q_OS_WIN
	static HANDLE hQoS;
	DWORD dwFlow = 0;
#endif

protected:
	void socketRead();
	void socketError(QAbstractSocket::SocketError);
	void socketDisconnected();
	void socketSslErrors(const QList< QSslError > &errors);

public:
	void proceedAnyway();

public:
	void encrypted();
	void connectionClosed(QAbstractSocket::SocketError, const QString &reason);
	void message(Nox::Protocol::TCPMessageType type, const QByteArray &);
	void handleSslErrors(const QList< QSslError > &);

public:
	Connection(QObject *parent, QSslSocket *qtsSocket);
	~Connection() override;

	static void messageToNetwork(const ::google::protobuf::Message &msg, Nox::Protocol::TCPMessageType msgType,
								 QByteArray &cache);
	void sendMessage(const ::google::protobuf::Message &msg, Nox::Protocol::TCPMessageType msgType, QByteArray &cache);
	void sendMessage(const QByteArray &qbaMsg);
	void disconnectSocket(bool force = false);
	void forceFlush();
	qint64 activityTime() const;
	void resetActivityTime();

#ifdef MURMUR
	QMutex qmCrypt;
#endif
	std::unique_ptr< CryptState > csCrypt;

	QList< QSslCertificate > peerCertificateChain() const;
	QSslCipher sessionCipher() const;
	QSsl::SslProtocol sessionProtocol() const;
	QString sessionProtocolString() const;
	QHostAddress peerAddress() const;
	quint16 peerPort() const;
	QHostAddress localAddress() const;
	quint16 localPort() const;
	bool bDisconnectedEmitted = false;

	void setToS();
#ifdef Q_OS_WIN
	static void setQoS(HANDLE hParentQoS);
#endif
};
