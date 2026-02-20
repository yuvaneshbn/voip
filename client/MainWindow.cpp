#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "AudioEngine.h"
#include "control_client.h"

#include <QCheckBox>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMetaObject>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QStatusBar>

namespace {
constexpr int kRoleClientId = Qt::UserRole + 1;
constexpr int kRoleParticipantName = Qt::UserRole + 3;
constexpr size_t kMaxPingSamples = 10;
}

MainWindow::MainWindow(const QString& serverIp,
                       const QString& clientName,
                       QWidget *parent)
    : QMainWindow(parent),
      ui(new Ui::MainWindow),
      serverIp_(serverIp.trimmed().isEmpty() ? QStringLiteral("127.0.0.1") : serverIp.trimmed()),
      clientName_(clientName.trimmed().isEmpty() ? QStringLiteral("Client 1") : clientName.trimmed()) {
    ui->setupUi(this);

    setWindowTitle("Dynamic VoIP UI (Qt Conversion)");
    resize(1280, 800);

    setStyleSheet(
        "QMainWindow { background: #09090b; color: #d4d4d8; }"
        "QFrame#headerFrame, QFrame#bottomFrame, QWidget#leftPanel { background: #18181b; }"
        "QLineEdit, QListWidget, QSlider, QPushButton, QCheckBox { font-size: 12px; }"
        "QLineEdit { background: #27272a; border: 1px solid #3f3f46; border-radius: 6px; padding: 6px; color: #e4e4e7; }"
        "QListWidget { background: #09090b; border: 1px solid #27272a; border-radius: 6px; }"
        "QPushButton { background: #27272a; color: #e4e4e7; border: 1px solid #3f3f46; border-radius: 6px; padding: 6px 10px; }"
        "QPushButton:hover { background: #3f3f46; }"
        "QPushButton:disabled { color: #71717a; border-color: #27272a; }"
        "QPushButton#btnEndCall { background: #7f1d1d; border-color: #991b1b; }"
        "QPushButton#btnPushToTalk:pressed { background: #14532d; }"
    );

    visualizerBars_ = {
        ui->bar1, ui->bar2, ui->bar3, ui->bar4,
        ui->bar5, ui->bar6, ui->bar7, ui->bar8,
    };

    for (QProgressBar *bar : visualizerBars_) {
        bar->setRange(0, 100);
        bar->setValue(0);
        bar->setTextVisible(false);
    }

    ui->sliderMic->setRange(0, 100);
    ui->sliderSpeaker->setRange(0, 100);
    ui->sliderMic->setValue(75);
    ui->sliderSpeaker->setValue(80);
    ui->lblMicValue->setText("75%");
    ui->lblSpeakerValue->setText("80%");
    ui->lblUsername->setText(clientName_);

    connect(ui->editSearch, &QLineEdit::textChanged, this, &MainWindow::onSearchChanged);
    connect(ui->listClients, &QListWidget::itemChanged, this, &MainWindow::onClientItemChanged);
    connect(ui->listClients, &QListWidget::itemDoubleClicked, this, &MainWindow::onClientDoubleClicked);

    connect(ui->btnGroupCall, &QPushButton::clicked, this, &MainWindow::startGroupCall);
    connect(ui->btnBroadcastAll, &QPushButton::clicked, this, &MainWindow::startBroadcastFromSidebar);
    connect(ui->btnStageBroadcast, &QPushButton::clicked, this, &MainWindow::startBroadcastFromStage);
    connect(ui->btnEndCall, &QPushButton::clicked, this, &MainWindow::endCall);

    connect(ui->btnPushToTalk, &QPushButton::pressed, this, &MainWindow::startPushToTalk);
    connect(ui->btnPushToTalk, &QPushButton::released, this, &MainWindow::stopPushToTalk);

    connect(ui->chkMicActive, &QCheckBox::toggled, this, &MainWindow::onMuteToggled);

    connect(ui->sliderMic, &QSlider::valueChanged, this, [this](int value) {
        ui->lblMicValue->setText(QString::number(value) + "%");
    });
    connect(ui->sliderSpeaker, &QSlider::valueChanged, this, [this](int value) {
        ui->lblSpeakerValue->setText(QString::number(value) + "%");
    });

    localSsrc_ = static_cast<uint32_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());

    control_ = std::make_unique<ControlClient>();
    if (!control_->initialize(serverIp_.toStdString(), DEFAULT_CONTROL_PORT)) {
        statusBar()->showMessage("Failed to initialize control client");
    } else {
        control_->set_user_list_callback([this](const std::vector<CtrlUserInfo>& users) {
            QMetaObject::invokeMethod(this, [this, users]() {
                rebuildClientsFromControl(users);
                refreshClientList();
                updateStats();
                updateGroupButtonState();
                updateCallStage();
            }, Qt::QueuedConnection);
        });

        control_->set_voice_callback([this](uint32_t, const QByteArray &pcm) {
            QMetaObject::invokeMethod(this, [this, pcm]() {
                if (audio_) {
                    audio_->playIncoming(pcm);
                }
            }, Qt::QueuedConnection);
        });

        control_->start();
        control_->join(localSsrc_, clientName_.toStdString());
        control_->talk(localSsrc_, {});
        control_->request_user_list();
        serverConnected_ = true;
    }

    audio_ = std::make_unique<AudioEngine>(this);
    audio_->setOutgoingVoiceCallback([this](const QByteArray &pcm) {
        if (control_ && localSsrc_ != 0) {
            control_->send_voice(localSsrc_, pcm);
        }
    });
    if (!audio_->start()) {
        statusBar()->showMessage("Audio device init failed (check mic/speaker permissions/devices)", 5000);
    }

    statusTimer_.setInterval(2000);
    listTimer_.setInterval(1500);
    visualizerTimer_.setInterval(120);

    connect(&statusTimer_, &QTimer::timeout, this, &MainWindow::pollServerStatus);
    connect(&listTimer_, &QTimer::timeout, this, &MainWindow::refreshUserList);
    connect(&visualizerTimer_, &QTimer::timeout, this, &MainWindow::tickVisualizer);

    statusTimer_.start();
    listTimer_.start();
    visualizerTimer_.start();

    refreshClientList();
    updateStats();
    updateGroupButtonState();
    updateHeader();
    updateCallStage();
    updateAudioControls();

    statusBar()->showMessage(QString("Connected to %1 as %2").arg(serverIp_, clientName_));
}

MainWindow::~MainWindow() {
    if (audio_) {
        audio_->stop();
    }
    if (control_) {
        if (localSsrc_ != 0) {
            control_->leave(localSsrc_);
        }
        control_->stop();
    }
    delete ui;
}

QString MainWindow::statusText(ClientStatus status) {
    switch (status) {
    case ClientStatus::Online:
        return "Online";
    case ClientStatus::InCall:
        return "Talking";
    case ClientStatus::Offline:
        return "Offline";
    }
    return "Unknown";
}

QString MainWindow::statusDot(ClientStatus status) {
    switch (status) {
    case ClientStatus::Online:
        return "#22c55e";
    case ClientStatus::InCall:
        return "#eab308";
    case ClientStatus::Offline:
        return "#52525b";
    }
    return "#52525b";
}

Client *MainWindow::findClientById(const QString &id) {
    for (Client &client : clients_) {
        if (client.id == id) {
            return &client;
        }
    }
    return nullptr;
}

void MainWindow::rebuildClientsFromControl(const std::vector<CtrlUserInfo>& users) {
    QVector<Client> next;
    next.reserve(static_cast<int>(users.size()));

    for (const CtrlUserInfo& u : users) {
        Client c;
        c.ssrc = u.ssrc;
        c.id = QString::number(u.ssrc);
        c.isSelf = (u.ssrc == localSsrc_);
        c.name = c.isSelf ? QString("%1 (You)").arg(clientName_) : QString::fromLocal8Bit(u.name);
        c.online = u.online != 0;

        if (!c.online) {
            c.status = ClientStatus::Offline;
        } else if (occupiedTargets_.contains(c.ssrc)) {
            c.status = ClientStatus::InCall;
        } else {
            c.status = ClientStatus::Online;
        }

        next.push_back(c);
    }

    clients_ = std::move(next);
}

void MainWindow::refreshClientList() {
    const QString q = ui->editSearch->text().trimmed();
    const QString query = q.toLower();

    QSignalBlocker block(ui->listClients);
    ui->listClients->clear();

    std::vector<Client> visible;
    visible.reserve(static_cast<size_t>(clients_.size()));

    auto matchesSearch = [&query](const Client &client) {
        if (query.isEmpty()) {
            return true;
        }

        const QString name = client.name.toLower();
        const QString id = client.id.toLower();
        const QString status = statusText(client.status).toLower();
        return name.contains(query) || id.contains(query) || status.contains(query);
    };

    for (const Client &client : clients_) {
        if (!matchesSearch(client)) {
            continue;
        }
        visible.push_back(client);
    }

    std::sort(visible.begin(), visible.end(), [](const Client &a, const Client &b) {
        if (a.status != b.status) {
            const int aRank = (a.status == ClientStatus::Online) ? 0 : (a.status == ClientStatus::InCall ? 1 : 2);
            const int bRank = (b.status == ClientStatus::Online) ? 0 : (b.status == ClientStatus::InCall ? 1 : 2);
            return aRank < bRank;
        }
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });

    for (const Client &client : visible) {

        auto *item = new QListWidgetItem(QString("%1  (%2)").arg(client.name, statusText(client.status)));
        item->setData(kRoleClientId, client.id);

        Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
        if (client.status == ClientStatus::Online && !client.isSelf) {
            flags |= Qt::ItemIsUserCheckable;
            item->setCheckState(selectedClientIds_.contains(client.id) ? Qt::Checked : Qt::Unchecked);
        } else {
            item->setCheckState(Qt::Unchecked);
        }
        item->setFlags(flags);

        item->setForeground(QColor(statusDot(client.status)));
        ui->listClients->addItem(item);
    }
}

void MainWindow::updateStats() {
    const QString q = ui->editSearch->text().trimmed();

    int online = 0;
    int inCall = 0;
    int offline = 0;

    for (const Client &client : clients_) {
        if (!q.isEmpty() && !client.name.contains(q, Qt::CaseInsensitive)) {
            continue;
        }

        if (client.status == ClientStatus::Online) {
            ++online;
        } else if (client.status == ClientStatus::InCall) {
            ++inCall;
        } else {
            ++offline;
        }
    }

    ui->lblOnlineCount->setText(QString("%1 online").arg(online));
    ui->lblInCallCount->setText(QString("%1 talking").arg(inCall));
    ui->lblOfflineCount->setText(QString("%1 offline").arg(offline));
}

void MainWindow::updateGroupButtonState() {
    int selectedOnline = 0;
    for (const QString &id : selectedClientIds_) {
        const Client *client = findClientById(id);
        if (client && client->status == ClientStatus::Online && !client->isSelf) {
            ++selectedOnline;
        }
    }

    ui->btnGroupCall->setEnabled(selectedOnline >= 1);
    ui->btnGroupCall->setText(selectedOnline >= 1
                                  ? QString("Talk to Selected (%1)").arg(selectedOnline)
                                  : "Select client(s) to talk");
}

void MainWindow::onSearchChanged(const QString &) {
    refreshClientList();
    updateStats();
}

void MainWindow::onClientItemChanged(QListWidgetItem *item) {
    if (!item) {
        return;
    }

    const QString id = item->data(kRoleClientId).toString();
    if (id.isEmpty()) {
        return;
    }

    if (item->checkState() == Qt::Checked) {
        selectedClientIds_.insert(id);
    } else {
        selectedClientIds_.remove(id);
    }

    updateGroupButtonState();
}

void MainWindow::onClientDoubleClicked(QListWidgetItem *item) {
    if (!item) {
        return;
    }

    const QString id = item->data(kRoleClientId).toString();
    Client *client = findClientById(id);
    if (!client) {
        return;
    }

    if (client->isSelf) {
        statusBar()->showMessage("You are already online", 2000);
        return;
    }

    if (client->status != ClientStatus::Online) {
        statusBar()->showMessage(QString("Cannot talk to %1: not online").arg(client->name), 3000);
        return;
    }

    applyOccupiedTargets({*client});
    statusBar()->showMessage(QString("Talking to %1").arg(client->name), 3000);
}

void MainWindow::applyOccupiedTargets(const QVector<Client>& selectedOnline) {
    if (!control_) {
        return;
    }

    std::vector<uint32_t> targets;
    targets.reserve(selectedOnline.size());

    occupiedTargets_.clear();
    participants_.clear();
    participants_.push_back(clientName_);

    for (const Client& c : selectedOnline) {
        targets.push_back(c.ssrc);
        occupiedTargets_.insert(c.ssrc);
        participants_.push_back(c.name);
    }

    control_->talk(localSsrc_, targets);
    if (audio_) {
        audio_->setMuted(isMuted_);
        audio_->setTransmitEnabled(!isMuted_);
    }

    callType_ = QStringLiteral("talk");
    selectedClientIds_.clear();

    for (Client& c : clients_) {
        if (!c.online) {
            c.status = ClientStatus::Offline;
        } else if (occupiedTargets_.contains(c.ssrc)) {
            c.status = ClientStatus::InCall;
        } else {
            c.status = ClientStatus::Online;
        }
    }

    refreshClientList();
    updateStats();
    updateGroupButtonState();
    updateCallStage();
    updateAudioControls();
}

void MainWindow::startGroupCall() {
    QVector<Client> selectedOnline;

    for (const QString &id : selectedClientIds_) {
        Client *client = findClientById(id);
        if (client && client->status == ClientStatus::Online && !client->isSelf) {
            selectedOnline.push_back(*client);
        }
    }

    if (selectedOnline.isEmpty()) {
        statusBar()->showMessage("Select at least one online client to talk", 3000);
        return;
    }

    applyOccupiedTargets(selectedOnline);
    statusBar()->showMessage(QString("Talking to %1 client(s)").arg(selectedOnline.size()), 3000);
}

void MainWindow::startBroadcastFromSidebar() {
    startBroadcastFromStage();
}

void MainWindow::startBroadcastFromStage() {
    if (!control_) {
        return;
    }

    occupiedTargets_.clear();
    control_->talk(localSsrc_, {});
    if (audio_) {
        audio_->setMuted(isMuted_);
        audio_->setTransmitEnabled(false);
    }

    callType_ = QStringLiteral("broadcast");
    participants_.clear();
    participants_.push_back(clientName_);

    for (const Client &client : clients_) {
        if (!client.isSelf && client.status != ClientStatus::Offline) {
            participants_.push_back(client.name);
        }
    }

    selectedClientIds_.clear();
    isPushToTalkPressed_ = false;

    for (Client& c : clients_) {
        c.status = c.online ? ClientStatus::Online : ClientStatus::Offline;
    }

    refreshClientList();
    updateStats();
    updateGroupButtonState();
    updateCallStage();
    updateAudioControls();

    statusBar()->showMessage("Broadcast enabled to all online clients", 3000);
}

void MainWindow::endCall() {
    if (control_) {
        control_->talk(localSsrc_, {});
    }

    occupiedTargets_.clear();
    participants_.clear();
    callType_.clear();
    isPushToTalkPressed_ = false;

    for (Client& c : clients_) {
        c.status = c.online ? ClientStatus::Online : ClientStatus::Offline;
    }

    refreshClientList();
    updateStats();
    updateCallStage();
    updateAudioControls();
    statusBar()->showMessage("Communication route cleared", 3000);
}

void MainWindow::startPushToTalk() {
    if (callType_ != "broadcast") {
        return;
    }

    isPushToTalkPressed_ = true;
    if (audio_) {
        audio_->setMuted(isMuted_);
        audio_->setTransmitEnabled(!isMuted_);
    }
    statusBar()->showMessage("Speaking to broadcast targets", 1000);
    updateCallStage();
    updateAudioControls();
}

void MainWindow::stopPushToTalk() {
    if (callType_ != "broadcast") {
        return;
    }

    isPushToTalkPressed_ = false;
    if (audio_) {
        audio_->setTransmitEnabled(false);
    }
    statusBar()->showMessage("Stopped speaking", 1000);
    updateCallStage();
    updateAudioControls();
}

void MainWindow::onMuteToggled(bool checked) {
    isMuted_ = !checked;
    if (audio_) {
        audio_->setMuted(isMuted_);
    }
    updateHeader();
    updateAudioControls();
}

void MainWindow::pollServerStatus() {
    if (!control_) {
        ++pingFailureStreak_;
        if (pingFailureStreak_ >= 3) {
            serverConnected_ = false;
            networkQuality_ = QStringLiteral("Bad");
        }
        updateHeader();
        return;
    }

    auto t0 = std::chrono::steady_clock::now();
    const bool ok = control_->ping_server(300);
    auto t1 = std::chrono::steady_clock::now();

    const int pingMs = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());
    updateNetworkQualityFromPing(ok, pingMs);
    updateHeader();
}

void MainWindow::refreshUserList() {
    if (control_ && serverConnected_) {
        control_->request_user_list();
    }
}

void MainWindow::tickVisualizer() {
    const bool active = !participants_.isEmpty() && !isMuted_;
    const bool speaking = isPushToTalkPressed_ || (active && callType_ == "talk");

    int base = speaking ? 70 : (active ? 35 : 0);
    for (int i = 0; i < visualizerBars_.size(); ++i) {
        QProgressBar* bar = visualizerBars_[i];
        int v = base > 0 ? (base + ((i * 9) % 25)) : 0;
        if (v > 100) {
            v = 100;
        }
        bar->setValue(v);
    }
}

void MainWindow::updateHeader() {
    ui->lblServerState->setText(serverConnected_ ? "Connected" : "Disconnected");
    ui->lblLatencyTitle->setText("Network");
    const QString quality = networkQualityText();
    ui->lblLatencyValue->setText(quality);
    if (quality == "Good") {
        ui->lblLatencyValue->setStyleSheet("color: #22c55e;");
    } else if (quality == "Average") {
        ui->lblLatencyValue->setStyleSheet("color: #eab308;");
    } else {
        ui->lblLatencyValue->setStyleSheet("color: #ef4444;");
    }
    ui->lblMuteState->setText(isMuted_ ? "Muted" : "Active");

    const QString dotColor = serverConnected_ ? "#22c55e" : "#ef4444";
    ui->lblServerDot->setStyleSheet(
        QString("background: %1; border-radius: 6px; min-width: 12px; min-height: 12px;").arg(dotColor));

    const QSignalBlocker block(ui->chkMicActive);
    ui->chkMicActive->setChecked(!isMuted_);
}

QString MainWindow::networkQualityText() const {
    return networkQuality_;
}

void MainWindow::updateNetworkQualityFromPing(bool ok, int pingMs) {
    if (ok) {
        pingFailureStreak_ = 0;
        serverConnected_ = true;

        // Keep rolling history to smooth short spikes.
        pingSamplesMs_.push_back(pingMs);
        if (pingSamplesMs_.size() > kMaxPingSamples) {
            pingSamplesMs_.erase(pingSamplesMs_.begin());
        }

        if (pingSamplesMs_.empty()) {
            return;
        }

        long long total = 0;
        for (int v : pingSamplesMs_) {
            total += v;
        }
        const int avg = static_cast<int>(total / static_cast<long long>(pingSamplesMs_.size()));

        QString candidate = QStringLiteral("Bad");
        if (avg <= 60) {
            candidate = QStringLiteral("Good");
        } else if (avg <= 150) {
            candidate = QStringLiteral("Average");
        }

        if (candidate == QStringLiteral("Bad")) {
            ++badCounter_;
        } else {
            badCounter_ = 0;
        }

        if (candidate == observedQuality_) {
            ++qualityStreak_;
        } else {
            observedQuality_ = candidate;
            qualityStreak_ = 1;
        }

        // Hysteresis: only switch after repeated observations.
        if (badCounter_ >= 3) {
            networkQuality_ = QStringLiteral("Bad");
        } else if (qualityStreak_ >= 3) {
            networkQuality_ = candidate;
        }
        return;
    }

    ++pingFailureStreak_;
    if (pingFailureStreak_ >= 3) {
        serverConnected_ = false;
        networkQuality_ = QStringLiteral("Bad");
    }
}

void MainWindow::updateCallStage() {
    const bool inCall = !participants_.isEmpty();
    ui->callStack->setCurrentWidget(inCall ? ui->pageActive : ui->pageIdle);

    if (!inCall) {
        ui->lblLiveBroadcast->setVisible(false);
        return;
    }

    ui->lblCallType->setText(callType_.isEmpty() ? "Call" : callType_.toUpper() + " MODE");
    ui->lblParticipantCount->setText(QString("%1 participant(s)").arg(participants_.size()));
    ui->lblLiveBroadcast->setVisible(callType_ == "broadcast");

    ui->listParticipants->clear();
    for (int i = 0; i < participants_.size(); ++i) {
        QString display = participants_[i];
        if (i == 0 && isPushToTalkPressed_) {
            display += " (speaking)";
        }

        auto *item = new QListWidgetItem(display);
        item->setData(kRoleParticipantName, participants_[i]);
        item->setForeground((i == 0 && isPushToTalkPressed_) ? QColor("#22c55e") : QColor("#d4d4d8"));
        ui->listParticipants->addItem(item);
    }
}

void MainWindow::updateAudioControls() {
    const bool inCall = !participants_.isEmpty();
    ui->btnEndCall->setEnabled(inCall);

    const bool broadcast = (callType_ == "broadcast");
    ui->btnPushToTalk->setEnabled(broadcast && !isMuted_);
    ui->lblPushHint->setVisible(broadcast);

    if (audio_) {
        const bool txEnabled = inCall && (!broadcast || isPushToTalkPressed_);
        audio_->setMuted(isMuted_);
        audio_->setTransmitEnabled(txEnabled);

        if (txEnabled && !isMuted_ && !audio_->isCaptureActive()) {
            ui->lblMicActivity->setText("Mic Error");
        } else {
            ui->lblMicActivity->setText((txEnabled && !isMuted_) ? "Mic Active" : "Mic Idle");
        }
    } else {
        ui->lblMicActivity->setText("Mic Idle");
    }
}

