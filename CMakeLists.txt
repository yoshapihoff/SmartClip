cmake_minimum_required(VERSION 3.16)

# ── Версия приложения ────────────────────────────────────────────────────
# Единственный источник правды — файл VERSION в корне проекта (например «1.0.0»).
# Читаем ДО project(), чтобы передать значение в project(VERSION ...) и потому в
# CMake-переменную PROJECT_VERSION. Схема версионирования — в README (раздел
# «Версионирование»), бамп — scripts/bump-version.sh (patch по умолчанию).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" SMARTCLIP_VERSION_RAW LIMIT_COUNT 1)
    string(STRIP "${SMARTCLIP_VERSION_RAW}" SMARTCLIP_VERSION)
else()
    set(SMARTCLIP_VERSION "0.0.0")
    message(WARNING "SmartClip: файл VERSION не найден — версия по умолчанию ${SMARTCLIP_VERSION}")
endif()

project(SmartClip VERSION ${SMARTCLIP_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Svg)

# AES-256-GCM для шифрования истории — ЖЁСТКАЯ зависимость.
# История буфера обмена ОБЯЗАНА храниться зашифрованной, поэтому OpenSSL
# требуется: без него сборка падает с понятной ошибкой, а не «тихо» работает
# без шифрования (fail-closed вместо fail-open).
find_package(OpenSSL REQUIRED)

qt_add_executable(SmartClip
    main.cpp
    SmartClipApp.cpp
    SettingsManager.cpp
    HelpDialog.cpp
    AboutDialog.cpp
    SettingsDialog.cpp
    HistoryManager.cpp
    Crypto.cpp
    SyncEngine.cpp
    MqttClient.cpp
    SyncManager.cpp
    LaunchAgentManager.cpp
    TrayPopup.cpp
    Version.h
    SmartClipApp.h
    SettingsManager.h
    HelpDialog.h
    AboutDialog.h
    SettingsDialog.h
    HistoryManager.h
    Crypto.h
    SyncEngine.h
    MqttClient.h
    SyncManager.h
    LaunchAgentManager.h
    TrayPopup.h
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
endif()

# Прокидываем версию в код (Version.h → SMARTCLIP_VERSION_STRING) и показываем в логе.
target_compile_definitions(SmartClip PRIVATE SMARTCLIP_VERSION="${PROJECT_VERSION}")
message(STATUS "SmartClip: версия ${PROJECT_VERSION}")

# Сетевая синхронизация через MQTT (Qt6::Mqtt). Модуль опционален: без него
# приложение собирается, а синк просто выключен (MqttClient::available()==false).
find_package(Qt6 QUIET COMPONENTS Mqtt Network)
if(Qt6Mqtt_FOUND)
    target_link_libraries(SmartClip PRIVATE Qt6::Mqtt)
    target_compile_definitions(SmartClip PRIVATE SMARTCLIP_HAVE_MQTT)
    # Qt6::Network нужен для мониторинга доступности сети (QNetworkInformation),
    # чтобы мгновенно переподключаться, когда интернет вернулся.
    if(Qt6Network_FOUND)
        target_link_libraries(SmartClip PRIVATE Qt6::Network)
        target_compile_definitions(SmartClip PRIVATE SMARTCLIP_HAVE_NETWORKINFO)
    endif()
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
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/Info.plist"
        # При установке/упаковке Qt ищется внутри бандла. Build-tree rpath НЕ
        # трогаем — чтобы неупакованный .app запускался против системного Qt.
        # Окончательная починка rpath для --bundle — в scripts/build-macos.sh
        # (санитария после macdeployqt).
        INSTALL_RPATH "@executable_path/../Frameworks"
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
