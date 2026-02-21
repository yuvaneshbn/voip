#pragma once

#include "Ban.h"
#include "database/ConnectionParameter.h"

#include <QtCore/QMap>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <vector>

class Server;
class ServerUser;
class Channel;
class ServerUserInfo;
class UserInfo;
class User;

class DBWrapper {
public:
	explicit DBWrapper(const ::nox::db::ConnectionParameter &) {}

	std::vector< Ban > getBans(unsigned int) const { return {}; }
	void saveBans(unsigned int, const std::vector< Ban > &) {}

	void initializeChannels(Server &) {}
	void initializeChannelLinks(Server &) {}

	void getConfigurationTo(unsigned int, const QString &, int &) const {}
	void getConfigurationTo(unsigned int, const QString &, bool &) const {}
	void getConfigurationTo(unsigned int, const QString &, QString &) const {}

	void updateLastDisconnect(unsigned int, unsigned int) {}
	void deleteChannel(unsigned int, unsigned int) {}
	void setLastChannel(unsigned int, const ServerUser &) {}
	void createChannel(unsigned int, const Channel &) {}
	void updateChannelData(unsigned int, const Channel &) {}

	void addChannelLink(unsigned int, unsigned int, unsigned int) {}
	void removeChannelLink(unsigned int, unsigned int, unsigned int) {}

	bool channelListenerExists(unsigned int, unsigned int, unsigned int) const { return false; }
	void addChannelListenerIfNotExists(unsigned int, unsigned int, unsigned int) {}
	void disableChannelListenerIfExists(unsigned int, unsigned int, unsigned int) {}
	void storeChannelListenerVolume(unsigned int, unsigned int, unsigned int, float) {}
	float getChannelListenerVolume(unsigned int, unsigned int, unsigned int) const { return 1.0f; }
	void deleteChannelListener(unsigned int, unsigned int, unsigned int) {}

	unsigned int getNextAvailableChannelID(unsigned int) const { return 1; }
	unsigned int getNextAvailableUserID(unsigned int) const { return 1; }

	int findRegisteredUserByCert(unsigned int, const QString &) const { return -1; }
	int findRegisteredUserByEmail(unsigned int, const QString &) const { return -1; }
	bool registeredUserExists(unsigned int, unsigned int) const { return false; }
	int registeredUserNameToID(unsigned int, const QString &) const { return -1; }
	QString getUserName(unsigned int, unsigned int) const { return QString(); }

	bool registerUser(unsigned int, const ServerUserInfo &) { return false; }
	bool unregisterUser(unsigned int, unsigned int) { return false; }
	bool setUserData(unsigned int, unsigned int, const ServerUserInfo &) { return false; }

	QMap< int, QString > getUserProperties(unsigned int, unsigned int) const { return {}; }
	QString getUserProperty(unsigned int, unsigned int, int) const { return QString(); }
	bool setUserProperties(unsigned int, unsigned int, const QMap< int, QString > &) { return false; }
	bool storeUserProperty(unsigned int, unsigned int, int, const QString &) { return false; }

	QByteArray getUserTexture(unsigned int, unsigned int) const { return {}; }
	bool storeUserTexture(unsigned int, unsigned int, const QByteArray &) { return false; }

	bool storeRegisteredUserPassword(unsigned int, unsigned int, const QString &) { return false; }

	QMap< int, QString > getRegisteredUserData(unsigned int, unsigned int) const { return {}; }
	void getRegisteredUserDetails(unsigned int, unsigned int, QString &, QStringList &) const {}
	void addAllRegisteredUserInfoTo(unsigned int, std::vector< UserInfo > &, QString = QString()) const {}
};
