#include "NoxProtocol.h"

#include <algorithm>
#include <cassert>
#include <cstring>

namespace Nox {
namespace Protocol {

std::string messageTypeName(TCPMessageType type) {
#define PROCESS_NOX_TCP_MESSAGE(name, value) \
	case TCPMessageType::name:                  \
		return #name;
	switch (type) { NOX_ALL_TCP_MESSAGES }
#undef PROCESS_NOX_TCP_MESSAGE
	return "Unknown";
}

std::string messageTypeName(UDPMessageType type) {
#define PROCESS_NOX_UDP_MESSAGE(name, value) \
	case UDPMessageType::name:                  \
		return #name;
	switch (type) { NOX_ALL_UDP_MESSAGES }
#undef PROCESS_NOX_UDP_MESSAGE
	return "Unknown";
}

std::string messageTypeName(LegacyUDPMessageType type) {
	switch (type) {
		case LegacyUDPMessageType::VoiceCELTAlpha:
			return "VoiceCELTAlpha";
		case LegacyUDPMessageType::Ping:
			return "Ping";
		case LegacyUDPMessageType::VoiceSpeex:
			return "VoiceSpeex";
		case LegacyUDPMessageType::VoiceCELTBeta:
			return "VoiceCELTBeta";
		case LegacyUDPMessageType::VoiceOpus:
			return "VoiceOpus";
	}
	return "Unknown";
}

bool protocolVersionsAreCompatible(Version::full_t lhs, Version::full_t rhs) {
	return (lhs < PROTOBUF_INTRODUCTION_VERSION) == (rhs < PROTOBUF_INTRODUCTION_VERSION);
}

template <Role role> ProtocolHandler<role>::ProtocolHandler(Version::full_t protocolVersion)
	: m_protocolVersion(protocolVersion) {}

template <Role role> Version::full_t ProtocolHandler<role>::getProtocolVersion() const {
	return m_protocolVersion;
}

template <Role role> void ProtocolHandler<role>::setProtocolVersion(Version::full_t protocolVersion) {
	m_protocolVersion = protocolVersion;
}

template <Role role>
UDPAudioEncoder<role>::UDPAudioEncoder(Version::full_t protocolVersion) : ProtocolHandler<role>(protocolVersion) {
	m_byteBuffer.resize(MAX_UDP_PACKET_SIZE);
}

template <Role role> void UDPAudioEncoder<role>::prepareAudioPacket(const AudioData &data) {
	if (this->m_protocolVersion >= PROTOBUF_INTRODUCTION_VERSION) {
		prepareAudioPacket_protobuf(data);
		return;
	}

	prepareAudioPacket_legacy(data);
}

template <Role role> std::span<const byte> UDPAudioEncoder<role>::updateAudioPacket(const AudioData &data) {
	if (this->m_protocolVersion >= PROTOBUF_INTRODUCTION_VERSION) {
		return updateAudioPacket_protobuf(data);
	}

	return updateAudioPacket_legacy(data);
}

template <Role role> void UDPAudioEncoder<role>::addPositionalData(const AudioData &data) {
	if (this->m_protocolVersion >= PROTOBUF_INTRODUCTION_VERSION) {
		addPositionalData_protobuf(data);
		return;
	}

	addPositionalData_legacy(data);
}

template <Role role> void UDPAudioEncoder<role>::dropPositionalData() {
	if (m_byteBuffer.size() > m_staticPartSize) {
		m_byteBuffer.resize(m_staticPartSize);
	}
	m_positionalAudioSize = m_staticPartSize;
}

template <Role role> std::span<const byte> UDPAudioEncoder<role>::encodeAudioPacket(const AudioData &data) {
	prepareAudioPacket(data);
	addPositionalData(data);
	return updateAudioPacket(data);
}

template <Role role> void UDPAudioEncoder<role>::prepareAudioPacket_legacy(const AudioData &data) {
	m_byteBuffer.clear();
	m_byteBuffer.push_back(static_cast<byte>(UDPMessageType::Audio));
	m_byteBuffer.push_back(static_cast<byte>(data.usedCodec));
	const std::size_t copySize = std::min<std::size_t>(data.payload.size(), MAX_UDP_PACKET_SIZE - m_byteBuffer.size());
	m_byteBuffer.insert(m_byteBuffer.end(), data.payload.data(), data.payload.data() + copySize);
	m_staticPartSize      = m_byteBuffer.size();
	m_positionalAudioSize = m_staticPartSize;
}

template <Role role> std::span<const byte> UDPAudioEncoder<role>::updateAudioPacket_legacy(const AudioData &data) {
	(void) data;
	return { m_byteBuffer.data(), m_byteBuffer.size() };
}

template <Role role> void UDPAudioEncoder<role>::addPositionalData_legacy(const AudioData &data) {
	if (!data.containsPositionalData) {
		return;
	}
	for (float v : data.position) {
		if (m_byteBuffer.size() + sizeof(float) > MAX_UDP_PACKET_SIZE) {
			break;
		}
		const byte *ptr = reinterpret_cast<const byte *>(&v);
		m_byteBuffer.insert(m_byteBuffer.end(), ptr, ptr + sizeof(float));
	}
	m_positionalAudioSize = m_byteBuffer.size();
}

template <Role role> void UDPAudioEncoder<role>::prepareAudioPacket_protobuf(const AudioData &data) {
	prepareAudioPacket_legacy(data);
}
template <Role role> std::span<const byte> UDPAudioEncoder<role>::updateAudioPacket_protobuf(const AudioData &data) {
	return updateAudioPacket_legacy(data);
}
template <Role role> void UDPAudioEncoder<role>::addPositionalData_protobuf(const AudioData &data) {
	addPositionalData_legacy(data);
}
template <Role role> void UDPAudioEncoder<role>::preparePreEncodedSnippets() {}
template <Role role> std::span<const byte> UDPAudioEncoder<role>::getPreEncodedContext(audio_context_t) const {
	return {};
}
template <Role role>
std::span<const byte> UDPAudioEncoder<role>::getPreEncodedVolumeAdjustment(const VolumeAdjustment &) const {
	return {};
}

template <Role role>
UDPPingEncoder<role>::UDPPingEncoder(Version::full_t protocolVersion) : ProtocolHandler<role>(protocolVersion) {
	m_byteBuffer.resize(MAX_UDP_PACKET_SIZE);
}

template <Role role> std::span<const byte> UDPPingEncoder<role>::encodePingPacket(const PingData &data) {
	if (this->m_protocolVersion >= PROTOBUF_INTRODUCTION_VERSION) {
		return encodePingPacket_protobuf(data);
	}

	return encodePingPacket_legacy(data);
}

template <Role role> std::span<const byte> UDPPingEncoder<role>::encodePingPacket_legacy(const PingData &data) {
	m_byteBuffer.clear();
	m_byteBuffer.push_back(static_cast<byte>(UDPMessageType::Ping));
	const byte *ptr = reinterpret_cast<const byte *>(&data.timestamp);
	m_byteBuffer.insert(m_byteBuffer.end(), ptr, ptr + sizeof(data.timestamp));
	return { m_byteBuffer.data(), m_byteBuffer.size() };
}
template <Role role> std::span<const byte> UDPPingEncoder<role>::encodePingPacket_protobuf(const PingData &data) {
	return encodePingPacket_legacy(data);
}

template <Role role>
UDPDecoder<role>::UDPDecoder(Version::full_t protocolVersion)
	: ProtocolHandler<role>(protocolVersion), m_messageType(UDPMessageType::Ping) {
	m_byteBuffer.resize(MAX_UDP_PACKET_SIZE);
}

template <Role role> std::span<byte> UDPDecoder<role>::getBuffer() {
	return { m_byteBuffer.data(), m_byteBuffer.size() };
}

template <Role role> bool UDPDecoder<role>::decode(const std::span<const byte> data, bool restrictToPing) {
	if (data.empty()) {
		return false;
	}
	m_messageType = static_cast<UDPMessageType>(data[0]);
	if (m_messageType == UDPMessageType::Ping) {
		return decodePing(data);
	}
	if (restrictToPing) {
		return false;
	}

	if (this->m_protocolVersion >= PROTOBUF_INTRODUCTION_VERSION) {
		return decodeAudio_protobuf(data);
	}

	return decodeAudio_legacy(data, AudioCodec::Opus);
}

template <Role role> bool UDPDecoder<role>::decodePing(const std::span<const byte> data) {
	if (this->m_protocolVersion >= PROTOBUF_INTRODUCTION_VERSION) {
		return decodePing_protobuf(data);
	}

	return decodePing_legacy(data);
}

template <Role role> UDPMessageType UDPDecoder<role>::getMessageType() const {
	return m_messageType;
}

template <Role role> AudioData UDPDecoder<role>::getAudioData() const {
	return m_audioData;
}

template <Role role> PingData UDPDecoder<role>::getPingData() const {
	return m_pingData;
}

template <Role role> bool UDPDecoder<role>::decodePing_legacy(const std::span<const byte> data) {
	if (data.size() < 1 + sizeof(std::uint64_t)) {
		return false;
	}
	std::memcpy(&m_pingData.timestamp, data.data() + 1, sizeof(std::uint64_t));
	return true;
}

template <Role role> bool UDPDecoder<role>::decodePing_protobuf(const std::span<const byte> data) {
	return decodePing_legacy(data);
}

template <Role role>
bool UDPDecoder<role>::decodeAudio_legacy(const std::span<const byte> data, AudioCodec codec) {
	(void) codec;
	m_audioData.payload = data.size() > 1 ? data.subspan(1) : std::span<const byte>();
	return true;
}

template <Role role> bool UDPDecoder<role>::decodeAudio_protobuf(const std::span<const byte> data) {
	return decodeAudio_legacy(data, AudioCodec::Opus);
}

#define PROCESS_ROLE(role_type)                        \
	template class ProtocolHandler<role_type>;     \
	template class UDPAudioEncoder<role_type>;     \
	template class UDPPingEncoder<role_type>;      \
	template class UDPDecoder<role_type>;

PROCESS_ROLE(Role::Client)
PROCESS_ROLE(Role::Server)

#undef PROCESS_ROLE

} // namespace Protocol
} // namespace Nox
