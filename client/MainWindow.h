#pragma once

#include <QMainWindow>
#include <QProgressBar>
#include <QSet>
#include <QTimer>
#include <QVector>

#include <cstdint>
#include <memory>
#include <vector>

#include "constants.h"
#include "shared/protocol/control_protocol.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class QListWidgetItem;
class ControlClient;
class AudioEngine;
class ServerDiscoveryService;

enum class ClientStatus {
    Online,
    InCall,
    Offline
};

struct Client {
    QString id;
    QString name;
    uint32_t ssrc = 0;
    bool isSelf = false;
    bool online = false;
    ClientStatus status = ClientStatus::Offline;
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(const QString& serverIp,
                        const QString& clientName,
                        QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onSearchChanged(const QString &text);
    void onClientItemChanged(QListWidgetItem *item);
    void onClientDoubleClicked(QListWidgetItem *item);

    void startGroupCall();
    void startBroadcastFromSidebar();
    void startBroadcastFromStage();
    void endCall();

    void startPushToTalk();
    void stopPushToTalk();

    void onMuteToggled(bool checked);
    void onSelectiveHearingToggled(bool checked);
    void applySelectiveHearingFromSelection();
    void onLayerPreferenceChanged(int index);

    void pollServerStatus();
    void refreshUserList();
    void tickVisualizer();

private:
    Ui::MainWindow *ui;

    QString serverIp_;
    QString clientName_;
    uint32_t localSsrc_ = 0;

    std::unique_ptr<ControlClient> control_;
    std::unique_ptr<AudioEngine> audio_;
    std::unique_ptr<ServerDiscoveryService> discovery_;

    QVector<Client> clients_;
    QSet<QString> selectedClientIds_;
    QSet<uint32_t> occupiedTargets_;
    QSet<uint32_t> hearingTargets_;
    QVector<QString> participants_;

    QString callType_;
    bool serverConnected_ = false;
    bool isMuted_ = false;
    bool selectiveHearingEnabled_ = false;
    QString preferredLayer_ = QStringLiteral("auto");
    bool isPushToTalkPressed_ = false;
    std::vector<int> pingSamplesMs_;
    int pingFailureStreak_ = 0;
    int badCounter_ = 0;
    int qualityStreak_ = 0;
    QString observedQuality_ = QStringLiteral("Average");
    QString networkQuality_ = QStringLiteral("Average");
    int discoveredServerCount_ = 0;

    QTimer statusTimer_;
    QTimer listTimer_;
    QTimer visualizerTimer_;

    QVector<QProgressBar *> visualizerBars_;

    void refreshClientList();
    void updateStats();
    void updateGroupButtonState();

    void updateHeader();
    void updateCallStage();
    void updateAudioControls();
    void updateSelectiveHearingState();

    Client *findClientById(const QString &id);
    void applyOccupiedTargets(const QVector<Client>& selectedOnline);
    void rebuildClientsFromControl(const std::vector<CtrlUserInfo>& users);
    bool acceptsVoiceFrom(uint32_t ssrc) const;
    QString networkQualityText() const;
    void updateNetworkQualityFromPing(bool ok, int pingMs);

    static QString statusText(ClientStatus status);
    static QString statusDot(ClientStatus status);
};

