cmake_minimum_required(VERSION 3.16)

project(SmartClip LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Svg)

# AES-256-GCM для шифрования истории. Ищем OpenSSL; если нет —
# SMARTCLIP_HAVE_OPENSSL не определяем, и приложение работает без шифрования
# (данные остаются в открытом виде, но сборка не падает).
find_package(OpenSSL QUIET)

qt_add_executable(SmartClip
    main.cpp
    SmartClipApp.cpp
    SettingsManager.cpp
    HelpDialog.cpp
    SettingsDialog.cpp
    HistoryManager.cpp
    Crypto.cpp
    SyncEngine.cpp
    MqttClient.cpp
    SyncManager.cpp
    LaunchAgentManager.cpp
    SmartClipApp.h
    SettingsManager.h
    HelpDialog.h
    SettingsDialog.h
    HistoryManager.h
    Crypto.h
    SyncEngine.h
    MqttClient.h
    SyncManager.h
    LaunchAgentManager.h
    resources.qrc
)

target_link_libraries(SmartClip
    PRIVATE
        Qt6::Widgets
        Qt6::Svg
)

if(OpenSSL_FOUND)
    target_link_libraries(SmartClip PRIVATE OpenSSL::Crypto)
    target_compile_definitions(SmartClip PRIVATE SMARTCLIP_HAVE_OPENSSL)
    message(STATUS "SmartClip: OpenSSL найден — шифрование истории включено")
else()
    message(WARNING "SmartClip: OpenSSL НЕ найден — шифрование истории недоступно")
endif()

# Сетевая синхронизация через MQTT (Qt6::Mqtt). Модуль опционален: без него
# приложение собирается, а синк просто выключен (MqttClient::available()==false).
find_package(Qt6 QUIET COMPONENTS Mqtt)
if(Qt6Mqtt_FOUND)
    target_link_libraries(SmartClip PRIVATE Qt6::Mqtt)
    target_compile_definitions(SmartClip PRIVATE SMARTCLIP_HAVE_MQTT)
    message(STATUS "SmartClip: Qt6::Mqtt найден — сетевая синхронизация включена")
else()
    message(WARNING "SmartClip: Qt6::Mqtt НЕ найден — сетевая синхронизация недоступна (установите qt6-mqtt)")
endif()

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
elseif(UNIX AND NOT APPLE)
    # ── Linux install rules ──
    include(GNUInstallDirs)

    install(TARGETS SmartClip
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )

    install(FILES assets/smartclip.desktop
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications
    )

    # Install icon in hicolor theme
    install(FILES icons/icon.png
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/512x512/apps
        RENAME smartclip.png
    )

    # Install tray icons used by application at runtime (via Qt resource system they're embedded,
    # but we provide them for packaging completeness)
    install(FILES icons/tray_black.svg
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps
        RENAME smartclip.svg
    )
endif()

enable_testing()
add_subdirectory(tests)
