#pragma once

#include <QAudioFormat>
#include <QByteArray>
#include <QObject>

#include <functional>
#include <memory>

QT_BEGIN_NAMESPACE
class QAudioSink;
class QAudioSource;
class QIODevice;
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

    void setOutgoingVoiceCallback(std::function<void(const QByteArray &)> cb);
    void playIncoming(const QByteArray &pcm16le);

private slots:
    void onCaptureReadyRead();

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
    QByteArray captureBuffer_;
    QAudioFormat ioFormat_;

    bool muted_ = false;
    bool transmitEnabled_ = false;
    int frameBytes_ = 0;
};

