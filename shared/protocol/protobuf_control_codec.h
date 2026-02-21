#pragma once

#include <QByteArray>

namespace protobufctrl {

// Frame prefix reserved for protobuf control payloads over TCP.
// Current runtime remains JSON-first; protobuf path can be enabled progressively.
constexpr char kPrefix[] = "PB1:";

inline bool is_protobuf_framed(const QByteArray &line) {
    return line.startsWith(kPrefix);
}

inline QByteArray strip_prefix(const QByteArray &line) {
    if (!is_protobuf_framed(line)) {
        return QByteArray{};
    }
    return line.mid(static_cast<int>(sizeof(kPrefix) - 1));
}

inline QByteArray with_prefix(const QByteArray &wireBytes) {
    QByteArray out(kPrefix);
    out.append(wireBytes);
    return out;
}

} // namespace protobufctrl

