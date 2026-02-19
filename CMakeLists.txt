
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

set(PROJECT_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/client
    ${CMAKE_CURRENT_SOURCE_DIR}/server
    ${CMAKE_CURRENT_SOURCE_DIR}/shared
    ${CMAKE_CURRENT_SOURCE_DIR}/shared/protocol
)

add_executable(voip-server
    server/main.cpp
    server/control_server.cpp
    server/control_server.h
    constants.h
    shared/protocol/control_protocol.h
)

target_include_directories(voip-server PRIVATE ${PROJECT_INCLUDE_DIRS})
target_link_libraries(voip-server PRIVATE Qt6::Core Qt6::Network)

add_executable(voip-client WIN32
    client/main.cpp
    client/AudioEngine.cpp
    client/AudioEngine.h
    client/MainWindow.cpp
    client/MainWindow.h
    client/MainWindow.ui
    client/control_client.cpp
    client/control_client.h
    constants.h
    shared/protocol/control_protocol.h
)

target_include_directories(voip-client PRIVATE ${PROJECT_INCLUDE_DIRS})
target_link_libraries(voip-client PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Multimedia)
