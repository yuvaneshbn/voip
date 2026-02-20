

#include "Timer.h"

#include <cassert>
#include <chrono>

Timer::Timer(bool start)
	: m_start(start ? std::chrono::steady_clock::now() : std::chrono::time_point< std::chrono::steady_clock >{}) {
}

bool Timer::isElapsed(std::chrono::microseconds duration) {
	assert(isStarted());

	if (elapsed() > duration) {
		m_start += duration;

		return true;
	}

	return false;
}

std::chrono::microseconds Timer::restart() {
	std::chrono::microseconds elapsed = isStarted() ? this->elapsed() : std::chrono::microseconds{ 0 };

	m_start = std::chrono::steady_clock::now();

	return elapsed;
}

bool Timer::isStarted() const {
	return m_start != std::chrono::time_point< std::chrono::steady_clock >{};
}

bool Timer::operator<(const Timer &other) const {
	// Timer with newer start time is "smaller" in duration.
	return other.m_start < m_start;
}
