#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <QApplication>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QtNetwork/QHostAddress>

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

#include "MainWindow.h"
#include "control_client.h"

namespace {
bool isServerAvailable(const QString& serverIp, int attempts = 10, int delayMs = 300) {
    ControlClient control;
    if (!control.initialize(serverIp.toStdString(), DEFAULT_CONTROL_PORT)) {
        return false;
    }

    for (int i = 0; i < attempts; ++i) {
        if (control.ping_server(1000)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    }
    return false;
}

bool isClientNameTaken(const QString& serverIp, const QString& clientName, int timeoutMs = 1000) {
    ControlClient control;
    if (!control.initialize(serverIp.toStdString(), DEFAULT_CONTROL_PORT)) {
        return false;
    }

    std::atomic<bool> received{false};
    std::atomic<bool> taken{false};

    control.set_user_list_callback([&](const std::vector<CtrlUserInfo>& users) {
        for (const CtrlUserInfo& user : users) {
            const QString existing = QString::fromLocal8Bit(user.name).trimmed();
            if (!existing.isEmpty() && existing.compare(clientName, Qt::CaseInsensitive) == 0) {
                taken.store(true);
                break;
            }
        }
        received.store(true);
    });

    control.start();
    control.ping_server(1000);

    const int rounds = 5;
    const int sliceMs = timeoutMs / rounds;
    for (int i = 0; i < rounds && !received.load(); ++i) {
        control.request_user_list();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(sliceMs);
        while (std::chrono::steady_clock::now() < deadline) {
            if (received.load()) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    control.stop();
    return taken.load();
}
}

int main(int argc, char *argv[]) {
#ifdef _WIN32
    // voip_client is built as a GUI subsystem app, so stdout/stderr are not
    // connected unless we explicitly attach/create a console.
    if (AttachConsole(ATTACH_PARENT_PROCESS) || AllocConsole()) {
        FILE* out = nullptr;
        FILE* err = nullptr;
        freopen_s(&out, "CONOUT$", "w", stdout);
        freopen_s(&err, "CONOUT$", "w", stderr);
        std::ios::sync_with_stdio(true);
    }
#endif

    QApplication app(argc, argv);

    QString serverIp = (argc > 1 && argv[1] && argv[1][0] != '\0')
        ? QString::fromLocal8Bit(argv[1])
        : QStringLiteral("127.0.0.1");

    if (!isServerAvailable(serverIp)) {
        while (true) {
            bool okIp = false;
            const QString inputIp = QInputDialog::getText(
                nullptr,
                QStringLiteral("Server Unavailable"),
                QStringLiteral("Unable to reach server.\nEnter server IPv4 address:"),
                QLineEdit::Normal,
                serverIp,
                &okIp).trimmed();

            if (!okIp || inputIp.isEmpty()) {
                QMessageBox::critical(nullptr,
                                      QStringLiteral("Connection Failed"),
                                      QStringLiteral("Failed to connect to server."));
                return 1;
            }

            QHostAddress addr;
            if (!addr.setAddress(inputIp) || addr.protocol() != QAbstractSocket::IPv4Protocol) {
                QMessageBox::warning(nullptr,
                                     QStringLiteral("Invalid IP"),
                                     QStringLiteral("Please enter a valid IPv4 address."));
                continue;
            }

            if (isServerAvailable(inputIp)) {
                serverIp = inputIp;
                break;
            }

            const QMessageBox::StandardButton retry =
                QMessageBox::warning(nullptr,
                                     QStringLiteral("Connection Failed"),
                                     QStringLiteral("Server not reachable at %1. Retry with another IP?")
                                         .arg(inputIp),
                                     QMessageBox::Retry | QMessageBox::Cancel,
                                     QMessageBox::Retry);
            if (retry != QMessageBox::Retry) {
                QMessageBox::critical(nullptr,
                                      QStringLiteral("Connection Failed"),
                                      QStringLiteral("Failed to connect to server."));
                return 1;
            }
        }
    }

    QString clientName;
    QString suggestedName = QStringLiteral("Client 1");
    while (true) {
        bool ok = false;
        const QString entered = QInputDialog::getText(nullptr,
                                                      QStringLiteral("Client Name"),
                                                      QStringLiteral("Choose unique client name:"),
                                                      QLineEdit::Normal,
                                                      suggestedName,
                                                      &ok).trimmed();
        if (!ok) {
            return 0;
        }
        if (entered.isEmpty()) {
            QMessageBox::warning(nullptr,
                                 QStringLiteral("Invalid Name"),
                                 QStringLiteral("Client name cannot be empty."));
            continue;
        }
        if (isClientNameTaken(serverIp, entered)) {
            QMessageBox::warning(nullptr,
                                 QStringLiteral("Name Already Used"),
                                 QStringLiteral("'%1' is already connected. Choose a different name.").arg(entered));
            suggestedName = entered + QStringLiteral("_2");
            continue;
        }
        clientName = entered;
        break;
    }

    MainWindow w(serverIp, clientName);
    w.show();
    return app.exec();
}
