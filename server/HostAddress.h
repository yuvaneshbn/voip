#pragma once

#include <QtCore/QMetaType>
#include <QtCore/QString>
#include <QtNetwork/QHostAddress>

class HostAddress {
public:
	HostAddress() = default;
	explicit HostAddress(const QHostAddress &address) : m_address(address) {}

	QHostAddress toAddress() const { return m_address; }
	bool isNull() const { return m_address.isNull(); }

	friend bool operator==(const HostAddress &lhs, const HostAddress &rhs) {
		return lhs.m_address == rhs.m_address;
	}

	friend bool operator!=(const HostAddress &lhs, const HostAddress &rhs) {
		return !(lhs == rhs);
	}

private:
	QHostAddress m_address;
};

inline size_t qHash(const HostAddress &address, size_t seed = 0) {
	return qHash(address.toAddress().toString(), seed);
}

Q_DECLARE_METATYPE(HostAddress)
