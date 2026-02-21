#include <QApplication>
#include <QInputDialog>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QThread>

#include "MainWindow.h"
#include "control_client.h"

namespace {
QString normalizeServerIp(QString serverIp) {
    serverIp = serverIp.trimmed();
    if (serverIp.isEmpty() || serverIp.compare(QStringLiteral("auto"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("auto");
    }
    if (serverIp.compare(QStringLiteral("localhost"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("127.0.0.1");
    }

    QHostAddress address;
    if (!address.setAddress(serverIp)) {
        return serverIp;
    }

    if (address.isLoopback()) {
        return QStringLiteral("127.0.0.1");
    }

    const auto localAddresses = QNetworkInterface::allAddresses();
    for (const QHostAddress &local : localAddresses) {
        if (local == address) {
            return QStringLiteral("127.0.0.1");
        }
    }

    return serverIp;
}

bool isServerAvailable(const QString& serverIp, int attempts = 10, int delayMs = 300) {
    ControlClient control;
    if (!control.initialize(serverIp.toStdString(), DEFAULT_CONTROL_PORT)) {
        return false;
    }

    for (int i = 0; i < attempts; ++i) {
        if (control.ping_server(500)) {
            return true;
        }
        QThread::msleep(static_cast< unsigned long >(delayMs));
    }
    return false;
}

QString resolveServerForUi(const QString &serverIp) {
    if (serverIp.compare(QStringLiteral("auto"), Qt::CaseInsensitive) != 0) {
        return serverIp;
    }

    ControlClient control;
    if (!control.initialize("auto", DEFAULT_CONTROL_PORT)) {
        return serverIp;
    }
    if (!control.ping_server(500)) {
        return serverIp;
    }

    const QString discovered = control.server_ip();
    return discovered.isEmpty() ? serverIp : discovered;
}
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    const QString requestedServerIp = (argc > 1 && argv[1] && argv[1][0] != '\0')
        ? QString::fromLocal8Bit(argv[1])
        : QStringLiteral("auto");
    const QString serverIp = normalizeServerIp(requestedServerIp);

    if (!isServerAvailable(serverIp)) {
        QMessageBox::critical(nullptr,
                              QStringLiteral("Server Unavailable"),
                              QStringLiteral("Unable to find server in the network"));
        return 1;
    }

    bool ok = false;
    const QString clientName = QInputDialog::getText(nullptr,
                                                     QStringLiteral("Client Name"),
                                                     QStringLiteral("Choose client name:"),
                                                     QLineEdit::Normal,
                                                     QStringLiteral("Client 1"),
                                                     &ok);
    if (!ok || clientName.trimmed().isEmpty()) {
        return 0;
    }

    MainWindow w(resolveServerForUi(serverIp), clientName.trimmed());
    w.show();
    return app.exec();
}

