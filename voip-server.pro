TEMPLATE = app
TARGET = voip-server

QT += core network
CONFIG += c++17 console
CONFIG -= app_bundle

INCLUDEPATH += $$PWD $$PWD/server $$PWD/shared/protocol
DEPENDPATH += $$INCLUDEPATH

SOURCES += \
    server/main.cpp \
    server/control_server.cpp

HEADERS += \
    server/control_server.h \
    constants.h \
    shared/protocol/control_protocol.h
