# Set the Qt installation path if CMake fails to find Qt automatically
# set(Qt6_DIR "C:/Qt/6.10.2/msvc2019_64/lib/cmake/Qt6")
cmake_minimum_required(VERSION 3.21)

project(NoxVoip LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Multimedia)

option(NOX_INCLUDE_LEGACY_CLIENT_SOURCES "Compile all client/shared tree sources into voip-client" OFF)
option(NOX_INCLUDE_LEGACY_SERVER_SOURCES "Compile all server/shared tree sources into voip-server" OFF)

set(PROJECT_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/client
    ${CMAKE_CURRENT_SOURCE_DIR}/server
    ${CMAKE_CURRENT_SOURCE_DIR}/shared
    ${CMAKE_CURRENT_SOURCE_DIR}/shared/protocol
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/opus
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/speexdsp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/speexdsp
)

# Runtime/default build scope.
set(NOX_CLIENT_RUNTIME_SOURCES
    client/main.cpp
    client/AudioEngine.cpp
    client/MainWindow.cpp
    client/control_client.cpp
)
set(NOX_CLIENT_RUNTIME_HEADERS
    client/AudioEngine.h
    client/MainWindow.h
    client/control_client.h
    client/MainWindow.ui
)

set(NOX_SERVER_RUNTIME_SOURCES
    server/main.cpp
    server/control_server.cpp
)
set(NOX_SERVER_RUNTIME_HEADERS
    server/control_server.h
)

set(NOX_SHARED_RUNTIME_HEADERS
    constants.h
    shared/protocol/control_protocol.h
)

# Auto-discovered inventories to avoid stale manifests.
file(GLOB NOX_CLIENT_ALL_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/client/*.cpp)
file(GLOB NOX_CLIENT_ALL_HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/client/*.h ${CMAKE_CURRENT_SOURCE_DIR}/client/crypto/*.h)
file(GLOB NOX_SERVER_ALL_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/server/*.cpp)
file(GLOB NOX_SERVER_ALL_HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/server/*.h)
file(GLOB NOX_SHARED_ALL_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shared/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/shared/*.cc)
file(GLOB NOX_SHARED_ALL_HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shared/*.h ${CMAKE_CURRENT_SOURCE_DIR}/shared/protocol/*.h)
file(GLOB NOX_THIRDPARTY_HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/opus/*.h ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/speexdsp/include/speex/*.h)

set(NOX_CLIENT_BUILD_SOURCES ${NOX_CLIENT_RUNTIME_SOURCES})
set(NOX_CLIENT_BUILD_HEADERS ${NOX_CLIENT_RUNTIME_HEADERS} ${NOX_SHARED_RUNTIME_HEADERS})
if(NOX_INCLUDE_LEGACY_CLIENT_SOURCES)
    list(APPEND NOX_CLIENT_BUILD_SOURCES ${NOX_CLIENT_ALL_SOURCES} ${NOX_SHARED_ALL_SOURCES})
    list(APPEND NOX_CLIENT_BUILD_HEADERS ${NOX_CLIENT_ALL_HEADERS} ${NOX_SHARED_ALL_HEADERS} ${NOX_THIRDPARTY_HEADERS})
endif()
list(REMOVE_DUPLICATES NOX_CLIENT_BUILD_SOURCES)
list(REMOVE_DUPLICATES NOX_CLIENT_BUILD_HEADERS)

set(NOX_SERVER_BUILD_SOURCES ${NOX_SERVER_RUNTIME_SOURCES})
set(NOX_SERVER_BUILD_HEADERS ${NOX_SERVER_RUNTIME_HEADERS} ${NOX_SHARED_RUNTIME_HEADERS})
if(NOX_INCLUDE_LEGACY_SERVER_SOURCES)
    list(APPEND NOX_SERVER_BUILD_SOURCES ${NOX_SERVER_ALL_SOURCES} ${NOX_SHARED_ALL_SOURCES})
    list(APPEND NOX_SERVER_BUILD_HEADERS ${NOX_SERVER_ALL_HEADERS} ${NOX_SHARED_ALL_HEADERS} ${NOX_THIRDPARTY_HEADERS})
endif()
list(REMOVE_DUPLICATES NOX_SERVER_BUILD_SOURCES)
list(REMOVE_DUPLICATES NOX_SERVER_BUILD_HEADERS)

add_executable(voip-server
    ${NOX_SERVER_BUILD_SOURCES}
    ${NOX_SERVER_BUILD_HEADERS}
)

target_include_directories(voip-server PRIVATE ${PROJECT_INCLUDE_DIRS})
target_link_libraries(voip-server PRIVATE Qt6::Core Qt6::Network)

add_executable(voip-client WIN32
    ${NOX_CLIENT_BUILD_SOURCES}
    ${NOX_CLIENT_BUILD_HEADERS}
)

target_include_directories(voip-client PRIVATE ${PROJECT_INCLUDE_DIRS})
target_link_libraries(voip-client PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Multimedia)
