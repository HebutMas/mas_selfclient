cmake_minimum_required(VERSION 3.21)
project(rm_custom_client LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT DEFINED CMAKE_PREFIX_PATH AND EXISTS "$ENV{HOME}/Qt/6.11.2/gcc_64")
  list(APPEND CMAKE_PREFIX_PATH "$ENV{HOME}/Qt/6.11.2/gcc_64")
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
qt_standard_project_setup()

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/RmDeps.cmake)

add_executable(rm_client
  src/app/main.cpp
  src/core/UserActions.cpp
  src/core/robots/CommonActions.cpp
  src/core/robots/HeavyActions.cpp
  src/core/robots/InfantryActions.cpp
  src/core/robots/SentryActions.cpp
  src/core/robots/AerialActions.cpp
  src/core/robots/DartActions.cpp
  src/core/robots/RadarActions.cpp
  src/net/GameData.h         src/net/GameData.cpp
  src/net/VideoReceiver.h    src/net/VideoReceiver.cpp
  src/ui/MainWindow.h        src/ui/MainWindow.cpp
  src/ui/VideoView.h         src/ui/VideoView.cpp
  src/ui/OverlayHud.h        src/ui/OverlayHud.cpp
  src/ui/ActionOverlay.h     src/ui/ActionOverlay.cpp
  src/ui/Minimap.h           src/ui/Minimap.cpp
  src/ui/StartPanelOverlay.h src/ui/StartPanelOverlay.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/MqttClient.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../common/MqttClient.h
)

qt_add_resources(rm_client "assets" PREFIX "/rm" FILES assets/map.png)

target_include_directories(rm_client PRIVATE src ${CMAKE_CURRENT_SOURCE_DIR}/../common)
rm_add_proto(rm_client)
rm_link_paho(rm_client)
rm_link_ffmpeg(rm_client)
target_link_libraries(rm_client PRIVATE Qt6::Widgets Qt6::Network)

set_target_properties(rm_client PROPERTIES
  BUILD_RPATH "${RM_PAHO_LIB_DIR};${RM_PROTOBUF_LIB_DIR}")

option(RM_BUILD_TESTS "Build unit tests" ON)
if(RM_BUILD_TESTS)
  enable_testing()
  find_package(Qt6 REQUIRED COMPONENTS Test)
  add_executable(test_user_actions
    tests/test_user_actions.cpp
    src/core/UserActions.cpp
    src/core/robots/CommonActions.cpp
    src/core/robots/HeavyActions.cpp
    src/core/robots/InfantryActions.cpp
    src/core/robots/SentryActions.cpp
    src/core/robots/AerialActions.cpp
    src/core/robots/DartActions.cpp
    src/core/robots/RadarActions.cpp
  )
  target_include_directories(test_user_actions PRIVATE src)
  rm_add_proto(test_user_actions)
  target_link_libraries(test_user_actions PRIVATE Qt6::Gui Qt6::Test)
  set_target_properties(test_user_actions PROPERTIES BUILD_RPATH "${RM_PROTOBUF_LIB_DIR}")
  add_test(NAME user_actions COMMAND test_user_actions)
  set_tests_properties(user_actions PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
