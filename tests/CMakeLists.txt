cmake_minimum_required(VERSION 3.16)

project(SmartClipTests)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Svg Test)
find_package(Qt6 QUIET COMPONENTS Mqtt)

include_directories(${CMAKE_SOURCE_DIR})

qt_add_executable(SettingsDialogTest
    SettingsDialogTest.cpp
    ${CMAKE_SOURCE_DIR}/SettingsDialog.cpp
    ${CMAKE_SOURCE_DIR}/SettingsManager.cpp
    ${CMAKE_SOURCE_DIR}/Crypto.cpp
    # Статус соединения в диалоге требует SyncManager (+ его зависимости).
    ${CMAKE_SOURCE_DIR}/SyncManager.cpp
    ${CMAKE_SOURCE_DIR}/MqttClient.cpp
    ${CMAKE_SOURCE_DIR}/SyncEngine.cpp
    ${CMAKE_SOURCE_DIR}/HistoryManager.cpp
)

target_link_libraries(SettingsDialogTest
    PRIVATE
        Qt6::Widgets
        Qt6::Test
)

if(OpenSSL_FOUND)
    target_link_libraries(SettingsDialogTest PRIVATE OpenSSL::Crypto)
    target_compile_definitions(SettingsDialogTest PRIVATE SMARTCLIP_HAVE_OPENSSL)
endif()

if(Qt6Mqtt_FOUND)
    target_link_libraries(SettingsDialogTest PRIVATE Qt6::Mqtt)
    target_compile_definitions(SettingsDialogTest PRIVATE SMARTCLIP_HAVE_MQTT)
endif()

qt_add_executable(HistoryManagerTest
    HistoryManagerTest.cpp
    ${CMAKE_SOURCE_DIR}/HistoryManager.cpp
    ${CMAKE_SOURCE_DIR}/Crypto.cpp
)

target_link_libraries(HistoryManagerTest
    PRIVATE
        Qt6::Test
)

if(OpenSSL_FOUND)
    target_link_libraries(HistoryManagerTest PRIVATE OpenSSL::Crypto)
    target_compile_definitions(HistoryManagerTest PRIVATE SMARTCLIP_HAVE_OPENSSL)
endif()

qt_add_executable(SyncEngineTest
    SyncEngineTest.cpp
    ${CMAKE_SOURCE_DIR}/SyncEngine.cpp
    ${CMAKE_SOURCE_DIR}/HistoryManager.cpp
    ${CMAKE_SOURCE_DIR}/SettingsManager.cpp
    ${CMAKE_SOURCE_DIR}/Crypto.cpp
)

target_link_libraries(SyncEngineTest
    PRIVATE
        Qt6::Test
)

if(OpenSSL_FOUND)
    target_link_libraries(SyncEngineTest PRIVATE OpenSSL::Crypto)
    target_compile_definitions(SyncEngineTest PRIVATE SMARTCLIP_HAVE_OPENSSL)
endif()

qt_add_executable(LaunchAgentManagerTest
    LaunchAgentManagerTest.cpp
    ${CMAKE_SOURCE_DIR}/LaunchAgentManager.cpp
)

target_link_libraries(LaunchAgentManagerTest
    PRIVATE
        Qt6::Test
)

qt_add_executable(SmartClipAppTest
    SmartClipAppTest.cpp
    ${CMAKE_SOURCE_DIR}/SmartClipApp.cpp
    ${CMAKE_SOURCE_DIR}/HelpDialog.cpp
    ${CMAKE_SOURCE_DIR}/SettingsManager.cpp
    ${CMAKE_SOURCE_DIR}/SettingsDialog.cpp
    ${CMAKE_SOURCE_DIR}/HistoryManager.cpp
    ${CMAKE_SOURCE_DIR}/Crypto.cpp
    ${CMAKE_SOURCE_DIR}/SyncEngine.cpp
    ${CMAKE_SOURCE_DIR}/MqttClient.cpp
    ${CMAKE_SOURCE_DIR}/SyncManager.cpp
    ${CMAKE_SOURCE_DIR}/LaunchAgentManager.cpp
)

target_link_libraries(SmartClipAppTest
    PRIVATE
        Qt6::Widgets
        Qt6::Svg
        Qt6::Test
)

if(OpenSSL_FOUND)
    target_link_libraries(SmartClipAppTest PRIVATE OpenSSL::Crypto)
    target_compile_definitions(SmartClipAppTest PRIVATE SMARTCLIP_HAVE_OPENSSL)
endif()

if(Qt6Mqtt_FOUND)
    target_link_libraries(SmartClipAppTest PRIVATE Qt6::Mqtt)
    target_compile_definitions(SmartClipAppTest PRIVATE SMARTCLIP_HAVE_MQTT)
endif()

add_test(NAME SettingsDialogTest COMMAND SettingsDialogTest)
add_test(NAME HistoryManagerTest COMMAND HistoryManagerTest)
add_test(NAME LaunchAgentManagerTest COMMAND LaunchAgentManagerTest)
add_test(NAME SmartClipAppTest COMMAND SmartClipAppTest)
add_test(NAME SyncEngineTest COMMAND SyncEngineTest)
