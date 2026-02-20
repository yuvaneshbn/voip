TEMPLATE = app
TARGET = voip-client

QT += core gui widgets network multimedia
CONFIG += c++17

include(voip-shared.pri)

INCLUDEPATH += $$PWD/client

# Stable/default client build scope.
NOX_CLIENT_RUNTIME_SOURCES += \
    client/main.cpp \
    client/AudioEngine.cpp \
    client/MainWindow.cpp \
    client/control_client.cpp

NOX_CLIENT_RUNTIME_HEADERS += \
    client/AudioEngine.h \
    client/MainWindow.h \
    client/control_client.h

SOURCES += $$NOX_CLIENT_RUNTIME_SOURCES
HEADERS += $$NOX_CLIENT_RUNTIME_HEADERS

FORMS += \
    client/MainWindow.ui

# Keep full tree visible in IDE without forcing compilation.
OTHER_FILES += \
    $$NOX_CLIENT_ALL_SOURCES \
    $$NOX_CLIENT_ALL_HEADERS \
    $$NOX_SHARED_ALL_SOURCES \
    $$NOX_SHARED_ALL_HEADERS \
    $$NOX_THIRDPARTY_HEADERS

# Opt-in full-tree build (legacy/extended paths).
contains(CONFIG, include_all_client_sources) {
    SOURCES += $$NOX_CLIENT_ALL_SOURCES $$NOX_SHARED_ALL_SOURCES
    HEADERS += $$NOX_CLIENT_ALL_HEADERS $$NOX_SHARED_ALL_HEADERS $$NOX_THIRDPARTY_HEADERS
}
