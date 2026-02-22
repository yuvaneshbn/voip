TEMPLATE = lib
TARGET = voip-shared
QT += core

CONFIG += staticlib
CONFIG += c++20
DEFINES += MUMBLE_VERSION_MAJOR=1 MUMBLE_VERSION_MINOR=0 MUMBLE_VERSION_PATCH=0

INCLUDEPATH += $$PWD

HEADERS += \
    $$PWD/protocol/AudioPacket.h \
    $$PWD/protocol/ControlPacket.h \
    $$PWD/protocol/control_protocol.h \
    $$PWD/protocol/network_packet.h \
    $$PWD/protocol/PacketDataStream.h \
    $$PWD/protocol/Version.h \
    $$PWD/protocol/VolumeAdjustment.h \
    $$PWD/utils/ByteBuffer.h \
    $$PWD/utils/Logger.h \
    $$PWD/utils/Timer.h

SOURCES += \
    $$PWD/protocol/AudioPacket.cpp \
    $$PWD/protocol/ControlPacket.cpp \
    $$PWD/protocol/Version.cpp \
    $$PWD/protocol/VolumeAdjustment.cpp \
    $$PWD/utils/ByteBuffer.cpp \
    $$PWD/utils/Logger.cpp \
    $$PWD/utils/Timer.cpp

PUBLIC_HEADERS += \
    $$PWD/protocol/AudioPacket.h \
    $$PWD/protocol/ControlPacket.h
