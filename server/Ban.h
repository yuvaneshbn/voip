#pragma once

#include <QtCore/QString>
#include <QtCore/QDateTime>
#include <QtNetwork/QHostAddress>

struct Ban {
	QHostAddress address;
	int mask             = 0;
	QString name;
	QString hash;
	QString reason;
	QDateTime start;
	unsigned int duration = 0;
};
