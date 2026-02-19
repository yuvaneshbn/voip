#include <QCoreApplication>
#include <QDebug>

#include "constants.h"
#include "control_server.h"

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    quint16 port = DEFAULT_CONTROL_PORT;
    if (argc > 1) {
        bool ok = false;
        const int parsed = QString::fromLocal8Bit(argv[1]).toInt(&ok);
        if (ok && parsed > 0 && parsed <= 65535) {
            port = static_cast<quint16>(parsed);
        }
    }

    ControlServer server;
    if (!server.start(port)) {
        qCritical() << "Failed to start UDP control server on port" << port;
        return 1;
    }

    qInfo() << "VoIP control server listening on UDP" << port;
    return app.exec();
}

