cmake_minimum_required(VERSION 3.16)
project(DynamicVoipQt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_standard_project_setup()

set(PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

qt_add_executable(DynamicVoipQt
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/MainWindow.ui
    ${PROJECT_ROOT}/client/webrtc/control_client.cpp
)

target_include_directories(DynamicVoipQt PRIVATE
    ${PROJECT_ROOT}
    ${PROJECT_ROOT}/client/webrtc
    ${PROJECT_ROOT}/shared/protocol
)

target_link_libraries(DynamicVoipQt PRIVATE Qt6::Widgets)

if(WIN32)
    target_link_libraries(DynamicVoipQt PRIVATE ws2_32)
endif()
