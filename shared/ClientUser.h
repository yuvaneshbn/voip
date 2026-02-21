
#ifndef NOX_CLIENTUSER_H_
#define NOX_CLIENTUSER_H_

#include <QObject>
#include <QString>

// Minimal ClientUser class for Nox VoIP
class ClientUser : public QObject {
    Q_OBJECT
public:
    explicit ClientUser(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~ClientUser() {}

    // Add minimal interface as needed
    QString name;
};

#endif // NOX_CLIENTUSER_H_
