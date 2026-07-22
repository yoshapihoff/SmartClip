# План портирования SmartClip на Linux

## Высокий приоритет (критично для работы)

### 1. LaunchAgentManager: реализовать Linux-автозапуск через XDG Autostart
**Файл:** [`LaunchAgentManager.cpp`](LaunchAgentManager.cpp), [`LaunchAgentManager.h`](LaunchAgentManager.h)

Добавить `#elif defined(Q_OS_LINUX)` ветку в [`applyLaunchAtStartup()`](LaunchAgentManager.cpp:18) — создание/удаление `~/.config/autostart/smartclip.desktop` по XDG Autostart Specification. На Linux не используется `launchctl`, вместо этого создаётся `.desktop`-файл.

### 2. LaunchAgentManager: переименовать `plistPath()` в кроссплатформенное имя
**Файл:** [`LaunchAgentManager.h`](LaunchAgentManager.h), [`LaunchAgentManager.cpp`](LaunchAgentManager.cpp)

Метод `plistPath()` — macOS-специфичное имя. Переименовать в `autostartFilePath()` или добавить отдельный метод для Linux (`desktopFilePath()`), возвращающий путь `~/.config/autostart/smartclip.desktop`.

### 3. CMakeLists.txt: добавить Linux-секцию с install() правилами
**Файл:** [`CMakeLists.txt`](CMakeLists.txt)

Добавить `elseif(LINUX)` блок:
- `include(GNUInstallDirs)`
- `install(TARGETS SmartClip RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})`
- Установка `.desktop` файла в `share/applications/`
- Установка иконок в `share/icons/hicolor/`

### 4. Создать `smartclip.desktop` файл
**Новый файл:** `assets/smartclip.desktop`

Файл для интеграции в DE (меню приложений + автозапуск) в соответствии с freedesktop.org спецификацией.

### 5. Makefile: убрать хардкод QT_PATH
**Файл:** [`Makefile`](Makefile)

- Убрать жёстко прописанный `QT_PATH=/Volumes/HDD/qt/6.5.3/macos`
- Добавить автоопределение Qt6 через `pkg-config` или `qmake6 -query QT_INSTALL_PREFIX`
- Исправить цель `run`: на Linux запускать бинарник напрямую, а не через `open`

---

## Средний приоритет (желательно для качества)

### 6. SmartClipApp.cpp: оценить clipboard polling для Linux
**Файл:** [`SmartClipApp.cpp`](SmartClipApp.cpp:136-141)

На X11 `QClipboard::dataChanged` надёжен. На Wayland могут быть проблемы — оценить необходимость polling'а. Возможно, добавить `#if defined(Q_OS_LINUX)` с polling'ом всегда для надёжности, либо только при обнаружении Wayland.

### 7. SmartClipApp.cpp: проверить updateIcon() в не-macOS ветке
**Файл:** [`SmartClipApp.cpp`](SmartClipApp.cpp:450-473)

Ветка `#else` (не-macOS) уже корректно выбирает tray_white/tray_black в зависимости от тёмной темы. Проверить на GNOME, KDE, XFCE. Проблем с `QSystemTrayIcon` в современных DE ожидается немного (Qt 6.2+ поддерживает StatusNotifier/SNI).

### 8. Тесты LaunchAgentManagerTest: добавить тесты Linux-автозапуска
**Файл:** [`tests/LaunchAgentManagerTest.cpp`](tests/LaunchAgentManagerTest.cpp)

Добавить `#elif defined(Q_OS_LINUX)` блоки в существующие тесты — создание/удаление `.desktop` файла в тестовой временной директории (через подмену `HOME`).

### 9. Тесты SmartClipAppTest: обновить testClipboardPolling
**Файл:** [`tests/SmartClipAppTest.cpp`](tests/SmartClipAppTest.cpp:342-351)

Если на Linux будет добавлен clipboard polling — обновить тест, убрать `QSKIP` или сделать платформно-зависимую проверку.

---

## Низкий приоритет (опционально)

### 10. HelpDialog.cpp: платформно-зависимый текст справки
**Файл:** [`HelpDialog.cpp`](HelpDialog.cpp:34,48)

Заменить `"Hold <b>Ctrl</b> (⌃ on macOS)"` на платформно-зависимый вариант через `#ifdef`. Некритично, т.к. Ctrl работает везде.

### 11. Документация: README с инструкциями для Linux
**Файл:** `README.md` (нужно создать или обновить)

Добавить:
- Зависимости (Qt6, CMake, компилятор)
- Инструкции по сборке: `cmake -B build && cmake --build build`
- Инструкции по установке: `cmake --install build`
- Особенности запуска на Wayland
