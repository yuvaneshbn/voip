#include "audiopreprocessor.h"

AudioPreprocessor::AudioPreprocessor(AudioPreprocessor &&other) : m_handle(std::exchange(other.m_handle, nullptr)) {
}

AudioPreprocessor::~AudioPreprocessor() {
	deinit();
}

AudioPreprocessor &AudioPreprocessor::operator=(AudioPreprocessor &&other) {
	if (this != &other) {
		m_handle = std::exchange(other.m_handle, nullptr);
	}
	return *this;
}

bool AudioPreprocessor::init(const std::uint32_t sampleRate, const std::uint32_t quantum) {
	(void) sampleRate;
	(void) quantum;
	m_handle = reinterpret_cast< SpeexPreprocessState_ * >(this);
	return true;
}

void AudioPreprocessor::deinit() {
	m_handle = nullptr;
}

bool AudioPreprocessor::run(std::int16_t &samples) {
	(void) samples;
	return true;
}

SpeexEchoState_ *AudioPreprocessor::getEchoState() {
	return nullptr;
}

bool AudioPreprocessor::setEchoState(SpeexEchoState_ *handle) {
	(void) handle;
	return true;
}

bool AudioPreprocessor::usesAGC() const { return false; }
bool AudioPreprocessor::setAGC(const bool enable) {
	(void) enable;
	return true;
}

std::int32_t AudioPreprocessor::getAGCDecrement() const { return 0; }
bool AudioPreprocessor::setAGCDecrement(const std::int32_t value) {
	(void) value;
	return true;
}
std::int32_t AudioPreprocessor::getAGCGain() const { return 0; }
std::int32_t AudioPreprocessor::getAGCIncrement() const { return 0; }
bool AudioPreprocessor::setAGCIncrement(const std::int32_t value) {
	(void) value;
	return true;
}
std::int32_t AudioPreprocessor::getAGCMaxGain() const { return 0; }
bool AudioPreprocessor::setAGCMaxGain(const std::int32_t value) {
	(void) value;
	return true;
}
std::int32_t AudioPreprocessor::getAGCTarget() const { return 0; }
bool AudioPreprocessor::setAGCTarget(const std::int32_t value) {
	(void) value;
	return true;
}

bool AudioPreprocessor::usesDenoise() const { return false; }
bool AudioPreprocessor::setDenoise(const bool enable) {
	(void) enable;
	return true;
}

bool AudioPreprocessor::usesDereverb() const { return false; }
bool AudioPreprocessor::setDereverb(const bool enable) {
	(void) enable;
	return true;
}

std::int32_t AudioPreprocessor::getNoiseSuppress() const { return 0; }
bool AudioPreprocessor::setNoiseSuppress(const std::int32_t value) {
	(void) value;
	return true;
}

AudioPreprocessor::psd_t AudioPreprocessor::getPSD() const { return {}; }
AudioPreprocessor::psd_t AudioPreprocessor::getNoisePSD() const { return {}; }
std::int32_t AudioPreprocessor::getSpeechProb() const { return 100; }

bool AudioPreprocessor::usesVAD() const { return false; }
bool AudioPreprocessor::setVAD(const bool enable) {
	(void) enable;
	return true;
}

bool AudioPreprocessor::getBool(const int op) const {
	(void) op;
	return false;
}

bool AudioPreprocessor::setBool(const int op, const bool value) {
	(void) op;
	(void) value;
	return true;
}

std::int32_t AudioPreprocessor::getInt32(const int op) const {
	(void) op;
	return 0;
}

bool AudioPreprocessor::setInt32(const int op, std::int32_t value) {
	(void) op;
	(void) value;
	return true;
}
