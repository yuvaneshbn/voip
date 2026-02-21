#pragma once

#include "HostAddress.h"

struct ServerAddress {
	HostAddress host;
	unsigned short port = 0;

	ServerAddress() = default;
	ServerAddress(const HostAddress &host_, unsigned short port_) : host(host_), port(port_) {}

	friend bool operator==(const ServerAddress &lhs, const ServerAddress &rhs) {
		return lhs.host == rhs.host && lhs.port == rhs.port;
	}
};

inline size_t qHash(const ServerAddress &address, size_t seed = 0) {
	return qHash(address.host, seed) ^ qHash(address.port, seed << 1);
}
