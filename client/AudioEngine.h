#pragma once

#include <QAudioFormat>
#include <QByteArray>
#include <QObject>
#include <QTimer>

#include <functional>
#include <memory>

class AecProcessor;

QT_BEGIN_NAMESPACE
class QAudioSink;
class QAudioSource;
class QIODevice;
class QMediaDevices;
QT_END_NAMESPACE

class AudioEngine : public QObject {
    Q_OBJECT

public:
    explicit AudioEngine(QObject *parent = nullptr);
    ~AudioEngine() override;

    bool start();
    void stop();
    bool isCaptureActive() const;

    void setMuted(bool muted);
    void setTransmitEnabled(bool enabled);
    void setInputGain(float gain);
    void setOutputGain(float gain);
    void setAecEnabled(bool enabled);

    void setOutgoingVoiceCallback(std::function<void(const QByteArray &)> cb);
    void playIncoming(const QByteArray &pcm16le);

signals:
    void captureActiveChanged(bool active);

private slots:
    void onCaptureReadyRead();
    void flushPlaybackBuffer();
    void onAudioDevicesChanged();

private:
    QAudioFormat makeAudioFormat() const;
    bool shouldTransmit() const;
    void ensureCaptureState();
    void startCaptureIfNeeded();
    void stopCapture();

    std::function<void(const QByteArray &)> outgoingVoiceCallback_;
    std::unique_ptr<QAudioSource> input_;
    std::unique_ptr<QAudioSink> output_;
    QIODevice *captureDevice_ = nullptr;
    QIODevice *playbackDevice_ = nullptr;
    std::unique_ptr<QMediaDevices> mediaDevices_;
    QByteArray captureBuffer_;
    QByteArray playbackBuffer_;
    QByteArray lastPlaybackFrame_;
    QAudioFormat ioFormat_;
    QTimer playbackFlushTimer_;
    std::unique_ptr<AecProcessor> aec_;
    bool aecEnabled_ = true;

    bool muted_ = false;
    bool transmitEnabled_ = false;
    float inputGain_ = 1.0f;
    float outputGain_ = 1.0f;
    int frameBytes_ = 0;
    bool running_ = false;
};

