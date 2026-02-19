TEMPLATE = app
TARGET = voip-client

QT += core gui widgets network
CONFIG += c++17

include(voip-shared.pri)

INCLUDEPATH += $$PWD/client

SOURCES += \
    client/main.cpp \
    client/MainWindow.cpp \
    client/control_client.cpp

HEADERS += \
    client/MainWindow.h \
    client/control_client.h

FORMS += \
    client/MainWindow.ui
