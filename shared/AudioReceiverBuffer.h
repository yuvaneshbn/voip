#pragma once

#include "NoxProtocol.h"
#include "VolumeAdjustment.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_map>
#include <vector>

class ServerUser;



class AudioReceiver {
public:
	AudioReceiver(ServerUser &receiver, Nox::Protocol::audio_context_t context,
				  const VolumeAdjustment &volumeAdjustment);
	AudioReceiver(ServerUser &receiver, Nox::Protocol::audio_context_t context, VolumeAdjustment &&volumeAdjustment);

	ServerUser &getReceiver();
	const ServerUser &getReceiver() const;

	Nox::Protocol::audio_context_t getContext() const;
	void setContext(Nox::Protocol::audio_context_t context);

	const VolumeAdjustment &getVolumeAdjustment() const;
	void setVolumeAdjustment(const VolumeAdjustment &adjustment);
	void setVolumeAdjustment(VolumeAdjustment &&adjustment);

protected:
	std::reference_wrapper< ServerUser > m_receiver;
	Nox::Protocol::audio_context_t m_context = Nox::Protocol::AudioContext::INVALID;
	VolumeAdjustment m_volumeAdjustment         = VolumeAdjustment::fromFactor(1.0f);
};


template< typename Iterator > struct ReceiverRange {
	Iterator begin;
	Iterator end;
};


class AudioReceiverBuffer {
public:
	AudioReceiverBuffer();

	void addReceiver(const ServerUser &sender, ServerUser &receiver, Nox::Protocol::audio_context_t context,
					 bool includePositionalData,
					 const VolumeAdjustment &volumeAdjustment = VolumeAdjustment::fromFactor(1.0f));
	void forceAddReceiver(ServerUser &receiver, Nox::Protocol::audio_context_t context, bool includePositionalData,
						  const VolumeAdjustment &volumeAdjustment = VolumeAdjustment::fromFactor(1.0f));

	void preprocessBuffer();

	void clear();

	std::vector< AudioReceiver > &getReceivers(bool receivePositionalData);


	/**
	 * If two receivers differ in their volume adjustment factors by this much or more, they will never end up in the
	 * same receiver range
	 */
	constexpr static const float maxFactorDiff = 0.05f;
	/**
	 * If two receivers differ in their volume adjustment decibels by this much or more, they will never end up in the
	 * same receiver range
	 */
	constexpr static const int maxDecibelDiff = 5;

	template< typename Iterator > static ReceiverRange< Iterator > getReceiverRange(Iterator begin, Iterator end) {
		ReceiverRange< Iterator > range;
		range.begin = begin;

		if (begin == end) {
			// In this case, it is invalid to dereference begin (as is done further down) and thus we have to
			// return early instead.
			range.end = end;

			return range;
		}

		// Find a range, such that all receivers in [begin, end) are compatible in the sense that they will all receive
		// the exact same audio packet (thus: no re-encoding required between sending the packet to them).
		range.end = std::lower_bound(begin, end, *begin, [](const AudioReceiver &lhs, const AudioReceiver &rhs) {
			return lhs.getContext() == rhs.getContext()
				   // The factor difference caps audible differences for high volume adjustments (where 1dB is already a
				   // big difference). Thus, this is a cap on the absolute loudness difference.
				   && std::abs(lhs.getVolumeAdjustment().factor - rhs.getVolumeAdjustment().factor) < maxFactorDiff
				   // The dB difference caps audible difference for low volume adjustments. E.g. a factor difference of
				   // 0.05 for an adjustment of -24dB is a difference of more than 5dB and therefore audible. Thus, this
				   // is a cap on the relative loudness difference.
				   && std::abs(lhs.getVolumeAdjustment().dbAdjustment - rhs.getVolumeAdjustment().dbAdjustment)
						  < maxDecibelDiff;
		});

		return range;
	}

protected:
	std::vector< AudioReceiver > m_regularReceivers;
	std::unordered_map< const ServerUser *, std::size_t > m_regularReceiverIndices;
	std::vector< AudioReceiver > m_positionalReceivers;
	std::unordered_map< const ServerUser *, std::size_t > m_positionalReceiverIndices;

	void preprocessBuffer(std::vector< AudioReceiver > &receiverList);
};
