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

constexpr uint8_t kVoiceFlagOpus = 0x01;
constexpr uint8_t kVoiceLayerShift = 1;
constexpr uint8_t kVoiceLayerMask = static_cast<uint8_t>(0x03 << kVoiceLayerShift);
constexpr uint8_t kVoiceLayerLow = 1;
constexpr uint8_t kVoiceLayerHigh = 2;

inline uint8_t voice_layer_from_flags(uint8_t flags) {
    const uint8_t layer = static_cast<uint8_t>((flags & kVoiceLayerMask) >> kVoiceLayerShift);
    if (layer == kVoiceLayerLow || layer == kVoiceLayerHigh) {
        return layer;
    }
    return kVoiceLayerHigh;
}

inline uint8_t voice_flags_with_layer(uint8_t baseFlags, uint8_t layer) {
    const uint8_t normalized = (layer == kVoiceLayerLow) ? kVoiceLayerLow : kVoiceLayerHigh;
    return static_cast<uint8_t>((baseFlags & ~kVoiceLayerMask) | (normalized << kVoiceLayerShift));
}

struct VoicePacket {
    uint32_t ssrc = 0;
    uint16_t sequence = 0;
    uint32_t timestampMs = 0;
    uint8_t flags = 0;
    QByteArray payload;
};

namespace detail {
constexpr uint32_t kVoiceMagic = 0x4E564F43U; // "NVOC"
constexpr uint8_t kVoiceVersion = 1;
constexpr int kVoiceHeaderBytes = 20;

inline void append_u16(QByteArray &out, uint16_t v) {
    out.append(static_cast<char>((v >> 8) & 0xFFU));
    out.append(static_cast<char>(v & 0xFFU));
}

inline void append_u32(QByteArray &out, uint32_t v) {
    out.append(static_cast<char>((v >> 24) & 0xFFU));
    out.append(static_cast<char>((v >> 16) & 0xFFU));
    out.append(static_cast<char>((v >> 8) & 0xFFU));
    out.append(static_cast<char>(v & 0xFFU));
}

inline uint16_t read_u16(const char *p) {
    return static_cast<uint16_t>((static_cast<uint8_t>(p[0]) << 8)
                                 | static_cast<uint8_t>(p[1]));
}

inline uint32_t read_u32(const char *p) {
    return (static_cast<uint32_t>(static_cast<uint8_t>(p[0])) << 24)
           | (static_cast<uint32_t>(static_cast<uint8_t>(p[1])) << 16)
           | (static_cast<uint32_t>(static_cast<uint8_t>(p[2])) << 8)
           | static_cast<uint32_t>(static_cast<uint8_t>(p[3]));
}
} // namespace detail

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

inline QByteArray encode_voice_packet(const VoicePacket &packet) {
    QByteArray out;
    out.reserve(detail::kVoiceHeaderBytes + packet.payload.size());

    detail::append_u32(out, detail::kVoiceMagic);
    out.append(static_cast<char>(detail::kVoiceVersion));
    out.append(static_cast<char>(detail::kVoiceHeaderBytes));
    out.append(static_cast<char>(packet.flags));
    out.append(static_cast<char>(0)); // reserved
    detail::append_u32(out, packet.ssrc);
    detail::append_u16(out, packet.sequence);
    detail::append_u16(out, static_cast<uint16_t>(packet.payload.size()));
    detail::append_u32(out, packet.timestampMs);
    out.append(packet.payload);
    return out;
}

inline bool decode_voice_packet(const QByteArray &bytes, VoicePacket &out) {
    if (bytes.size() < detail::kVoiceHeaderBytes) {
        return false;
    }

    const char *p = bytes.constData();
    const uint32_t magic = detail::read_u32(p);
    if (magic != detail::kVoiceMagic) {
        return false;
    }

    const uint8_t version = static_cast<uint8_t>(p[4]);
    const uint8_t headerBytes = static_cast<uint8_t>(p[5]);
    if (version != detail::kVoiceVersion || headerBytes != detail::kVoiceHeaderBytes) {
        return false;
    }

    out.ssrc = detail::read_u32(p + 8);
    out.sequence = detail::read_u16(p + 12);
    const uint16_t payloadBytes = detail::read_u16(p + 14);
    out.timestampMs = detail::read_u32(p + 16);
    out.flags = static_cast<uint8_t>(p[6]);

    if (bytes.size() != (detail::kVoiceHeaderBytes + static_cast<int>(payloadBytes))) {
        return false;
    }
    if (out.ssrc == 0 || payloadBytes == 0) {
        return false;
    }

    out.payload = bytes.mid(detail::kVoiceHeaderBytes, payloadBytes);
    return true;
}

} // namespace ctrlproto

