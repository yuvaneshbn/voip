TEMPLATE = app
TARGET = voip-server

QT += core network
CONFIG += c++17 console
CONFIG -= app_bundle

include(voip-shared.pri)

INCLUDEPATH += $$PWD/server

SOURCES += \
    server/main.cpp \
    server/control_server.cpp

HEADERS += \
    server/control_server.h
