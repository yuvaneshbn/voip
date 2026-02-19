#include "AudioEngine.h"

#include <QAudioDevice>
#include <QAudioSink>
#include <QAudioSource>
#include <QDebug>
#include <QMediaDevices>
#include <QIODevice>

namespace {
constexpr int kSampleRate = 48000;
constexpr int kChannels = 1;
constexpr int kBytesPerSample = 2;
constexpr int kFrameMs = 20;
}

AudioEngine::AudioEngine(QObject *parent)
    : QObject(parent) {
}

AudioEngine::~AudioEngine() {
    stop();
}

bool AudioEngine::start() {
    stop();

    const QAudioDevice inputDevice = QMediaDevices::defaultAudioInput();
    const QAudioDevice outputDevice = QMediaDevices::defaultAudioOutput();
    if (inputDevice.isNull() || outputDevice.isNull()) {
        return false;
    }

    const QAudioFormat format = makeAudioFormat();
    if (!inputDevice.isFormatSupported(format) || !outputDevice.isFormatSupported(format)) {
        qWarning() << "Required audio format not supported (need 48kHz mono Int16). Input:"
                   << inputDevice.description() << "Output:" << outputDevice.description();
        return false;
    }
    ioFormat_ = format;

    int bytesPerFrame = ioFormat_.bytesPerFrame();
    if (bytesPerFrame <= 0) {
        bytesPerFrame = ioFormat_.channelCount() * kBytesPerSample;
    }
    frameBytes_ = (ioFormat_.sampleRate() * bytesPerFrame * kFrameMs) / 1000;

    output_ = std::make_unique<QAudioSink>(outputDevice, ioFormat_, this);
    if (!output_) {
        return false;
    }

    playbackDevice_ = output_->start();
    if (!playbackDevice_) {
        stop();
        return false;
    }

    ensureCaptureState();
    return true;
}

void AudioEngine::stop() {
    stopCapture();
    playbackDevice_ = nullptr;

    if (output_) {
        output_->stop();
    }
    output_.reset();
}

void AudioEngine::setMuted(bool muted) {
    muted_ = muted;
    ensureCaptureState();
}

void AudioEngine::setTransmitEnabled(bool enabled) {
    transmitEnabled_ = enabled;
    ensureCaptureState();
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
    playbackDevice_->write(pcm16le);
}

void AudioEngine::onCaptureReadyRead() {
    if (!captureDevice_) {
        return;
    }

    captureBuffer_.append(captureDevice_->readAll());
    while (captureBuffer_.size() >= frameBytes_) {
        const QByteArray frame = captureBuffer_.first(frameBytes_);
        captureBuffer_.remove(0, frameBytes_);

        if (shouldTransmit() && outgoingVoiceCallback_) {
            qDebug() << "Captured frame size:" << frame.size();
            outgoingVoiceCallback_(frame);
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
        return;
    }

    QObject::connect(captureDevice_, &QIODevice::readyRead,
                     this, &AudioEngine::onCaptureReadyRead, Qt::UniqueConnection);
}

void AudioEngine::stopCapture() {
    if (captureDevice_) {
        QObject::disconnect(captureDevice_, nullptr, this, nullptr);
    }

    if (input_) {
        input_->stop();
    }

    captureDevice_ = nullptr;
    captureBuffer_.clear();
    input_.reset();
}

