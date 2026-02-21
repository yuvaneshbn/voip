#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <cstdint>
#include <vector>

namespace hybridctrl {

constexpr int kProtocolVersion = 2;

struct HelloRequest {
    QString clientName;
    quint16 udpPort = 0;
};

struct HelloAck {
    uint32_t clientId = 0;
    int protocolVersion = kProtocolVersion;
};

struct JoinRoom {
    uint32_t clientId = 0;
    QString room;
};

struct LeaveRoom {
    uint32_t clientId = 0;
    QString room;
};

struct TalkTargets {
    uint32_t clientId = 0;
    std::vector<uint32_t> targets;
};

struct Subscribe {
    uint32_t clientId = 0;
    std::vector<uint32_t> sources;
    int maxStreams = 4;
    bool filterEnabled = false;
    QString preferredLayer = QStringLiteral("auto");
};

inline QJsonArray u32_to_json(const std::vector<uint32_t> &values) {
    QJsonArray arr;
    for (uint32_t v : values) {
        arr.push_back(static_cast<double>(v));
    }
    return arr;
}

inline std::vector<uint32_t> u32_from_json(const QJsonArray &arr) {
    std::vector<uint32_t> out;
    out.reserve(static_cast<size_t>(arr.size()));
    for (const QJsonValue &v : arr) {
        const uint32_t n = static_cast<uint32_t>(v.toDouble(0));
        if (n != 0) {
            out.push_back(n);
        }
    }
    return out;
}

inline QJsonObject to_json(const HelloRequest &m) {
    QJsonObject o;
    o.insert(QStringLiteral("type"), QStringLiteral("hello"));
    o.insert(QStringLiteral("name"), m.clientName);
    o.insert(QStringLiteral("udp_port"), static_cast<int>(m.udpPort));
    o.insert(QStringLiteral("protocol_version"), kProtocolVersion);
    return o;
}

inline QJsonObject to_json(const HelloAck &m) {
    QJsonObject o;
    o.insert(QStringLiteral("type"), QStringLiteral("hello_ack"));
    o.insert(QStringLiteral("client_id"), static_cast<double>(m.clientId));
    o.insert(QStringLiteral("protocol_version"), m.protocolVersion);
    return o;
}

inline QJsonObject to_json(const JoinRoom &m) {
    QJsonObject o;
    o.insert(QStringLiteral("type"), QStringLiteral("join_room"));
    o.insert(QStringLiteral("client_id"), static_cast<double>(m.clientId));
    o.insert(QStringLiteral("room"), m.room);
    return o;
}

inline QJsonObject to_json(const LeaveRoom &m) {
    QJsonObject o;
    o.insert(QStringLiteral("type"), QStringLiteral("leave_room"));
    o.insert(QStringLiteral("client_id"), static_cast<double>(m.clientId));
    o.insert(QStringLiteral("room"), m.room);
    return o;
}

inline QJsonObject to_json(const TalkTargets &m) {
    QJsonObject o;
    o.insert(QStringLiteral("type"), QStringLiteral("talk"));
    o.insert(QStringLiteral("client_id"), static_cast<double>(m.clientId));
    o.insert(QStringLiteral("targets"), u32_to_json(m.targets));
    return o;
}

inline QJsonObject to_json(const Subscribe &m) {
    QJsonObject o;
    o.insert(QStringLiteral("type"), QStringLiteral("subscribe"));
    o.insert(QStringLiteral("client_id"), static_cast<double>(m.clientId));
    o.insert(QStringLiteral("sources"), u32_to_json(m.sources));
    o.insert(QStringLiteral("max_streams"), m.maxStreams);
    o.insert(QStringLiteral("filter_enabled"), m.filterEnabled);
    o.insert(QStringLiteral("preferred_layer"), m.preferredLayer);
    return o;
}

} // namespace hybridctrl

