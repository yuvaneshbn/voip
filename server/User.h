#pragma once

#include "shared/Version.h"

#include <QtCore/QByteArray>
#include <QtCore/QString>

class Channel;

class User {
public:
	virtual ~User() = default;

	int iId                 = -1;
	unsigned int uiSession  = 0;
	QString qsName;
	Channel *cChannel       = nullptr;
	bool bMute              = false;
	bool bDeaf              = false;
	bool bSelfMute          = false;
	bool bSelfDeaf          = false;
	bool bSuppress          = false;
	bool bPrioritySpeaker   = false;
	bool bRecording         = false;
	QByteArray qbaTextureHash;
	QByteArray qbaCommentHash;
	QString ssContext;
	Version::full_t m_version = Version::UNKNOWN;
};

struct UserInfo {
	int userID = -1;
	QString name;
};
