#pragma once

#include "shared/VolumeAdjustment.h"

#include <QtCore/QHash>
#include <QtCore/QSet>

class ChannelListenerManager {
public:
	void addListener(unsigned int userSession, unsigned int channelId) {
		m_channelListeners[channelId].insert(userSession);
		m_userChannels[userSession].insert(channelId);
	}

	void removeListener(unsigned int userSession, unsigned int channelId) {
		m_channelListeners[channelId].remove(userSession);
		m_userChannels[userSession].remove(channelId);
	}

	bool isListening(unsigned int userSession, unsigned int channelId) const {
		return m_channelListeners.value(channelId).contains(userSession);
	}

	bool isListeningToAny(unsigned int userSession) const {
		return !m_userChannels.value(userSession).isEmpty();
	}

	QSet< unsigned int > getListenersForChannel(unsigned int channelId) const {
		return m_channelListeners.value(channelId);
	}

	QSet< unsigned int > getListenedChannelsForUser(unsigned int userSession) const {
		return m_userChannels.value(userSession);
	}

	VolumeAdjustment getListenerVolumeAdjustment(unsigned int userSession, unsigned int channelId) const {
		return m_volumeByListenerByChannel.value(channelId).value(userSession, VolumeAdjustment::fromFactor(1.0f));
	}

	void setListenerVolumeAdjustment(unsigned int userSession, unsigned int channelId, const VolumeAdjustment &volume) {
		m_volumeByListenerByChannel[channelId][userSession] = volume;
	}

private:
	QHash< unsigned int, QSet< unsigned int > > m_channelListeners;
	QHash< unsigned int, QSet< unsigned int > > m_userChannels;
	QHash< unsigned int, QHash< unsigned int, VolumeAdjustment > > m_volumeByListenerByChannel;
};
