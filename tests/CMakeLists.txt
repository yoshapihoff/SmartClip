cmake_minimum_required(VERSION 3.16)

project(SmartClipTests)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Test)

include_directories(${CMAKE_SOURCE_DIR})

qt_add_executable(SettingsDialogTest
    SettingsDialogTest.cpp
    ${CMAKE_SOURCE_DIR}/SettingsDialog.cpp
    ${CMAKE_SOURCE_DIR}/SettingsManager.cpp
)

target_link_libraries(SettingsDialogTest
    PRIVATE
        Qt6::Widgets
        Qt6::Test
)

qt_add_executable(HistoryManagerTest
    HistoryManagerTest.cpp
    ${CMAKE_SOURCE_DIR}/HistoryManager.cpp
)

target_link_libraries(HistoryManagerTest
    PRIVATE
        Qt6::Test
)

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
    ${CMAKE_SOURCE_DIR}/SettingsManager.cpp
    ${CMAKE_SOURCE_DIR}/SettingsDialog.cpp
    ${CMAKE_SOURCE_DIR}/HistoryManager.cpp
    ${CMAKE_SOURCE_DIR}/LaunchAgentManager.cpp
)

target_link_libraries(SmartClipAppTest
    PRIVATE
        Qt6::Widgets
        Qt6::Test
)

add_test(NAME SettingsDialogTest COMMAND SettingsDialogTest)
add_test(NAME HistoryManagerTest COMMAND HistoryManagerTest)
add_test(NAME LaunchAgentManagerTest COMMAND LaunchAgentManagerTest)
add_test(NAME SmartClipAppTest COMMAND SmartClipAppTest)
