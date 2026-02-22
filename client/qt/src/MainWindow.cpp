#include "MainWindow.h"

#include "ui_MainWindow.h"

#include <QColor>
#include <QListWidgetItem>
#include <QSignalBlocker>
#include <QMetaObject>
#include <QUdpSocket>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QNetworkInterface>

#include <chrono>
#include <cstdlib>

#include "AudioProcessor.h"
#include "control_client.h"
#include "network.h"

namespace {
constexpr int kRoleClientId = Qt::UserRole + 1;
constexpr int kRoleParticipantName = Qt::UserRole + 3;

int strengthBucket(bool connected, int latencyMs) {
    if (!connected) {
        return 0;
    }
    if (latencyMs <= 80) {
        return 2;
    }
    if (latencyMs <= 180) {
        return 1;
    }
    return 0;
}

bool matchesSearch(const Client& client, const QString& query) {
    const QString q = query.trimmed();
    if (q.isEmpty()) {
        return true;
    }

    QString status;
    switch (client.status) {
    case ClientStatus::Online:
        status = QStringLiteral("online");
        break;
    case ClientStatus::InCall:
        status = QStringLiteral("occupied");
        break;
    case ClientStatus::Offline:
        status = QStringLiteral("offline");
        break;
    }

    return client.name.contains(q, Qt::CaseInsensitive)
        || client.id.contains(q, Qt::CaseInsensitive)
        || status.contains(q, Qt::CaseInsensitive);
}

QString detectLocalIpv4ForServer(const QString& serverIp) {
    // Preferred path: ask OS routing table which local address reaches serverIp.
    QHostAddress serverAddr;
    if (serverAddr.setAddress(serverIp) && serverAddr.protocol() == QAbstractSocket::IPv4Protocol) {
        QUdpSocket sock;
        if (sock.bind(QHostAddress::AnyIPv4, 0)) {
            sock.connectToHost(serverAddr, DEFAULT_CONTROL_PORT);
            if (sock.waitForConnected(200)) {
                const QHostAddress local = sock.localAddress();
                if (local.protocol() == QAbstractSocket::IPv4Protocol && !local.isNull()) {
                    return local.toString();
                }
            }
        }
    }

    // Fallback: first active non-loopback IPv4.
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& iface : interfaces) {
        const auto flags = iface.flags();
        if (!(flags & QNetworkInterface::IsUp) ||
            !(flags & QNetworkInterface::IsRunning) ||
            (flags & QNetworkInterface::IsLoopBack)) {
            continue;
        }

        const auto entries = iface.addressEntries();
        for (const QNetworkAddressEntry& entry : entries) {
            const QHostAddress ip = entry.ip();
            if (ip.protocol() == QAbstractSocket::IPv4Protocol && !ip.isNull()) {
                return ip.toString();
            }
        }
    }

    return QStringLiteral("127.0.0.1");
}
}

MainWindow::MainWindow(const QString& serverIp,
                       const QString& clientName,
                       QWidget *parent)
    : QMainWindow(parent),
      ui(new Ui::MainWindow),
      serverIp_(serverIp.trimmed().isEmpty() ? QStringLiteral("127.0.0.1") : serverIp.trimmed()),
      clientName_(clientName.trimmed().isEmpty() ? QStringLiteral("Client 1") : clientName.trimmed()),
      localIp_(detectLocalIpv4ForServer(serverIp.trimmed().isEmpty() ? QStringLiteral("127.0.0.1") : serverIp.trimmed())) {
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
    ui->lblUsername->setText(QString("%1  [Local: %2]").arg(clientName_, localIp_));

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

    initMediaEngine();

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
        control_->set_talk_update_callback([this](uint32_t from, const std::vector<uint32_t>& targets) {
            QMetaObject::invokeMethod(this, [this, from, targets]() {
                handleTalkUpdate(from, targets);
            }, Qt::QueuedConnection);
        });

        control_->start();
        control_->join(localSsrc_, clientName_.toStdString());
        control_->ping_server(1000);
        control_->talk(localSsrc_, {});
        control_->request_user_list();
        serverConnected_ = control_->get_last_pong_age_ms() <= 5000;
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

    statusBar()->showMessage(QString("Server: %1 | Local: %2 | User: %3").arg(serverIp_, localIp_, clientName_));
}

MainWindow::~MainWindow() {
    if (audioEngine_) {
        audioEngine_->stop();
    }
    if (networkEngine_) {
        networkEngine_->stop();
    }
    if (control_) {
        if (localSsrc_ != 0) {
            control_->leave(localSsrc_);
        }
        control_->stop();
    }
    delete ui;
}

void MainWindow::initMediaEngine() {
    try {
        audioEngine_ = std::make_unique<AudioProcessor>(NetworkMode::LAN);
        networkEngine_ = std::make_unique<NetworkEngine>();

        // Use OS-assigned ephemeral UDP port to avoid collisions between
        // multiple clients running on the same machine.
        const uint16_t localPort = 0;
        if (!networkEngine_->initialize(localPort)) {
            statusBar()->showMessage("Audio network initialization failed");
            return;
        }

        if (!networkEngine_->connect(serverIp_.toStdString(), DEFAULT_AUDIO_PORT)) {
            statusBar()->showMessage("Audio network connect failed");
            return;
        }

        networkEngine_->set_packet_callback(
            [this](uint32_t ssrc, uint16_t seq, const uint8_t* data, size_t len, bool hasPositional, const float* position) {
                if (audioEngine_) {
                    audioEngine_->on_packet(ssrc, seq, data, len, hasPositional, position);
                }
            }
        );

        audioEngine_->set_send_callback(
            [this](uint16_t seq, uint32_t ts, const uint8_t* data, size_t len, bool hasPositional, const float* position) {
                if (!networkEngine_ || !mediaConnected_) {
                    return false;
                }

                if (!callActiveAtomic_.load()) {
                    return false;
                }

                if (mutedAtomic_.load()) {
                    return false;
                }

                if (broadcastMode_.load() && !pushToTalkAtomic_.load()) {
                    return false;
                }

                return networkEngine_->send_audio(seq, ts, data, len, localSsrc_, hasPositional, position);
            }
        );

        networkEngine_->start();
        networkEngine_->send_probe(localSsrc_);
        audioEngine_->start();
        audioEngine_->set_microphone_enabled(true);
        mediaConnected_ = true;
    } catch (...) {
        mediaConnected_ = false;
        statusBar()->showMessage("Media engine initialization failed");
    }
}

QString MainWindow::statusText(ClientStatus status) {
    switch (status) {
    case ClientStatus::Online:
        return "Online";
    case ClientStatus::InCall:
        return "Occupied";
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
    next.reserve(static_cast<int>(users.size()) + 1);
    bool selfSeen = false;

    for (const CtrlUserInfo& u : users) {
        Client c;
        c.ssrc = u.ssrc;
        c.id = QString::number(u.ssrc);
        c.name = QString::fromLocal8Bit(u.name).trimmed();
        if (c.name.isEmpty()) {
            c.name = QStringLiteral("Client %1").arg(c.id);
        }

        if (u.ssrc == localSsrc_) {
            c.name = QString("%1 (You · %2)").arg(clientName_, localIp_);
            c.online = true;
            c.status = ClientStatus::Online;
            selfSeen = true;
        } else {
            c.online = u.online != 0;
            if (!c.online) {
                c.status = ClientStatus::Offline;
            } else if (occupiedTargets_.contains(c.ssrc)) {
                c.status = ClientStatus::InCall;
            } else {
                c.status = ClientStatus::Online;
            }
        }

        next.push_back(c);
    }

    if (!selfSeen) {
        Client self;
        self.ssrc = localSsrc_;
        self.id = QString::number(localSsrc_);
        self.name = QString("%1 (You · %2)").arg(clientName_, localIp_);
        self.online = serverConnected_;
        self.status = self.online ? ClientStatus::Online : ClientStatus::Offline;
        next.push_back(self);
    }

    clients_ = std::move(next);

    for (auto it = selectedClientIds_.begin(); it != selectedClientIds_.end();) {
        const Client* client = findClientById(*it);
        const bool selectable = client && client->ssrc != localSsrc_ && client->status == ClientStatus::Online;
        if (!selectable) {
            it = selectedClientIds_.erase(it);
        } else {
            ++it;
        }
    }
}

void MainWindow::refreshClientList() {
    const QString q = ui->editSearch->text().trimmed();

    QSignalBlocker block(ui->listClients);
    ui->listClients->clear();

    for (const Client &client : clients_) {
        if (!matchesSearch(client, q)) {
            continue;
        }

        auto *item = new QListWidgetItem(QString("%1  (%2)").arg(client.name, statusText(client.status)));
        item->setData(kRoleClientId, client.id);

        Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
        if (client.status == ClientStatus::Online && client.ssrc != localSsrc_) {
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
    int online = 0;
    int inCall = 0;
    int offline = 0;

    for (const Client &client : clients_) {
        if (client.status == ClientStatus::Online) {
            ++online;
        } else if (client.status == ClientStatus::InCall) {
            ++inCall;
        } else {
            ++offline;
        }
    }

    ui->lblOnlineCount->setText(QString("%1 online").arg(online));
    ui->lblInCallCount->setText(QString("%1 occupied").arg(inCall));
    ui->lblOfflineCount->setText(QString("%1 offline").arg(offline));
}

void MainWindow::updateGroupButtonState() {
    int selectedOnline = 0;
    for (const QString &id : selectedClientIds_) {
        const Client *client = findClientById(id);
        if (client && client->status == ClientStatus::Online) {
            ++selectedOnline;
        }
    }

    ui->btnGroupCall->setEnabled(selectedOnline >= 1);
    ui->btnGroupCall->setText(selectedOnline >= 1
                                  ? QString("Talk to Selected (%1)").arg(selectedOnline)
                                  : "Select clients to talk");
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

    if (client->ssrc == localSsrc_) {
        statusBar()->showMessage("You are already online", 2000);
        return;
    }

    if (client->status != ClientStatus::Online) {
        statusBar()->showMessage(QString("Cannot talk to %1: not online").arg(client->name), 3000);
        return;
    }

    applyOccupiedTargets({*client});
    statusBar()->showMessage(QString("Talking to %1. You can now communicate.").arg(client->name), 3000);
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

    callType_ = QStringLiteral("occupied");
    incomingFromSsrc_ = 0;
    callActiveAtomic_.store(true);
    broadcastMode_.store(false);
    pushToTalkAtomic_.store(false);
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
        if (client && client->status == ClientStatus::Online) {
            selectedOnline.push_back(*client);
        }
    }

    if (selectedOnline.isEmpty()) {
        statusBar()->showMessage("Select at least one online client to talk", 3000);
        return;
    }

    applyOccupiedTargets(selectedOnline);
    if (networkEngine_) {
        networkEngine_->send_probe(localSsrc_);
    }
    broadcastMode_.store(false);
    pushToTalkAtomic_.store(false);
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

    callType_ = QStringLiteral("broadcast");
    incomingFromSsrc_ = 0;
    pushToTalkAtomic_.store(false);
    broadcastMode_.store(true);
    callActiveAtomic_.store(true);
    participants_.clear();
    participants_.push_back(clientName_);

    for (const Client &client : clients_) {
        if (client.ssrc != localSsrc_ && client.status != ClientStatus::Offline) {
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
    if (networkEngine_) {
        networkEngine_->send_probe(localSsrc_);
    }

    statusBar()->showMessage("Broadcast enabled to all online clients", 3000);
}

void MainWindow::endCall() {
    if (control_) {
        control_->talk(localSsrc_, {});
    }

    occupiedTargets_.clear();
    participants_.clear();
    callType_.clear();
    incomingFromSsrc_ = 0;
    isPushToTalkPressed_ = false;
    broadcastMode_.store(false);
    pushToTalkAtomic_.store(false);
    callActiveAtomic_.store(false);

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
    pushToTalkAtomic_.store(true);
    statusBar()->showMessage("Speaking to broadcast targets", 1000);
    updateCallStage();
    updateAudioControls();
}

void MainWindow::stopPushToTalk() {
    if (callType_ != "broadcast") {
        return;
    }

    isPushToTalkPressed_ = false;
    pushToTalkAtomic_.store(false);
    statusBar()->showMessage("Stopped speaking", 1000);
    updateCallStage();
    updateAudioControls();
}

void MainWindow::onMuteToggled(bool checked) {
    isMuted_ = !checked;
    mutedAtomic_.store(isMuted_);
    updateHeader();
    updateAudioControls();
}

void MainWindow::pollServerStatus() {
    if (!control_) {
        serverConnected_ = false;
        latencyMs_ = -1;
        smoothedStrength_ = 0;
        strengthSamples_.clear();
        updateHeader();
        return;
    }

    const uint64_t pongAgeMs = control_->get_last_pong_age_ms();
    serverConnected_ = (pongAgeMs <= 5000);
    if (!serverConnected_) {
        latencyMs_ = -1;
        smoothedStrength_ = 0;
        strengthSamples_.clear();
        updateHeader();
        return;
    }

    latencyMs_ = static_cast<int>(control_->get_last_rtt_ms());

    const int sample = strengthBucket(serverConnected_, latencyMs_);
    strengthSamples_.push_back(sample);
    if (strengthSamples_.size() > 3) {
        strengthSamples_.pop_front();
    }

    int sum = 0;
    for (const int v : strengthSamples_) {
        sum += v;
    }
    const double avg = strengthSamples_.empty() ? 0.0 : static_cast<double>(sum) / static_cast<double>(strengthSamples_.size());
    if (avg >= 1.5) {
        smoothedStrength_ = 2;
    } else if (avg >= 0.5) {
        smoothedStrength_ = 1;
    } else {
        smoothedStrength_ = 0;
    }

    updateHeader();
}

void MainWindow::refreshUserList() {
    if (control_ && serverConnected_) {
        control_->request_user_list();
    }
    if (networkEngine_ && mediaConnected_) {
        networkEngine_->send_probe(localSsrc_);
    }
}

void MainWindow::tickVisualizer() {
    const bool active = !participants_.isEmpty() && !isMuted_;
    const bool speaking = isPushToTalkPressed_ || (active && callType_ == "occupied");

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
    if (smoothedStrength_ >= 2) {
        ui->lblLatencyValue->setText(QStringLiteral("Good"));
    } else if (smoothedStrength_ == 1) {
        ui->lblLatencyValue->setText(QStringLiteral("Average"));
    } else {
        ui->lblLatencyValue->setText(QStringLiteral("Bad"));
    }
    ui->lblMuteState->setText(isMuted_ ? "Muted" : "Active");

    const QString dotColor = serverConnected_ ? "#22c55e" : "#ef4444";
    ui->lblServerDot->setStyleSheet(
        QString("background: %1; border-radius: 6px; min-width: 12px; min-height: 12px;").arg(dotColor));

    const QSignalBlocker block(ui->chkMicActive);
    ui->chkMicActive->setChecked(!isMuted_);
}

void MainWindow::updateCallStage() {
    const bool inCall = callActiveAtomic_.load() && !participants_.isEmpty();
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
    ui->btnEndCall->setEnabled(inCall && callActiveAtomic_.load());

    const bool broadcast = (callType_ == "broadcast");
    ui->btnPushToTalk->setEnabled(broadcast && !isMuted_);
    ui->btnPushToTalk->setText("Hold to Broadcast");
    ui->lblPushHint->setVisible(broadcast);

    const bool canTalkInCall = inCall && callActiveAtomic_.load() && !isMuted_;
    const bool micShouldBeOn = canTalkInCall && (callType_ != "broadcast" || isPushToTalkPressed_);
    ui->lblMicActivity->setText(micShouldBeOn ? "Mic Active" : "Mic Idle");
    if (audioEngine_) {
        audioEngine_->set_microphone_enabled(micShouldBeOn);
    }
}

QString MainWindow::displayNameForSsrc(uint32_t ssrc) const {
    if (ssrc == localSsrc_) {
        return clientName_;
    }
    for (const Client& client : clients_) {
        if (client.ssrc == ssrc) {
            return client.name;
        }
    }
    return QString("Client %1").arg(ssrc);
}

void MainWindow::handleTalkUpdate(uint32_t from, const std::vector<uint32_t>& targets) {
    if (from == localSsrc_) {
        return;
    }

    // Do not let incoming route updates override local outgoing call modes.
    if (callType_ == "occupied" || callType_ == "broadcast") {
        return;
    }

    // Only direct target routes indicate "talking to you".
    bool toMe = false;
    for (uint32_t t : targets) {
        if (t == localSsrc_) {
            toMe = true;
            break;
        }
    }

    if (!toMe) {
        // Ignore non-target route churn (e.g. transient targets=0 updates).
        // Keep current incoming call state until explicit user action (End Call)
        // or a new direct target update arrives.
        return;
    }

    const QString fromName = displayNameForSsrc(from);
    participants_.clear();
    participants_.push_back(fromName);
    participants_.push_back(clientName_);
    callType_ = QStringLiteral("incoming");
    incomingFromSsrc_ = from;
    callActiveAtomic_.store(true);
    broadcastMode_.store(false);
    pushToTalkAtomic_.store(false);

    statusBar()->showMessage(QString("%1 is talking to you").arg(fromName), 2000);
    updateCallStage();
    updateAudioControls();
}
