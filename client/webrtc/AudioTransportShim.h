#pragma once

#if __has_include("api/audio/audio_transport.h")
#include "api/audio/audio_transport.h"
namespace webrtc {
using AudioTransport = ::webrtc::AudioTransport;
}
#else
#include <cstddef>
#include <cstdint>

namespace webrtc {
class AudioTransport {
public:
    virtual ~AudioTransport() = default;

    virtual int32_t RecordedDataIsAvailable(const void* audioSamples,
                                            size_t nSamples,
                                            size_t nBytesPerSample,
                                            size_t nChannels,
                                            uint32_t samplesPerSec,
                                            uint32_t totalDelayMS,
                                            int32_t clockDrift,
                                            uint32_t currentMicLevel,
                                            bool keyPressed,
                                            uint32_t& newMicLevel) = 0;

    virtual int32_t NeedMorePlayData(size_t nSamples,
                                     size_t nBytesPerSample,
                                     size_t nChannels,
                                     uint32_t samplesPerSec,
                                     void* audioSamples,
                                     size_t& nSamplesOut,
                                     int64_t* elapsed_time_ms,
                                     int64_t* ntp_time_ms) = 0;
};
} // namespace webrtc
#endif
