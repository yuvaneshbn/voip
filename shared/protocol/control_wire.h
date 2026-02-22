#pragma once

#include <QByteArray>
#include <QJsonObject>

enum class ControlWireFormat {
    Json = 0,
    Protobuf = 1,
};

struct ControlWireMessage {
    QJsonObject json;
    ControlWireFormat format = ControlWireFormat::Json;
};

namespace controlwire {

QByteArray encode(const QJsonObject &obj, ControlWireFormat preferred = ControlWireFormat::Json);
bool decode(const QByteArray &line, ControlWireMessage &out);

} // namespace controlwire

