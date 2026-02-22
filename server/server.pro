TARGET = voip_sfu
TEMPLATE = app
QT += core network

CONFIG -= gui
CONFIG += console
CONFIG += c++20

INCLUDEPATH += \
    $$PWD/.. \
    $$PWD/../shared/protocol

HEADERS += \
    $$PWD/../constants.h \
    $$PWD/permission/permission_manager.h \
    $$PWD/../shared/protocol/control_protocol.h

SOURCES += \
    $$PWD/sfu/sfu.cpp \
    $$PWD/permission/permission_manager.cpp

DEFINES += SERVER_PORT=5004
DEFINES += CONTROL_PORT=5005
DEFINES += MAX_CLIENTS=50

win32 {
    LIBS += -lws2_32

    contains(QMAKE_CXX, g\\+\\+|mingw) {
        RUNTIME_BIN_DIR =
        RUNTIME_BIN_CANDIDATES = \
            $$dirname(QMAKE_CXX) \
            C:/msys64/ucrt64/bin \
            C:/Qt/Tools/mingw1310_64/bin

        for(binDir, RUNTIME_BIN_CANDIDATES) {
            exists($$binDir/libwinpthread-1.dll) {
                RUNTIME_BIN_DIR = $$binDir
                break()
            }
        }

        CONFIG(debug, debug|release) {
            TARGET_OUT_DIR = $$OUT_PWD/debug
        } else {
            TARGET_OUT_DIR = $$OUT_PWD/release
        }

        !isEmpty(RUNTIME_BIN_DIR) {
            for(dll, libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll) {
                exists($$RUNTIME_BIN_DIR/$$dll) {
                    QMAKE_POST_LINK += $$QMAKE_COPY $$shell_quote($$RUNTIME_BIN_DIR/$$dll) $$shell_quote($$TARGET_OUT_DIR/$$dll) $$escape_expand(\\n\\t)
                }
            }
        }
    }
}
