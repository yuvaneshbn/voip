#pragma once

#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QSet>
#include <QtCore/QHash>
#include <QtCore/QString>

class User;
class ChanACL;

class Channel : public QObject {
	Q_OBJECT
public:
	unsigned int iId                 = 0;
	QString qsName;
	Channel *cParent                 = nullptr;
	bool bInheritACL                 = true;
	bool bTemporary                  = false;
	unsigned int uiMaxUsers          = 0;
	QList< ChanACL * > qlACL;
	QList< Channel * > qlChannels;
	QList< User * > qlUsers;
	QHash< unsigned int, Channel * > qhLinks;

	explicit Channel(unsigned int id = 0, const QString &name = QString(), Channel *parent = nullptr)
		: QObject(parent), iId(id), qsName(name), cParent(parent) {
		if (cParent) {
			cParent->qlChannels << this;
		}
	}

	void removeChannel(Channel *channel) {
		qlChannels.removeAll(channel);
	}

	QSet< Channel * > allLinks() const {
		QSet< Channel * > out;
		for (Channel *c : qhLinks) {
			if (c) {
				out.insert(c);
			}
		}
		return out;
	}

	QSet< Channel * > allChildren() const {
		QSet< Channel * > out;
		for (Channel *child : qlChannels) {
			if (!child) {
				continue;
			}
			out.insert(child);
			out.unite(child->allChildren());
		}
		return out;
	}
};
