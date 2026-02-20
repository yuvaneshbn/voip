INCLUDEPATH += $$PWD
INCLUDEPATH += $$PWD/shared
INCLUDEPATH += $$PWD/shared/protocol
INCLUDEPATH += $$PWD/thirdparty/opus
INCLUDEPATH += $$PWD/thirdparty/speexdsp/include
INCLUDEPATH += $$PWD/thirdparty/speexdsp

# Auto-discovered file inventories to keep manifests in sync with the tree.
NOX_CLIENT_ALL_SOURCES = $$files($$PWD/client/*.cpp, false)
NOX_CLIENT_ALL_HEADERS = $$files($$PWD/client/*.h, false) \
    $$files($$PWD/client/crypto/*.h, false)

NOX_SERVER_ALL_SOURCES = $$files($$PWD/server/*.cpp, false)
NOX_SERVER_ALL_HEADERS = $$files($$PWD/server/*.h, false)

NOX_SHARED_ALL_SOURCES = $$files($$PWD/shared/*.cpp, false) \
    $$files($$PWD/shared/*.cc, false)
NOX_SHARED_ALL_HEADERS = $$files($$PWD/shared/*.h, false) \
    $$files($$PWD/shared/protocol/*.h, false)

NOX_THIRDPARTY_HEADERS = $$files($$PWD/thirdparty/opus/*.h, false) \
    $$files($$PWD/thirdparty/speexdsp/include/speex/*.h, false)

HEADERS += \
    $$PWD/constants.h \
    $$PWD/shared/protocol/control_protocol.h
