#pragma once

#include <QtCore/QString>

class Channel;
class User;

class Group {
public:
	static bool appliesToUser(const Channel &, const Channel &, const QString &, const User &) {
		return false;
	}
};
