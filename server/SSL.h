#pragma once

#include <QtCore/QString>
#include <QtNetwork/QSsl>

namespace MumbleSSL {
inline QString protocolToString(QSsl::SslProtocol protocol) {
	return QString::number(static_cast< int >(protocol));
}

inline QString defaultOpenSSLCipherString() {
	return QStringLiteral("DEFAULT");
}
} // namespace MumbleSSL
