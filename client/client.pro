TARGET = voip_client
TEMPLATE = app
QT += core network widgets multimedia

CONFIG += c++20
CONFIG -= console

INCLUDEPATH += \
    $$PWD/.. \
    $$PWD/audio \
    $$PWD/qt/src \
    $$PWD/webrtc \
    $$PWD/../shared/protocol \
    $$PWD/../thirdparty/opus \
    $$PWD/../thirdparty/speexdsp/include \
    $$PWD/../thirdparty/speexdsp

HEADERS += \
    $$PWD/../constants.h \
    $$PWD/audio/AudioProcessor.h \
    $$PWD/audio/AudioPreprocessor.h \
    $$PWD/audio/NoiseSuppressor.h \
    $$PWD/audio/AudioPipeline.h \
    $$PWD/audio/engine_jitter.h \
    $$PWD/audio/codec_opus.h \
    $$PWD/audio/resampler.h \
    $$PWD/audio/wasapi_capture.h \
    $$PWD/audio/wasapi_playback.h \
    $$PWD/qt/src/MainWindow.h \
    $$PWD/webrtc/network.h \
    $$PWD/webrtc/control_client.h \
    $$PWD/webrtc/AudioTransportShim.h \
    $$PWD/../shared/protocol/control_protocol.h

SOURCES += \
    $$PWD/audio/AudioProcessor.cpp \
    $$PWD/audio/AudioPreprocessor.cpp \
    $$PWD/audio/NoiseSuppressor.cpp \
    $$PWD/audio/AudioPipeline.cpp \
    $$PWD/audio/engine_jitter.cpp \
    $$PWD/audio/codec_opus.cpp \
    $$PWD/audio/resampler.cpp \
    $$PWD/audio/wasapi_capture.cpp \
    $$PWD/audio/wasapi_playback.cpp \
    $$PWD/qt/src/main.cpp \
    $$PWD/qt/src/MainWindow.cpp \
    $$PWD/webrtc/network.cpp \
    $$PWD/webrtc/control_client.cpp

SOURCES += \
    $$PWD/../thirdparty/speexdsp/libspeexdsp/buffer.c \
    $$PWD/../thirdparty/speexdsp/libspeexdsp/fftwrap.c \
    $$PWD/../thirdparty/speexdsp/libspeexdsp/filterbank.c \
    $$PWD/../thirdparty/speexdsp/libspeexdsp/jitter.c \
    $$PWD/../thirdparty/speexdsp/libspeexdsp/kiss_fft.c \
    $$PWD/../thirdparty/speexdsp/libspeexdsp/kiss_fftr.c \
    $$PWD/../thirdparty/speexdsp/libspeexdsp/mdf.c \
    $$PWD/../thirdparty/speexdsp/libspeexdsp/preprocess.c \
    $$PWD/../thirdparty/speexdsp/libspeexdsp/resample.c \
    $$PWD/../thirdparty/speexdsp/libspeexdsp/scal.c \
    $$PWD/../thirdparty/speexdsp/libspeexdsp/smallft.c

FORMS += \
    $$PWD/qt/src/MainWindow.ui

DEFINES += HAVE_CONFIG_H

win32 {
    LIBS += -lws2_32 -lwinmm -lole32 -luuid
    contains(QMAKE_TARGET.arch, x86): OPUS_LIB = $$PWD/../thirdparty/opus/win32/opus.lib
    else: OPUS_LIB = $$PWD/../thirdparty/opus/opus.lib
    exists($$OPUS_LIB) {
        OPUS_LIB_WIN = $$replace(OPUS_LIB, /, \\)
        LIBS += \"$${OPUS_LIB_WIN}\"
    }

    OPUS_DLL = $$PWD/../thirdparty/opus/opus.dll
    !exists($$OPUS_DLL) {
        OPUS_DLL = $$PWD/../thirdparty/opus/win32/opus.dll
    }
    exists($$OPUS_DLL) {
        CONFIG(debug, debug|release) {
            TARGET_OUT_DIR = $$OUT_PWD/debug
        } else {
            TARGET_OUT_DIR = $$OUT_PWD/release
        }
        OPUS_SRC_WIN = $$replace(OPUS_DLL, /, \\)
        OPUS_DST_WIN = $$replace(TARGET_OUT_DIR, /, \\)\\opus.dll
        QMAKE_POST_LINK += cmd /c copy /Y \"$$OPUS_SRC_WIN\" \"$$OPUS_DST_WIN\" $$escape_expand(\\n\\t)
    }
}
