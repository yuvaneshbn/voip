TEMPLATE = app
TARGET = voip-client

QT += core gui widgets network multimedia
CONFIG += c++17

INCLUDEPATH += $$PWD $$PWD/client $$PWD/shared/protocol $$PWD/thirdparty/opus $$PWD/local-deps/opus/include
DEPENDPATH += $$INCLUDEPATH

SOURCES += \
    client/main.cpp \
    client/AudioEngine.cpp \
    client/MainWindow.cpp \
    client/OpusCodec.cpp \
    client/control_client.cpp

HEADERS += \
    client/AudioEngine.h \
    client/MainWindow.h \
    client/OpusCodec.h \
    client/control_client.h \
    constants.h \
    shared/protocol/control_protocol.h

FORMS += \
    client/MainWindow.ui

exists($$PWD/local-deps/opus/opus.lib) {
    LIBS += $$PWD/local-deps/opus/opus.lib
} else:exists($$PWD/local-deps/opus/lib/opus.lib) {
    LIBS += $$PWD/local-deps/opus/lib/opus.lib
} else:exists($$PWD/thirdparty/opus/opus.lib) {
    LIBS += $$PWD/thirdparty/opus/opus.lib
} else:exists($$PWD/thirdparty/opus/win32/opus.lib) {
    LIBS += $$PWD/thirdparty/opus/win32/opus.lib
} else {
    error("Missing Opus library. Expected local-deps/opus or thirdparty/opus")
}
