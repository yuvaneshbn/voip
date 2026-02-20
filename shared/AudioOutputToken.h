#ifndef MUMBLE_MUMBLE_AUDIOOUTPUTTOKEN_H_
#define MUMBLE_MUMBLE_AUDIOOUTPUTTOKEN_H_



class AudioOutputBuffer;

class AudioOutputToken {
public:
	AudioOutputToken() = default;
	AudioOutputToken(AudioOutputBuffer *buffer) : m_buffer(buffer) {}

	~AudioOutputToken() = default;

	inline bool operator==(const AudioOutputToken &rhs) const { return m_buffer == rhs.m_buffer; }
	inline bool operator!=(const AudioOutputToken &rhs) const { return m_buffer != rhs.m_buffer; }

	operator bool() const { return m_buffer; }

	template< typename UnderlyingType, typename SignalFunc, typename SlotObject, typename SlotFunc >
	void connect(SignalFunc signalFunc, SlotObject &slotObject, SlotFunc slotFunc) {
		assert(dynamic_cast< UnderlyingType * >(m_buffer));
		QObject::connect(dynamic_cast< UnderlyingType * >(m_buffer), signalFunc, &slotObject, slotFunc);
	}

private:
	AudioOutputBuffer *m_buffer = nullptr;

	friend class AudioOutput;
};

#endif // AUDIOOUTPUTTOKEN_H_
