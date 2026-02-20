TEMPLATE = app
TARGET = voip-server

QT += core network
CONFIG += c++17 console
CONFIG -= app_bundle

include(voip-shared.pri)

INCLUDEPATH += $$PWD/server

# Stable/default server build scope.
NOX_SERVER_RUNTIME_SOURCES += \
    server/main.cpp \
    server/control_server.cpp

NOX_SERVER_RUNTIME_HEADERS += \
    server/control_server.h

SOURCES += $$NOX_SERVER_RUNTIME_SOURCES
HEADERS += $$NOX_SERVER_RUNTIME_HEADERS

# Keep full tree visible in IDE without forcing compilation.
OTHER_FILES += \
    $$NOX_SERVER_ALL_SOURCES \
    $$NOX_SERVER_ALL_HEADERS \
    $$NOX_SHARED_ALL_SOURCES \
    $$NOX_SHARED_ALL_HEADERS \
    $$NOX_THIRDPARTY_HEADERS

# Opt-in full-tree build (legacy/extended paths).
contains(CONFIG, include_all_server_sources) {
    SOURCES += $$NOX_SERVER_ALL_SOURCES $$NOX_SHARED_ALL_SOURCES
    HEADERS += $$NOX_SERVER_ALL_HEADERS $$NOX_SHARED_ALL_HEADERS $$NOX_THIRDPARTY_HEADERS
}
