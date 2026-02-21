cmake_minimum_required(VERSION 3.21)

if(WIN32)
    set(_NOX_QT_MINGW_GCC_CANDIDATES
        "C:/Qt/Tools/mingw1310_64/bin/gcc.exe"
        "C:/Qt/Tools/mingw1120_64/bin/gcc.exe"
    )
    foreach(_nox_gcc IN LISTS _NOX_QT_MINGW_GCC_CANDIDATES)
        if(EXISTS "${_nox_gcc}")
            set(CMAKE_C_COMPILER "${_nox_gcc}" CACHE FILEPATH "C compiler" FORCE)
            break()
        endif()
    endforeach()
endif()

project(NoxVoip LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

include(CheckIncludeFileCXX)

set(NOX_LOCAL_DEPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/local-deps" CACHE PATH "Local dependency root")
if(EXISTS "${NOX_LOCAL_DEPS_DIR}")
    list(APPEND CMAKE_PREFIX_PATH "${NOX_LOCAL_DEPS_DIR}")
    if(NOT DEFINED Qt6_DIR AND EXISTS "${NOX_LOCAL_DEPS_DIR}/qt6/lib/cmake/Qt6/Qt6Config.cmake")
        set(Qt6_DIR "${NOX_LOCAL_DEPS_DIR}/qt6/lib/cmake/Qt6" CACHE PATH "Qt6 CMake package path")
    endif()
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Multimedia)

set(PROJECT_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/client
    ${CMAKE_CURRENT_SOURCE_DIR}/server
    ${CMAKE_CURRENT_SOURCE_DIR}/shared/protocol
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/opus
    ${NOX_LOCAL_DEPS_DIR}/opus/include
)

set(NOX_CLIENT_SOURCES
    client/main.cpp
    client/AudioEngine.cpp
    client/audio/AecProcessor.cpp
    client/MainWindow.cpp
    client/OpusCodec.cpp
    client/control_client.cpp
    client/hybrid/server_discovery_service.cpp
    shared/protocol/control_wire.cpp
    client/MainWindow.ui
)

set(NOX_SERVER_SOURCES
    server/main.cpp
    server/control_server.cpp
    server/hybrid/client_registry.cpp
    shared/protocol/control_wire.cpp
)

add_executable(voip-client WIN32 ${NOX_CLIENT_SOURCES})
target_include_directories(voip-client PRIVATE ${PROJECT_INCLUDE_DIRS})
target_link_libraries(voip-client PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Multimedia)

option(NOX_ENABLE_SPEEXDSP_AEC "Enable SpeexDSP-based AEC in client capture path" ON)
if(NOX_ENABLE_SPEEXDSP_AEC)
    set(NOX_SPEEXDSP_SRC_ROOT_CANDIDATES
        "${NOX_LOCAL_DEPS_DIR}/speexdsp/source"
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/speexdsp"
    )
    set(NOX_SPEEXDSP_INC_CANDIDATES
        "${NOX_LOCAL_DEPS_DIR}/speexdsp/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/speexdsp/include"
    )
    set(NOX_SPEEXDSP_LIB_CANDIDATES
        "${NOX_LOCAL_DEPS_DIR}/speexdsp/libspeexdsp.lib"
        "${NOX_LOCAL_DEPS_DIR}/speexdsp/lib/libspeexdsp.lib"
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/speexdsp/libspeexdsp.lib"
    )

    set(NOX_SPEEXDSP_INC "")
    foreach(_inc IN LISTS NOX_SPEEXDSP_INC_CANDIDATES)
        if(EXISTS "${_inc}/speex/speex_echo.h")
            set(NOX_SPEEXDSP_INC "${_inc}")
            break()
        endif()
    endforeach()

    set(NOX_SPEEXDSP_LIB "")
    foreach(_lib IN LISTS NOX_SPEEXDSP_LIB_CANDIDATES)
        if(EXISTS "${_lib}")
            set(NOX_SPEEXDSP_LIB "${_lib}")
            break()
        endif()
    endforeach()

    set(NOX_SPEEXDSP_SRC_ROOT "")
    foreach(_root IN LISTS NOX_SPEEXDSP_SRC_ROOT_CANDIDATES)
        if(EXISTS "${_root}/libspeexdsp/os_support.h" AND EXISTS "${_root}/include/speex/speex_echo.h")
            set(NOX_SPEEXDSP_SRC_ROOT "${_root}")
            break()
        endif()
    endforeach()

    if(NOX_SPEEXDSP_SRC_ROOT)
        add_library(nox-speexdsp STATIC
            ${NOX_SPEEXDSP_SRC_ROOT}/libspeexdsp/buffer.c
            ${NOX_SPEEXDSP_SRC_ROOT}/libspeexdsp/fftwrap.c
            ${NOX_SPEEXDSP_SRC_ROOT}/libspeexdsp/filterbank.c
            ${NOX_SPEEXDSP_SRC_ROOT}/libspeexdsp/kiss_fft.c
            ${NOX_SPEEXDSP_SRC_ROOT}/libspeexdsp/kiss_fftr.c
            ${NOX_SPEEXDSP_SRC_ROOT}/libspeexdsp/mdf.c
            ${NOX_SPEEXDSP_SRC_ROOT}/libspeexdsp/preprocess.c
            ${NOX_SPEEXDSP_SRC_ROOT}/libspeexdsp/scal.c
            ${NOX_SPEEXDSP_SRC_ROOT}/libspeexdsp/smallft.c
        )
        set_target_properties(nox-speexdsp PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
        target_include_directories(nox-speexdsp PUBLIC
            ${NOX_SPEEXDSP_SRC_ROOT}/include
            ${NOX_SPEEXDSP_SRC_ROOT}/libspeexdsp
        )
        target_compile_definitions(nox-speexdsp PRIVATE FLOATING_POINT USE_SMALLFT EXPORT=)
        target_compile_definitions(voip-client PRIVATE NOX_HAS_SPEEXDSP_AEC=1)
        target_include_directories(voip-client PRIVATE ${NOX_SPEEXDSP_SRC_ROOT}/include)
        target_link_libraries(voip-client PRIVATE nox-speexdsp)
    elseif(NOX_SPEEXDSP_INC AND NOX_SPEEXDSP_LIB)
        target_compile_definitions(voip-client PRIVATE NOX_HAS_SPEEXDSP_AEC=1)
        target_include_directories(voip-client PRIVATE ${NOX_SPEEXDSP_INC})
        target_link_libraries(voip-client PRIVATE "${NOX_SPEEXDSP_LIB}")
    else()
        message(STATUS "SpeexDSP AEC not enabled: install libspeexdsp under local-deps/speexdsp to activate.")
    endif()
endif()

set(NOX_OPUS_LIB_CANDIDATES
    "${NOX_LOCAL_DEPS_DIR}/opus/opus.lib"
    "${NOX_LOCAL_DEPS_DIR}/opus/lib/opus.lib"
    "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/opus/opus.lib"
    "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/opus/win32/opus.lib"
)
set(NOX_OPUS_DLL_CANDIDATES
    "${NOX_LOCAL_DEPS_DIR}/opus/opus.dll"
    "${NOX_LOCAL_DEPS_DIR}/opus/bin/opus.dll"
    "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/opus/opus.dll"
    "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/opus/win32/opus.dll"
)

set(NOX_OPUS_LIB "")
foreach(_nox_lib IN LISTS NOX_OPUS_LIB_CANDIDATES)
    if(EXISTS "${_nox_lib}")
        set(NOX_OPUS_LIB "${_nox_lib}")
        break()
    endif()
endforeach()
if(NOX_OPUS_LIB STREQUAL "")
    message(FATAL_ERROR "Opus import library not found (checked local-deps/opus and thirdparty/opus).")
endif()
target_link_libraries(voip-client PRIVATE "${NOX_OPUS_LIB}")

set(NOX_OPUS_DLL "")
foreach(_nox_dll IN LISTS NOX_OPUS_DLL_CANDIDATES)
    if(EXISTS "${_nox_dll}")
        set(NOX_OPUS_DLL "${_nox_dll}")
        break()
    endif()
endforeach()
if(NOX_OPUS_DLL)
    add_custom_command(TARGET voip-client POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${NOX_OPUS_DLL}"
            "$<TARGET_FILE_DIR:voip-client>/opus.dll"
        COMMENT "Copying Opus runtime DLL beside voip-client"
    )
endif()

add_executable(voip-server ${NOX_SERVER_SOURCES})
target_include_directories(voip-server PRIVATE ${PROJECT_INCLUDE_DIRS})
target_link_libraries(voip-server PRIVATE Qt6::Core Qt6::Network)

option(NOX_ENABLE_ASIO_BACKEND "Build Asio/Boost.Asio hybrid networking targets" ON)
if(NOX_ENABLE_ASIO_BACKEND)
    set(NOX_ASIO_LOCAL_INCLUDE "")
    if(EXISTS "${NOX_LOCAL_DEPS_DIR}/asio/include/asio.hpp")
        set(NOX_ASIO_LOCAL_INCLUDE "${NOX_LOCAL_DEPS_DIR}/asio/include")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/asio/asio/include/asio.hpp")
        set(NOX_ASIO_LOCAL_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/asio/asio/include")
    endif()

    check_include_file_cxx("asio.hpp" NOX_HAS_STANDALONE_ASIO)
    check_include_file_cxx("boost/asio.hpp" NOX_HAS_BOOST_ASIO)
    if(NOX_ASIO_LOCAL_INCLUDE OR NOX_HAS_STANDALONE_ASIO OR NOX_HAS_BOOST_ASIO)
        add_executable(nox-asio-server server/asio_hybrid_server.cpp)
        add_executable(nox-asio-client client/asio_hybrid_client.cpp)
        target_include_directories(nox-asio-server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        target_include_directories(nox-asio-client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        if(NOX_ASIO_LOCAL_INCLUDE)
            target_include_directories(nox-asio-server PRIVATE "${NOX_ASIO_LOCAL_INCLUDE}")
            target_include_directories(nox-asio-client PRIVATE "${NOX_ASIO_LOCAL_INCLUDE}")
            target_compile_definitions(nox-asio-server PRIVATE ASIO_STANDALONE)
            target_compile_definitions(nox-asio-client PRIVATE ASIO_STANDALONE)
        elseif(NOX_HAS_STANDALONE_ASIO)
            target_compile_definitions(nox-asio-server PRIVATE ASIO_STANDALONE)
            target_compile_definitions(nox-asio-client PRIVATE ASIO_STANDALONE)
        else()
            find_package(Boost REQUIRED COMPONENTS system)
            target_link_libraries(nox-asio-server PRIVATE Boost::system)
            target_link_libraries(nox-asio-client PRIVATE Boost::system)
        endif()
        if(WIN32)
            target_link_libraries(nox-asio-server PRIVATE ws2_32)
            target_link_libraries(nox-asio-client PRIVATE ws2_32)
            target_link_libraries(nox-asio-server PRIVATE mswsock)
            target_link_libraries(nox-asio-client PRIVATE mswsock)
        endif()
    else()
        message(STATUS "Skipping nox-asio-* targets: neither asio.hpp nor boost/asio.hpp found.")
    endif()
endif()

option(NOX_ENABLE_PROTOBUF_CONTROL "Enable protobuf control schema generation/bootstrap" OFF)
if(NOX_ENABLE_PROTOBUF_CONTROL)
    set(NOX_PROTOBUF_HINT_ROOTS
        "${NOX_LOCAL_DEPS_DIR}/protobuf"
        "C:/msys64/ucrt64"
    )
    foreach(_pb_root IN LISTS NOX_PROTOBUF_HINT_ROOTS)
        if(EXISTS "${_pb_root}/include/google/protobuf/message.h")
            set(Protobuf_INCLUDE_DIR "${_pb_root}/include")
        endif()
        if(EXISTS "${_pb_root}/bin/protoc.exe")
            set(Protobuf_PROTOC_EXECUTABLE "${_pb_root}/bin/protoc.exe")
        elseif(EXISTS "${_pb_root}/protoc.exe")
            set(Protobuf_PROTOC_EXECUTABLE "${_pb_root}/protoc.exe")
        endif()
        if(EXISTS "${_pb_root}/lib/libprotobuf.dll.a")
            set(Protobuf_LIBRARY "${_pb_root}/lib/libprotobuf.dll.a")
        elseif(EXISTS "${_pb_root}/lib/libprotobuf.a")
            set(Protobuf_LIBRARY "${_pb_root}/lib/libprotobuf.a")
        elseif(EXISTS "${_pb_root}/lib/protobuf.lib")
            set(Protobuf_LIBRARY "${_pb_root}/lib/protobuf.lib")
        endif()
    endforeach()

    find_package(Protobuf QUIET)
    if(NOT Protobuf_FOUND AND Protobuf_INCLUDE_DIR AND Protobuf_LIBRARY AND Protobuf_PROTOC_EXECUTABLE)
        add_library(protobuf::libprotobuf UNKNOWN IMPORTED)
        set_target_properties(protobuf::libprotobuf PROPERTIES
            IMPORTED_LOCATION "${Protobuf_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${Protobuf_INCLUDE_DIR}"
        )
        set(Protobuf_FOUND TRUE)
    endif()
    if(Protobuf_FOUND)
        set(NOX_PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shared/protocol")
        set(NOX_PROTO_FILE "${NOX_PROTO_DIR}/control.proto")
        if(EXISTS "${NOX_PROTO_FILE}")
            set(NOX_PROTO_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/proto")
            file(MAKE_DIRECTORY "${NOX_PROTO_OUT_DIR}")
            add_custom_command(
                OUTPUT "${NOX_PROTO_OUT_DIR}/control.pb.cc" "${NOX_PROTO_OUT_DIR}/control.pb.h"
                COMMAND ${Protobuf_PROTOC_EXECUTABLE}
                    --cpp_out="${NOX_PROTO_OUT_DIR}"
                    --proto_path="${NOX_PROTO_DIR}"
                    "${NOX_PROTO_FILE}"
                DEPENDS "${NOX_PROTO_FILE}"
                COMMENT "Generating protobuf control messages"
            )
            add_library(nox-control-proto STATIC "${NOX_PROTO_OUT_DIR}/control.pb.cc")
            target_include_directories(nox-control-proto PUBLIC "${NOX_PROTO_OUT_DIR}")
            target_link_libraries(nox-control-proto PUBLIC protobuf::libprotobuf)
            target_compile_definitions(voip-client PRIVATE NOX_HAS_PROTOBUF_CONTROL=1)
            target_compile_definitions(voip-server PRIVATE NOX_HAS_PROTOBUF_CONTROL=1)
            target_link_libraries(voip-client PRIVATE nox-control-proto)
            target_link_libraries(voip-server PRIVATE nox-control-proto)
        else()
            message(WARNING "NOX_ENABLE_PROTOBUF_CONTROL=ON but shared/protocol/control.proto not found.")
        endif()
    else()
        message(WARNING "NOX_ENABLE_PROTOBUF_CONTROL=ON but Protobuf was not found.")
    endif()
endif()

option(NOX_BUILD_PORTAUDIO_PROBE "Build PortAudio + Opus pipeline probe app" ON)
if(NOX_BUILD_PORTAUDIO_PROBE)
    set(NOX_PORTAUDIO_HEADER_CANDIDATES
        "${NOX_LOCAL_DEPS_DIR}/portaudio/include/portaudio.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/portaudio/include/portaudio.h"
    )
    set(NOX_PORTAUDIO_LIB_CANDIDATES
        "${NOX_LOCAL_DEPS_DIR}/portaudio/portaudio.lib"
        "${NOX_LOCAL_DEPS_DIR}/portaudio/lib/portaudio.lib"
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/portaudio/portaudio.lib"
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/portaudio/lib/portaudio.lib"
    )
    set(NOX_PORTAUDIO_DLL_CANDIDATES
        "${NOX_LOCAL_DEPS_DIR}/portaudio/portaudio.dll"
        "${NOX_LOCAL_DEPS_DIR}/portaudio/bin/portaudio.dll"
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/portaudio/portaudio.dll"
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/portaudio/bin/portaudio.dll"
    )

    set(NOX_PORTAUDIO_HEADER "")
    foreach(_nox_header IN LISTS NOX_PORTAUDIO_HEADER_CANDIDATES)
        if(EXISTS "${_nox_header}")
            set(NOX_PORTAUDIO_HEADER "${_nox_header}")
            break()
        endif()
    endforeach()

    set(NOX_PORTAUDIO_LIB "")
    foreach(_nox_lib IN LISTS NOX_PORTAUDIO_LIB_CANDIDATES)
        if(EXISTS "${_nox_lib}")
            set(NOX_PORTAUDIO_LIB "${_nox_lib}")
            break()
        endif()
    endforeach()

    if(NOX_PORTAUDIO_HEADER AND NOX_PORTAUDIO_LIB)
        get_filename_component(NOX_PORTAUDIO_INCLUDE_DIR "${NOX_PORTAUDIO_HEADER}" DIRECTORY)

        add_executable(opus-portaudio-probe tools/opus_portaudio_probe.cpp)
        target_include_directories(opus-portaudio-probe PRIVATE
            ${NOX_PORTAUDIO_INCLUDE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/opus
            ${NOX_LOCAL_DEPS_DIR}/opus/include
        )
        target_link_libraries(opus-portaudio-probe PRIVATE "${NOX_PORTAUDIO_LIB}" "${NOX_OPUS_LIB}")

        if(WIN32)
            target_link_libraries(opus-portaudio-probe PRIVATE ws2_32)
        endif()

        set(NOX_PORTAUDIO_DLL "")
        foreach(_nox_dll IN LISTS NOX_PORTAUDIO_DLL_CANDIDATES)
            if(EXISTS "${_nox_dll}")
                set(NOX_PORTAUDIO_DLL "${_nox_dll}")
                break()
            endif()
        endforeach()

        if(NOX_OPUS_DLL)
            add_custom_command(TARGET opus-portaudio-probe POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${NOX_OPUS_DLL}"
                    "$<TARGET_FILE_DIR:opus-portaudio-probe>/opus.dll"
                COMMENT "Copying Opus runtime DLL beside opus-portaudio-probe"
            )
        endif()

        if(NOX_PORTAUDIO_DLL)
            add_custom_command(TARGET opus-portaudio-probe POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${NOX_PORTAUDIO_DLL}"
                    "$<TARGET_FILE_DIR:opus-portaudio-probe>/portaudio.dll"
                COMMENT "Copying PortAudio runtime DLL beside opus-portaudio-probe"
            )
        endif()
    else()
        message(STATUS "Skipping opus-portaudio-probe: PortAudio header/lib not found (expected under local-deps/portaudio or thirdparty/portaudio).")
    endif()
endif()
