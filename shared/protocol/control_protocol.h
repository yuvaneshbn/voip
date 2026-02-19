#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

struct CtrlUserInfo {
    uint32_t ssrc = 0;
    std::string name;
    uint8_t online = 0;
};

namespace ctrlproto {

inline QByteArray encode(const QJsonObject &obj) {
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

inline bool decode(const QByteArray &bytes, QJsonObject &out) {
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }
    out = doc.object();
    return true;
}

inline QJsonArray users_to_json(const std::vector<CtrlUserInfo> &users) {
    QJsonArray arr;
    for (const CtrlUserInfo &u : users) {
        QJsonObject o;
        o.insert(QStringLiteral("ssrc"), static_cast<double>(u.ssrc));
        o.insert(QStringLiteral("name"), QString::fromStdString(u.name));
        o.insert(QStringLiteral("online"), static_cast<int>(u.online));
        arr.push_back(o);
    }
    return arr;
}

inline std::vector<CtrlUserInfo> users_from_json(const QJsonArray &arr) {
    std::vector<CtrlUserInfo> users;
    users.reserve(static_cast<size_t>(arr.size()));

    for (const QJsonValue &v : arr) {
        if (!v.isObject()) {
            continue;
        }

        const QJsonObject o = v.toObject();
        CtrlUserInfo u;
        u.ssrc = static_cast<uint32_t>(o.value(QStringLiteral("ssrc")).toDouble(0));
        u.name = o.value(QStringLiteral("name")).toString().toStdString();
        u.online = static_cast<uint8_t>(o.value(QStringLiteral("online")).toInt(0));
        users.push_back(std::move(u));
    }

    return users;
}

} // namespace ctrlproto

