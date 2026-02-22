#pragma once

#include <QMainWindow>
#include <QProgressBar>
#include <QSet>
#include <QTimer>
#include <QVector>

#include <atomic>
#include <cstdint>
#include <deque>
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
class AudioProcessor;
class NetworkEngine;

enum class ClientStatus {
    Online,
    InCall,
    Offline
};

struct Client {
    QString id;
    QString name;
    uint32_t ssrc = 0;
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

    void pollServerStatus();
    void refreshUserList();
    void tickVisualizer();

private:
    Ui::MainWindow *ui;

    QString serverIp_;
    QString clientName_;
    QString localIp_;
    uint32_t localSsrc_ = 0;

    std::unique_ptr<ControlClient> control_;
    std::unique_ptr<AudioProcessor> audioEngine_;
    std::unique_ptr<NetworkEngine> networkEngine_;
    bool mediaConnected_ = false;
    std::atomic<bool> mutedAtomic_{false};
    std::atomic<bool> broadcastMode_{false};
    std::atomic<bool> pushToTalkAtomic_{false};
    std::atomic<bool> callActiveAtomic_{false};

    QVector<Client> clients_;
    QSet<QString> selectedClientIds_;
    QSet<uint32_t> occupiedTargets_;
    QVector<QString> participants_;

    QString callType_;
    bool serverConnected_ = false;
    bool isMuted_ = false;
    bool isPushToTalkPressed_ = false;
    uint32_t incomingFromSsrc_ = 0;
    int latencyMs_ = 0;
    int smoothedStrength_ = 0;
    std::deque<int> strengthSamples_;

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

    Client *findClientById(const QString &id);
    void applyOccupiedTargets(const QVector<Client>& selectedOnline);
    void rebuildClientsFromControl(const std::vector<CtrlUserInfo>& users);
    void handleTalkUpdate(uint32_t from, const std::vector<uint32_t>& targets);
    QString displayNameForSsrc(uint32_t ssrc) const;

    static QString statusText(ClientStatus status);
    static QString statusDot(ClientStatus status);

    void initMediaEngine();
};
