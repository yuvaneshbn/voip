cmake_minimum_required(VERSION 3.16)
project(VoIPApp C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Prefer a repo-local Qt 6 distribution when present.
set(LOCAL_QT6_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/local-deps/qt6" CACHE PATH
    "Path to local Qt6 root (contains bin/, include/, lib/cmake/Qt6)")
if(EXISTS "${LOCAL_QT6_ROOT}/lib/cmake/Qt6/Qt6Config.cmake")
    list(PREPEND CMAKE_PREFIX_PATH "${LOCAL_QT6_ROOT}")
    list(PREPEND CMAKE_PREFIX_PATH "${LOCAL_QT6_ROOT}/lib/cmake")
    message(STATUS "Using local Qt6 from: ${LOCAL_QT6_ROOT}")
endif()

if(WIN32)
    if(NOT MSVC)
        execute_process(
            COMMAND qmake -query QT_INSTALL_PREFIX
            OUTPUT_VARIABLE _QT_INSTALL_PREFIX
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(_QT_INSTALL_PREFIX)
            list(APPEND CMAKE_PREFIX_PATH "${_QT_INSTALL_PREFIX}")
        endif()
    endif()

    if(MSVC)
        set(_QT_KITS msvc2022_64 msvc2019_64)
    else()
        set(_QT_KITS mingw_64)
    endif()

    file(GLOB _QT_ROOTS "C:/Qt/*")
    foreach(_QT_ROOT IN LISTS _QT_ROOTS)
        foreach(_QT_KIT IN LISTS _QT_KITS)
            if(EXISTS "${_QT_ROOT}/${_QT_KIT}/lib/cmake")
                list(APPEND CMAKE_PREFIX_PATH "${_QT_ROOT}/${_QT_KIT}")
            endif()
        endforeach()
    endforeach()
endif()

find_package(Qt6 QUIET COMPONENTS Widgets Network)
if(Qt6_FOUND)
    set(QT_WIDGETS_TARGET Qt6::Widgets)
    set(QT_NETWORK_TARGET Qt6::Network)
    set(QT_GUI_TARGET Qt6::Gui)
    set(QT_CORE_TARGET Qt6::Core)
    set(QT_PACKAGE_DIR "${Qt6_DIR}")
else()
    find_package(Qt5 REQUIRED COMPONENTS Widgets Network)
    set(QT_WIDGETS_TARGET Qt5::Widgets)
    set(QT_NETWORK_TARGET Qt5::Network)
    set(QT_GUI_TARGET Qt5::Gui)
    set(QT_CORE_TARGET Qt5::Core)
    set(QT_PACKAGE_DIR "${Qt5_DIR}")
endif()

set(QT_QWINDOWS_PLUGIN "")
if(QT_PACKAGE_DIR)
    cmake_path(NORMAL_PATH QT_PACKAGE_DIR)
    set(_QT_PLUGIN_CANDIDATE "${QT_PACKAGE_DIR}/../../../plugins/platforms/qwindows.dll")
    if(EXISTS "${_QT_PLUGIN_CANDIDATE}")
        set(QT_QWINDOWS_PLUGIN "${_QT_PLUGIN_CANDIDATE}")
    endif()
endif()

set(QT_RUNTIME_BIN_DIR "")
if(QT_PACKAGE_DIR)
    cmake_path(NORMAL_PATH QT_PACKAGE_DIR)
    set(_QT_RUNTIME_CANDIDATE "${QT_PACKAGE_DIR}/../../../bin")
    if(EXISTS "${_QT_RUNTIME_CANDIDATE}")
        set(QT_RUNTIME_BIN_DIR "${_QT_RUNTIME_CANDIDATE}")
    endif()
endif()

set(QT_WINDEPLOYQT "")
if(QT_PACKAGE_DIR)
    set(_QT_WINDEPLOYQT_CANDIDATE "${QT_PACKAGE_DIR}/../../../bin/windeployqt.exe")
    if(EXISTS "${_QT_WINDEPLOYQT_CANDIDATE}")
        set(QT_WINDEPLOYQT "${_QT_WINDEPLOYQT_CANDIDATE}")
    endif()
endif()

# ============================================================================
# OPUS LIBRARY CONFIGURATION (Windows local path)
# ============================================================================
set(OPUS_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/opus" CACHE PATH "Path to Opus headers and libraries")

find_path(OPUS_INCLUDE_DIR opus.h
    HINTS
        "${OPUS_ROOT_DIR}"
        "${OPUS_ROOT_DIR}/include"
        "C:/msys64/ucrt64/include"
        "C:/msys64/ucrt64/include/opus"
    REQUIRED
)

if (WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    # 32-bit build: use Win32 Opus binaries
    set(OPUS_LIBRARY "${OPUS_ROOT_DIR}/win32/opus.lib")
else()
    find_library(OPUS_LIBRARY NAMES opus libopus
        HINTS
            "${OPUS_ROOT_DIR}"
            "${OPUS_ROOT_DIR}/lib"
            "C:/msys64/ucrt64/lib"
        REQUIRED
    )
endif()

message(STATUS "Opus Include: ${OPUS_INCLUDE_DIR}")
message(STATUS "Opus Library: ${OPUS_LIBRARY}")

# ============================================================================
# PLATFORM-SPECIFIC AUDIO LIBRARIES
# ============================================================================
if(WIN32)
    set(AUDIO_LIBS winmm ws2_32 ole32 uuid)
    message(STATUS "Windows: using winmm + ws2_32")
elseif(APPLE)
    set(AUDIO_LIBS "-framework CoreAudio" "-framework Foundation")
    message(STATUS "macOS: using CoreAudio")
else()
    find_package(ALSA REQUIRED)
    set(AUDIO_LIBS ALSA::ALSA)
    message(STATUS "Linux: using ALSA")
endif()

# ============================================================================
# GLOBAL INCLUDE DIRECTORIES
# ============================================================================
set(CLIENT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/client")
set(SERVER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/server")
set(SHARED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shared")
set(SPEEXDSP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/speexdsp")
set(SPEEXDSP_INCLUDE_DIR "${SPEEXDSP_DIR}/include")
set(SPEEXDSP_SRC_DIR "${SPEEXDSP_DIR}/libspeexdsp")

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CLIENT_DIR}
    ${CLIENT_DIR}/audio
    ${CLIENT_DIR}/webrtc
    ${SHARED_DIR}
    ${SHARED_DIR}/protocol
    ${OPUS_INCLUDE_DIR}
    ${SPEEXDSP_INCLUDE_DIR}
)

set(SPEEXDSP_SOURCES
    ${SPEEXDSP_SRC_DIR}/buffer.c
    ${SPEEXDSP_SRC_DIR}/fftwrap.c
    ${SPEEXDSP_SRC_DIR}/filterbank.c
    ${SPEEXDSP_SRC_DIR}/jitter.c
    ${SPEEXDSP_SRC_DIR}/kiss_fft.c
    ${SPEEXDSP_SRC_DIR}/kiss_fftr.c
    ${SPEEXDSP_SRC_DIR}/mdf.c
    ${SPEEXDSP_SRC_DIR}/preprocess.c
    ${SPEEXDSP_SRC_DIR}/resample.c
    ${SPEEXDSP_SRC_DIR}/scal.c
    ${SPEEXDSP_SRC_DIR}/smallft.c
)

add_library(speexdsp STATIC ${SPEEXDSP_SOURCES})
target_include_directories(speexdsp
    PUBLIC
        ${SPEEXDSP_INCLUDE_DIR}
    PRIVATE
        ${SPEEXDSP_DIR}
        ${SPEEXDSP_SRC_DIR}
)
target_compile_definitions(speexdsp PRIVATE HAVE_CONFIG_H)

# ============================================================================
# CLIENT EXECUTABLE (Qt GUI)
# ============================================================================
set(CLIENT_AUDIO_SOURCES
    client/audio/AudioProcessor.cpp
    client/audio/AudioPreprocessor.cpp
    client/audio/NoiseSuppressor.cpp
    client/audio/AudioPipeline.cpp
    client/audio/engine_jitter.cpp
    client/audio/codec_opus.cpp
    client/audio/resampler.cpp
)

set(CLIENT_AUDIO_HEADERS
    client/audio/AudioProcessor.h
    client/audio/AudioPreprocessor.h
    client/audio/NoiseSuppressor.h
    client/audio/AudioPipeline.h
    client/audio/engine_jitter.h
    client/audio/codec_opus.h
    client/audio/resampler.h
)

if(WIN32)
    list(APPEND CLIENT_AUDIO_SOURCES
        client/audio/wasapi_capture.cpp
        client/audio/wasapi_playback.cpp
    )
    list(APPEND CLIENT_AUDIO_HEADERS
        client/audio/wasapi_capture.h
        client/audio/wasapi_playback.h
    )
endif()

add_executable(voip_client WIN32
    constants.h
    client/qt/src/main.cpp
    client/qt/src/MainWindow.cpp
    client/qt/src/MainWindow.h
    client/qt/src/MainWindow.ui
    ${CLIENT_AUDIO_SOURCES}
    ${CLIENT_AUDIO_HEADERS}
    client/webrtc/network.cpp
    client/webrtc/network.h
    client/webrtc/control_client.cpp
    client/webrtc/control_client.h
    client/webrtc/AudioTransportShim.h
)

target_include_directories(voip_client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CLIENT_DIR}
    ${CLIENT_DIR}/audio
    ${CLIENT_DIR}/qt/src
    ${CLIENT_DIR}/webrtc
    ${SHARED_DIR}
    ${SHARED_DIR}/protocol
    ${OPUS_INCLUDE_DIR}
    ${SPEEXDSP_INCLUDE_DIR}
)

# Link Qt UI + platform socket libs used by control client
target_link_libraries(voip_client
    PRIVATE
        speexdsp
        ${OPUS_LIBRARY}
        ${AUDIO_LIBS}
        ${QT_NETWORK_TARGET}
        ${QT_WIDGETS_TARGET}
)

if(WIN32)
    target_link_libraries(voip_client PRIVATE ws2_32)

    # Bundle runtime DLLs beside the EXE to avoid PATH-dependent DLL resolution.
    add_custom_command(TARGET voip_client POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:voip_client>
            $<TARGET_FILE_DIR:voip_client>
        COMMAND_EXPAND_LISTS
    )

    if(QT_QWINDOWS_PLUGIN)
        add_custom_command(TARGET voip_client POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_FILE_DIR:voip_client>/platforms
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${QT_QWINDOWS_PLUGIN}"
            $<TARGET_FILE_DIR:voip_client>/platforms
        )
    endif()

    if(QT_WINDEPLOYQT)
        add_custom_command(TARGET voip_client POST_BUILD
            COMMAND "${QT_WINDEPLOYQT}"
                --release
                --dir "$<TARGET_FILE_DIR:voip_client>"
                "$<TARGET_FILE:voip_client>"
        )
    endif()

    # If local Qt6 is present, copy runtime DLLs/plugins directly so the app
    # runs even without global Qt in PATH.
    if(EXISTS "${LOCAL_QT6_ROOT}/bin")
        file(GLOB LOCAL_QT6_RUNTIME_DLLS "${LOCAL_QT6_ROOT}/bin/Qt6*.dll")
        if(LOCAL_QT6_RUNTIME_DLLS)
            add_custom_command(TARGET voip_client POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${LOCAL_QT6_RUNTIME_DLLS}
                    $<TARGET_FILE_DIR:voip_client>
                COMMAND_EXPAND_LISTS
            )
        endif()

        if(EXISTS "${LOCAL_QT6_ROOT}/plugins/platforms/qwindows.dll")
            add_custom_command(TARGET voip_client POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                    $<TARGET_FILE_DIR:voip_client>/platforms
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${LOCAL_QT6_ROOT}/plugins/platforms/qwindows.dll"
                    $<TARGET_FILE_DIR:voip_client>/platforms
            )
        endif()
    endif()

endif()

# Compiler flags for client
if(MSVC)
    target_compile_options(voip_client PRIVATE /W4 /O2 /arch:AVX2 /Zc:__cplusplus)
else()
    target_compile_options(voip_client PRIVATE -Wall -Wextra -O2 -march=native -ffast-math)
endif()

# ============================================================================
# SERVER EXECUTABLE (SFU - Selective Forwarding Unit)
# ============================================================================
add_executable(voip_sfu
    server/sfu/sfu.cpp
    server/permission/permission_manager.cpp
    server/permission/permission_manager.h
)

target_include_directories(voip_sfu PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SERVER_DIR}
    ${SERVER_DIR}/permission
    ${SHARED_DIR}
    ${SHARED_DIR}/protocol
)

target_link_libraries(voip_sfu
    PRIVATE
        ${AUDIO_LIBS}
)

if(MSVC)
    target_compile_options(voip_sfu PRIVATE /W4 /O2)
else()
    target_compile_options(voip_sfu PRIVATE -Wall -Wextra -O2)
endif()

# ============================================================================
# INSTALLATION
# ============================================================================
install(TARGETS voip_client voip_sfu
    RUNTIME DESTINATION bin
)

if (WIN32)
    set(OPUS_DLL_PATH "${OPUS_ROOT_DIR}/opus.dll")
    if (WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4)
        set(OPUS_DLL_PATH "${OPUS_ROOT_DIR}/win32/opus.dll")
    endif()
    if (EXISTS "${OPUS_DLL_PATH}")
        add_custom_command(TARGET voip_client POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${OPUS_DLL_PATH}"
                $<TARGET_FILE_DIR:voip_client>
        )
    else()
        message(WARNING "Opus DLL not found; skipping copy. Place opus.dll near the executable.")
    endif()
endif()

# For single-config generators (e.g., MinGW), copy EXEs into build/Release
if (WIN32 AND NOT CMAKE_CONFIGURATION_TYPES)
    set(RELEASE_DIR "${CMAKE_BINARY_DIR}/Release")
    add_custom_command(TARGET voip_client POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${RELEASE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:voip_client>
            "${RELEASE_DIR}"
    )
    add_custom_command(TARGET voip_sfu POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${RELEASE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:voip_sfu>
            "${RELEASE_DIR}"
    )
endif()

# ============================================================================
# BUILD SUMMARY
# ============================================================================
message(STATUS "")
message(STATUS "============================================")
message(STATUS "VoIP Application - Build Config")
message(STATUS "Executable : voip_client")
message(STATUS "Server     : voip_sfu")
message(STATUS "C++ Std    : 17")
message(STATUS "Codec      : Opus (24 kbps, 48kHz)")
message(STATUS "Opus Root  : ${OPUS_ROOT_DIR}")
message(STATUS "OS         : ${CMAKE_SYSTEM_NAME}")
message(STATUS "Compiler   : ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "============================================")
message(STATUS "")
message(STATUS "Build with:")
message(STATUS "  mkdir build && cd build")
message(STATUS "  cmake .. -DCMAKE_BUILD_TYPE=Release")
message(STATUS "  cmake --build . --config Release")
message(STATUS "")
