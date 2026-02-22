TEMPLATE = subdirs

SUBDIRS += \
    server \
    shared \
    client

CONFIG += ordered

QT = core network multimedia
CONFIG += c++20
CONFIG += opengl
CONFIG += thread
CONFIG += console

win32 {
    CONFIG += windeployqt
    LIBS += -lws2_32 -lwinmm -lole32 -luuid
}

unix:!macx {
    QT += dbus
    LIBS += -lpulse
}

macx {
    QMAKE_MACOSX_DEPLOYMENT_TARGET = 10.15
    LIBS += -framework CoreAudio \
            -framework AudioToolbox \
            -framework CoreFoundation
}
