cmake_minimum_required(VERSION 3.16)

project(SmartClip LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_add_executable(SmartClip
    main.cpp
    SmartClipApp.cpp
    SettingsManager.cpp
    SettingsDialog.cpp
    HistoryManager.cpp
    LaunchAgentManager.cpp
    SmartClipApp.h
    SettingsManager.h
    SettingsDialog.h
    HistoryManager.h
    LaunchAgentManager.h
    resources.qrc
)

target_link_libraries(SmartClip
    PRIVATE
        Qt6::Widgets
)

if(APPLE)
    set(APP_ICON "${CMAKE_SOURCE_DIR}/icons/SmartClip.icns")
    if(EXISTS "${APP_ICON}")
        set_source_files_properties(${APP_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
        target_sources(SmartClip PRIVATE ${APP_ICON})
        set_target_properties(SmartClip PROPERTIES
            MACOSX_BUNDLE_ICON_FILE "SmartClip.icns"
        )
    endif()

    set_target_properties(SmartClip PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.example.smartclip"
        MACOSX_BUNDLE_BUNDLE_NAME "SmartClip"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/Info.plist"
    )
endif()
