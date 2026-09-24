cmake_minimum_required(VERSION 3.21)
project(rm_simulator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Auto-detect a Qt installed via the official online installer.
if(NOT DEFINED CMAKE_PREFIX_PATH AND EXISTS "$ENV{HOME}/Qt/6.11.2/gcc_64")
  list(APPEND CMAKE_PREFIX_PATH "$ENV{HOME}/Qt/6.11.2/gcc_64")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)
qt_standard_project_setup()

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RmDeps.cmake)

qt_add_executable(rm_simulator
  src/main.cpp
  src/Simulator.cpp
  src/Simulator.h
  src/SimulatorWindow.cpp
  src/SimulatorWindow.h
  src/VideoSender.cpp
  src/VideoSender.h
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/MqttClient.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/MqttClient.h
)

target_include_directories(rm_simulator PRIVATE src ${CMAKE_CURRENT_SOURCE_DIR}/../common)
rm_add_proto(rm_simulator)
rm_link_paho(rm_simulator)
rm_link_ffmpeg(rm_simulator)
target_link_libraries(rm_simulator PRIVATE Qt6::Core Qt6::Widgets Qt6::Network)

set_target_properties(rm_simulator PROPERTIES
  BUILD_RPATH "${RM_PAHO_LIB_DIR};${RM_PROTOBUF_LIB_DIR}")
