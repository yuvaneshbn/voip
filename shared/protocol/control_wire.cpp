#include "control_wire.h"

#include <QJsonArray>

#include "control_protocol.h"
#include "protobuf_control_codec.h"

#if defined(NOX_HAS_PROTOBUF_CONTROL)
#include "control.pb.h"
#endif

namespace {

#if defined(NOX_HAS_PROTOBUF_CONTROL)
std::vector<uint32_t> json_u32_array(const QJsonArray &arr) {
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

QJsonArray to_json_array(const google::protobuf::RepeatedField<uint32_t> &arr) {
    QJsonArray out;
    for (int i = 0; i < arr.size(); ++i) {
        out.push_back(static_cast<double>(arr.Get(i)));
    }
    return out;
}

bool json_to_envelope(const QJsonObject &obj, nox::control::v1::ControlEnvelope &env) {
    const QString type = obj.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("hello")) {
        auto *m = env.mutable_hello();
        m->set_client_name(obj.value(QStringLiteral("name")).toString().toStdString());
        m->set_udp_port(static_cast<uint32_t>(obj.value(QStringLiteral("udp_port")).toInt(0)));
        m->set_protocol_version(static_cast<uint32_t>(obj.value(QStringLiteral("protocol_version")).toInt(0)));
        return true;
    }
    if (type == QStringLiteral("hello_ack")) {
        auto *m = env.mutable_hello_ack();
        m->set_client_id(static_cast<uint32_t>(obj.value(QStringLiteral("client_id")).toDouble(0)));
        m->set_protocol_version(static_cast<uint32_t>(obj.value(QStringLiteral("protocol_version")).toInt(0)));
        return true;
    }
    if (type == QStringLiteral("join")) {
        auto *m = env.mutable_join();
        m->set_client_id(static_cast<uint32_t>(obj.value(QStringLiteral("ssrc")).toDouble(0)));
        m->set_name(obj.value(QStringLiteral("name")).toString().toStdString());
        m->set_udp_port(static_cast<uint32_t>(obj.value(QStringLiteral("udp_port")).toInt(0)));
        m->set_room(obj.value(QStringLiteral("room")).toString(QStringLiteral("default")).toStdString());
        return true;
    }
    if (type == QStringLiteral("leave")) {
        auto *m = env.mutable_leave();
        m->set_client_id(static_cast<uint32_t>(obj.value(QStringLiteral("ssrc")).toDouble(0)));
        return true;
    }
    if (type == QStringLiteral("talk")) {
        auto *m = env.mutable_talk();
        m->set_client_id(static_cast<uint32_t>(obj.value(QStringLiteral("ssrc")).toDouble(0)));
        const auto targets = json_u32_array(obj.value(QStringLiteral("targets")).toArray());
        for (uint32_t t : targets) {
            m->add_targets(t);
        }
        return true;
    }
    if (type == QStringLiteral("subscribe")) {
        auto *m = env.mutable_subscribe();
        m->set_client_id(static_cast<uint32_t>(obj.value(QStringLiteral("ssrc")).toDouble(0)));
        const auto sources = json_u32_array(obj.value(QStringLiteral("sources")).toArray());
        for (uint32_t s : sources) {
            m->add_sources(s);
        }
        m->set_max_streams(static_cast<uint32_t>(obj.value(QStringLiteral("max_streams")).toInt(4)));
        m->set_filter_enabled(obj.value(QStringLiteral("filter_enabled")).toBool(false));
        m->set_preferred_layer(obj.value(QStringLiteral("preferred_layer")).toString().toStdString());
        return true;
    }
    if (type == QStringLiteral("ping")) {
        auto *m = env.mutable_ping();
        m->set_ping_id(static_cast<uint64_t>(obj.value(QStringLiteral("ping_id")).toDouble(0)));
        return true;
    }
    if (type == QStringLiteral("pong")) {
        auto *m = env.mutable_pong();
        m->set_ping_id(static_cast<uint64_t>(obj.value(QStringLiteral("ping_id")).toDouble(0)));
        return true;
    }
    if (type == QStringLiteral("voice_feedback")) {
        auto *m = env.mutable_voice_feedback();
        m->set_reporter_client_id(static_cast<uint32_t>(obj.value(QStringLiteral("reporter_ssrc")).toDouble(0)));
        m->set_source_client_id(static_cast<uint32_t>(obj.value(QStringLiteral("source_ssrc")).toDouble(0)));
        m->set_loss_pct(static_cast<uint32_t>(obj.value(QStringLiteral("loss_pct")).toInt(0)));
        m->set_jitter_ms(static_cast<uint32_t>(obj.value(QStringLiteral("jitter_ms")).toInt(0)));
        m->set_plc_pct(static_cast<uint32_t>(obj.value(QStringLiteral("plc_pct")).toInt(0)));
        m->set_fec_pct(static_cast<uint32_t>(obj.value(QStringLiteral("fec_pct")).toInt(0)));
        m->set_rtt_ms(static_cast<uint32_t>(obj.value(QStringLiteral("rtt_ms")).toInt(0)));
        return true;
    }
    if (type == QStringLiteral("list")) {
        env.mutable_list_users();
        return true;
    }
    if (type == QStringLiteral("users")) {
        auto *users = env.mutable_users();
        const QJsonArray arr = obj.value(QStringLiteral("users")).toArray();
        for (const QJsonValue &v : arr) {
            if (!v.isObject()) {
                continue;
            }
            const QJsonObject u = v.toObject();
            auto *item = users->add_users();
            item->set_client_id(static_cast<uint32_t>(u.value(QStringLiteral("ssrc")).toDouble(0)));
            item->set_name(u.value(QStringLiteral("name")).toString().toStdString());
            item->set_online(u.value(QStringLiteral("online")).toInt(0) != 0);
            item->set_room(u.value(QStringLiteral("room")).toString(QStringLiteral("default")).toStdString());
        }
        return true;
    }
    return false;
}

bool envelope_to_json(const nox::control::v1::ControlEnvelope &env, QJsonObject &out) {
    using nox::control::v1::ControlEnvelope;
    switch (env.payload_case()) {
    case ControlEnvelope::kHello: {
        out.insert(QStringLiteral("type"), QStringLiteral("hello"));
        out.insert(QStringLiteral("name"), QString::fromStdString(env.hello().client_name()));
        out.insert(QStringLiteral("udp_port"), static_cast<int>(env.hello().udp_port()));
        out.insert(QStringLiteral("protocol_version"), static_cast<int>(env.hello().protocol_version()));
        return true;
    }
    case ControlEnvelope::kHelloAck: {
        out.insert(QStringLiteral("type"), QStringLiteral("hello_ack"));
        out.insert(QStringLiteral("client_id"), static_cast<double>(env.hello_ack().client_id()));
        out.insert(QStringLiteral("protocol_version"), static_cast<int>(env.hello_ack().protocol_version()));
        return true;
    }
    case ControlEnvelope::kJoin: {
        out.insert(QStringLiteral("type"), QStringLiteral("join"));
        out.insert(QStringLiteral("ssrc"), static_cast<double>(env.join().client_id()));
        out.insert(QStringLiteral("name"), QString::fromStdString(env.join().name()));
        out.insert(QStringLiteral("udp_port"), static_cast<int>(env.join().udp_port()));
        out.insert(QStringLiteral("room"), QString::fromStdString(env.join().room()));
        return true;
    }
    case ControlEnvelope::kLeave: {
        out.insert(QStringLiteral("type"), QStringLiteral("leave"));
        out.insert(QStringLiteral("ssrc"), static_cast<double>(env.leave().client_id()));
        return true;
    }
    case ControlEnvelope::kTalk: {
        out.insert(QStringLiteral("type"), QStringLiteral("talk"));
        out.insert(QStringLiteral("ssrc"), static_cast<double>(env.talk().client_id()));
        out.insert(QStringLiteral("targets"), to_json_array(env.talk().targets()));
        return true;
    }
    case ControlEnvelope::kSubscribe: {
        out.insert(QStringLiteral("type"), QStringLiteral("subscribe"));
        out.insert(QStringLiteral("ssrc"), static_cast<double>(env.subscribe().client_id()));
        out.insert(QStringLiteral("sources"), to_json_array(env.subscribe().sources()));
        out.insert(QStringLiteral("max_streams"), static_cast<int>(env.subscribe().max_streams()));
        out.insert(QStringLiteral("filter_enabled"), env.subscribe().filter_enabled());
        out.insert(QStringLiteral("preferred_layer"), QString::fromStdString(env.subscribe().preferred_layer()));
        return true;
    }
    case ControlEnvelope::kPing: {
        out.insert(QStringLiteral("type"), QStringLiteral("ping"));
        out.insert(QStringLiteral("ping_id"), static_cast<double>(env.ping().ping_id()));
        return true;
    }
    case ControlEnvelope::kPong: {
        out.insert(QStringLiteral("type"), QStringLiteral("pong"));
        out.insert(QStringLiteral("ping_id"), static_cast<double>(env.pong().ping_id()));
        return true;
    }
    case ControlEnvelope::kVoiceFeedback: {
        out.insert(QStringLiteral("type"), QStringLiteral("voice_feedback"));
        out.insert(QStringLiteral("reporter_ssrc"), static_cast<double>(env.voice_feedback().reporter_client_id()));
        out.insert(QStringLiteral("source_ssrc"), static_cast<double>(env.voice_feedback().source_client_id()));
        out.insert(QStringLiteral("loss_pct"), static_cast<int>(env.voice_feedback().loss_pct()));
        out.insert(QStringLiteral("jitter_ms"), static_cast<int>(env.voice_feedback().jitter_ms()));
        out.insert(QStringLiteral("plc_pct"), static_cast<int>(env.voice_feedback().plc_pct()));
        out.insert(QStringLiteral("fec_pct"), static_cast<int>(env.voice_feedback().fec_pct()));
        out.insert(QStringLiteral("rtt_ms"), static_cast<int>(env.voice_feedback().rtt_ms()));
        return true;
    }
    case ControlEnvelope::kListUsers: {
        out.insert(QStringLiteral("type"), QStringLiteral("list"));
        return true;
    }
    case ControlEnvelope::kUsers: {
        out.insert(QStringLiteral("type"), QStringLiteral("users"));
        QJsonArray arr;
        for (int i = 0; i < env.users().users_size(); ++i) {
            const auto &u = env.users().users(i);
            QJsonObject item;
            item.insert(QStringLiteral("ssrc"), static_cast<double>(u.client_id()));
            item.insert(QStringLiteral("name"), QString::fromStdString(u.name()));
            item.insert(QStringLiteral("online"), u.online() ? 1 : 0);
            item.insert(QStringLiteral("room"), QString::fromStdString(u.room()));
            arr.push_back(item);
        }
        out.insert(QStringLiteral("users"), arr);
        return true;
    }
    case ControlEnvelope::PAYLOAD_NOT_SET:
        break;
    }
    return false;
}
#endif

} // namespace

namespace controlwire {

QByteArray encode(const QJsonObject &obj, ControlWireFormat preferred) {
#if defined(NOX_HAS_PROTOBUF_CONTROL)
    if (preferred == ControlWireFormat::Protobuf) {
        nox::control::v1::ControlEnvelope env;
        if (json_to_envelope(obj, env)) {
            std::string payload;
            if (env.SerializeToString(&payload)) {
                return protobufctrl::with_prefix(QByteArray::fromStdString(payload));
            }
        }
    }
#else
    Q_UNUSED(preferred);
#endif
    return ctrlproto::encode(obj);
}

bool decode(const QByteArray &line, ControlWireMessage &out) {
#if defined(NOX_HAS_PROTOBUF_CONTROL)
    if (protobufctrl::is_protobuf_framed(line)) {
        const QByteArray payload = protobufctrl::strip_prefix(line);
        nox::control::v1::ControlEnvelope env;
        if (!env.ParseFromArray(payload.constData(), payload.size())) {
            return false;
        }
        QJsonObject decoded;
        if (!envelope_to_json(env, decoded)) {
            return false;
        }
        out.json = decoded;
        out.format = ControlWireFormat::Protobuf;
        return true;
    }
#endif
    QJsonObject decoded;
    if (!ctrlproto::decode(line, decoded)) {
        return false;
    }
    out.json = decoded;
    out.format = ControlWireFormat::Json;
    return true;
}

} // namespace controlwire

