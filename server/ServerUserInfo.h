#pragma once

#include "User.h"

#include <QtCore/QList>
#include <QtCore/QStringList>
#include <QtNetwork/QSslCertificate>

class ServerUserInfo : public User {
public:
	QString qsComment;
	QString qsHash;
	bool bVerified = false;
	bool bOnline   = true;
	QStringList qslEmail;
	QList< QSslCertificate > qlCerts;
};
