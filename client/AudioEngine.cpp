#include "AudioEngine.h"
#include "client/audio/AecProcessor.h"
#include "constants.h"

#include <QAudioDevice>
#include <QAudioSink>
#include <QAudioSource>
#include <QDebug>
#include <QMediaDevices>

#include <algorithm>

namespace {
constexpr int kSampleRate = SAMPLE_RATE;
constexpr int kChannels = 1;
constexpr int kBytesPerSample = 2;
constexpr int kFrameMs = AUDIO_FRAME_MS;
}

AudioEngine::AudioEngine(QObject *parent)
    : QObject(parent) {
    mediaDevices_ = std::make_unique<QMediaDevices>();
    QObject::connect(mediaDevices_.get(), &QMediaDevices::audioInputsChanged,
                     this, &AudioEngine::onAudioDevicesChanged, Qt::UniqueConnection);
    QObject::connect(mediaDevices_.get(), &QMediaDevices::audioOutputsChanged,
                     this, &AudioEngine::onAudioDevicesChanged, Qt::UniqueConnection);

    playbackFlushTimer_.setInterval(10);
    QObject::connect(&playbackFlushTimer_, &QTimer::timeout,
                     this, &AudioEngine::flushPlaybackBuffer);
}

AudioEngine::~AudioEngine() {
    stop();
}

bool AudioEngine::start() {
    stop();

    const QAudioDevice inputDevice = QMediaDevices::defaultAudioInput();
    const QAudioDevice outputDevice = QMediaDevices::defaultAudioOutput();
    if (inputDevice.isNull() || outputDevice.isNull()) {
        running_ = false;
        return false;
    }

    const QAudioFormat format = makeAudioFormat();
    if (!inputDevice.isFormatSupported(format) || !outputDevice.isFormatSupported(format)) {
        qWarning() << "Required audio format not supported (need 48kHz mono Int16). Input:"
                   << inputDevice.description() << "Output:" << outputDevice.description();
        running_ = false;
        return false;
    }
    ioFormat_ = format;

    int bytesPerFrame = ioFormat_.bytesPerFrame();
    if (bytesPerFrame <= 0) {
        bytesPerFrame = ioFormat_.channelCount() * kBytesPerSample;
    }
    frameBytes_ = (ioFormat_.sampleRate() * bytesPerFrame * kFrameMs) / 1000;
    aec_ = std::make_unique<AecProcessor>();
    if (aecEnabled_) {
        const int frameSamples = frameBytes_ / std::max(1, bytesPerFrame);
        aec_->initialize(ioFormat_.sampleRate(), frameSamples);
    }

    output_ = std::make_unique<QAudioSink>(outputDevice, ioFormat_, this);
    if (!output_) {
        running_ = false;
        return false;
    }

    playbackDevice_ = output_->start();
    if (!playbackDevice_) {
        stop();
        return false;
    }

    output_->setVolume(outputGain_);
    playbackBuffer_.clear();
    playbackFlushTimer_.start();

    ensureCaptureState();
    running_ = true;
    return true;
}

void AudioEngine::stop() {
    running_ = false;
    stopCapture();
    playbackFlushTimer_.stop();
    playbackDevice_ = nullptr;
    playbackBuffer_.clear();

    if (output_) {
        output_->stop();
    }
    output_.reset();
    if (aec_) {
        aec_->reset();
    }
}

void AudioEngine::setMuted(bool muted) {
    muted_ = muted;
    ensureCaptureState();
}

void AudioEngine::setTransmitEnabled(bool enabled) {
    transmitEnabled_ = enabled;
    ensureCaptureState();
}

void AudioEngine::setInputGain(float gain) {
    inputGain_ = std::clamp(gain, 0.0f, 1.0f);
    if (input_) {
        input_->setVolume(inputGain_);
    }
}

void AudioEngine::setOutputGain(float gain) {
    outputGain_ = std::clamp(gain, 0.0f, 1.0f);
    if (output_) {
        output_->setVolume(outputGain_);
    }
}

void AudioEngine::setAecEnabled(bool enabled) {
    aecEnabled_ = enabled;
    if (!aec_) {
        return;
    }
    if (!aecEnabled_) {
        aec_->reset();
        return;
    }
    const int bytesPerFrame = std::max(1, ioFormat_.bytesPerFrame());
    const int frameSamples = frameBytes_ / bytesPerFrame;
    if (frameSamples > 0) {
        aec_->initialize(ioFormat_.sampleRate(), frameSamples);
    }
}

void AudioEngine::setOutgoingVoiceCallback(std::function<void(const QByteArray &)> cb) {
    outgoingVoiceCallback_ = std::move(cb);
}

bool AudioEngine::isCaptureActive() const {
    return captureDevice_ != nullptr;
}

void AudioEngine::playIncoming(const QByteArray &pcm16le) {
    if (!playbackDevice_ || pcm16le.isEmpty()) {
        return;
    }

    playbackBuffer_.append(pcm16le);
    if (frameBytes_ > 0 && pcm16le.size() >= frameBytes_) {
        lastPlaybackFrame_ = pcm16le.last(frameBytes_);
    } else {
        lastPlaybackFrame_ = pcm16le;
    }

    // Cap queued playback to avoid runaway latency under burst/loss.
    const int maxQueueBytes = frameBytes_ * 10;
    if (maxQueueBytes > 0 && playbackBuffer_.size() > maxQueueBytes) {
        playbackBuffer_.remove(0, playbackBuffer_.size() - maxQueueBytes);
    }

    flushPlaybackBuffer();
}

void AudioEngine::onCaptureReadyRead() {
    if (!captureDevice_) {
        return;
    }

    captureBuffer_.append(captureDevice_->readAll());
    while (captureBuffer_.size() >= frameBytes_) {
        const QByteArray frame = captureBuffer_.first(frameBytes_);
        captureBuffer_.remove(0, frameBytes_);
        QByteArray processed = frame;
        if (aecEnabled_ && aec_ && aec_->isReady()) {
            processed = aec_->processFrame(frame, lastPlaybackFrame_);
        }

        if (shouldTransmit() && outgoingVoiceCallback_) {
            qDebug() << "Captured frame size:" << processed.size();
            outgoingVoiceCallback_(processed);
        }
    }
}

QAudioFormat AudioEngine::makeAudioFormat() const {
    QAudioFormat format;
    format.setSampleRate(kSampleRate);
    format.setChannelCount(kChannels);
    format.setSampleFormat(QAudioFormat::Int16);
    return format;
}

bool AudioEngine::shouldTransmit() const {
    return transmitEnabled_ && !muted_;
}

void AudioEngine::ensureCaptureState() {
    if (shouldTransmit()) {
        startCaptureIfNeeded();
    } else {
        stopCapture();
    }
}

void AudioEngine::startCaptureIfNeeded() {
    if (captureDevice_) {
        return;
    }

    const QAudioDevice inputDevice = QMediaDevices::defaultAudioInput();
    if (inputDevice.isNull()) {
        return;
    }

    if (!input_) {
        input_ = std::make_unique<QAudioSource>(inputDevice, ioFormat_, this);
    }
    if (!input_) {
        return;
    }

    captureDevice_ = input_->start();
    if (!captureDevice_) {
        qWarning() << "Audio capture failed to start. Input:"
                   << inputDevice.description()
                   << "format:" << ioFormat_;
        input_->stop();
        input_.reset();
        emit captureActiveChanged(false);
        return;
    }

    QObject::connect(captureDevice_, &QIODevice::readyRead,
                     this, &AudioEngine::onCaptureReadyRead, Qt::UniqueConnection);
    input_->setVolume(inputGain_);
    emit captureActiveChanged(true);
}

void AudioEngine::stopCapture() {
    const bool wasActive = (captureDevice_ != nullptr);
    if (captureDevice_) {
        QObject::disconnect(captureDevice_, nullptr, this, nullptr);
    }

    if (input_) {
        input_->stop();
    }

    captureDevice_ = nullptr;
    captureBuffer_.clear();
    input_.reset();
    if (wasActive) {
        emit captureActiveChanged(false);
    }
}

void AudioEngine::flushPlaybackBuffer() {
    if (!playbackDevice_ || playbackBuffer_.isEmpty()) {
        return;
    }

    while (!playbackBuffer_.isEmpty()) {
        const qint64 writable = std::max<qint64>(output_->bytesFree(), 0);
        if (writable <= 0) {
            break;
        }

        const int toWrite = static_cast<int>(std::min<qint64>(writable, playbackBuffer_.size()));
        const qint64 written = playbackDevice_->write(playbackBuffer_.constData(), toWrite);
        if (written <= 0) {
            break;
        }
        playbackBuffer_.remove(0, static_cast<int>(written));
    }
}

void AudioEngine::onAudioDevicesChanged() {
    if (!running_) {
        return;
    }

    const bool wasMuted = muted_;
    const bool wasTransmitEnabled = transmitEnabled_;
    qInfo() << "Audio device topology changed; restarting audio engine.";

    stop();
    if (!start()) {
        qWarning() << "Failed to restart audio after device change.";
        return;
    }

    setMuted(wasMuted);
    setTransmitEnabled(wasTransmitEnabled);
}

