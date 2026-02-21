#pragma once

#include <QHash>
#include <QMap>
#include <QTcpSocket>
#include <QTimer>
#include <QUdpSocket>
#include <QVector>


#include "OpusCodec.h"
#include "shared/protocol/control_protocol.h"

class ControlClient : public QObject {
    Q_OBJECT

public:
    explicit ControlClient(QObject *parent = nullptr);

    bool initialize(const std::string &serverIp, quint16 port);
    void start();
    void stop();
    QString server_ip() const;
    uint32_t assigned_client_id() const;

    bool ping_server(int timeoutMs);
    void update_rtt_estimate(int rttMs);

    void join(uint32_t ssrc, const std::string &name);
    void leave(uint32_t ssrc);
    void talk(uint32_t ssrc, const std::vector<uint32_t> &targets);
    void set_receive_policy(uint32_t ssrc, const std::vector<uint32_t> &sources, int maxStreams, bool filterEnabled, const QString &preferredLayer);
    void send_voice(uint32_t ssrc, const QByteArray &pcm16le);
    void request_user_list();

    void set_user_list_callback(std::function<void(const std::vector<CtrlUserInfo> &)> cb);
    void set_voice_callback(std::function<void(uint32_t, const QByteArray &)> cb);
    void set_client_label(const QString &name);

signals:
    void pongReceived(quint64 pingId);

private slots:
    void onUdpReadyRead();
    void onControlReadyRead();
    void onControlConnected();
    void onControlDisconnected();

private:
    struct QueuedVoiceFrame {
        uint8_t flags = 0;
        uint32_t timestampMs = 0;
        qint64 arrivalMs = 0;
        QByteArray payload;
    };

    struct VoiceJitterState {
        bool initialized = false;
        uint8_t activeLayer = ctrlproto::kVoiceLayerHigh;
        uint16_t expectedSeq = 0;
        bool playoutAnchored = false;
        uint32_t anchorRemoteTsMs = 0;
        qint64 anchorLocalMs = 0;
        uint32_t nextExpectedTsMs = 0;
        bool havePrevTiming = false;
        uint32_t prevRemoteTsMs = 0;
        qint64 prevArrivalMs = 0;
        double jitterMs = 0.0;
        int expectedFramesWindow = 0;
        int receivedFramesWindow = 0;
        int fecRecoveredFramesWindow = 0;
        int plcFramesWindow = 0;
        QMap<uint16_t, QueuedVoiceFrame> pendingFrames;
    };

    void handleIncomingVoice(const ctrlproto::VoicePacket &packet);
    void flushJitterBuffer(uint32_t ssrc, VoiceJitterState &state, qint64 nowMs);
    void onPlayoutTick();
    void onFeedbackTick();
    void sendVoiceFeedback(uint32_t sourceSsrc, int lossPct, int jitterMs, int plcPct, int fecPct);
    void applyAdaptiveBitrateFromFeedback(int lossPct, int rttMs, int jitterMs, int plcPct, int fecPct);
    bool ensureControlConnected(int timeoutMs);
    void flushPendingControlWrites();
    void sendPacket(const QJsonObject &obj);

    QUdpSocket mediaSocket_;
    QTcpSocket controlSocket_;
    QByteArray controlReadBuffer_;
    QVector<QByteArray> pendingControlWrites_;
    QHostAddress serverAddress_;
    quint16 serverPort_ = 0;
    bool discoveryMode_ = false;
    bool joiningSent_ = false;
    std::function<void(const std::vector<CtrlUserInfo> &)> userListCallback_;
    std::function<void(uint32_t, const QByteArray &)> voiceCallback_;
    quint64 nextPingId_ = 1;
    uint16_t nextVoiceSeqLow_ = 1;
    uint16_t nextVoiceSeqHigh_ = 1;
    QHash<uint32_t, VoiceJitterState> jitterBySsrc_;
    OpusCodec opusCodec_;
    QTimer playoutTimer_;
    QTimer feedbackTimer_;
    uint32_t localSsrc_ = 0;
    uint32_t assignedClientId_ = 0;
    QString clientLabel_ = QStringLiteral("nox-client");
    int rttEstimateMs_ = 80;
    int currentTargetBitrate_ = 32000;
    double feedbackLossEwma_ = 0.0;
    double feedbackRttEwma_ = 80.0;
    double feedbackJitterEwma_ = 10.0;
    double feedbackPlcEwma_ = 0.0;
    double feedbackFecEwma_ = 0.0;
    qint64 lastBitrateAdjustMs_ = 0;
};

