# SmartClip — Clipboard History Manager

Кроссплатформенный менеджер истории буфера обмена, живущий в system tray.
Поддерживает macOS и Linux.

## Возможности

- Сохранение истории буфера обмена в system tray-меню
- Быстрая вставка кликом по элементу
- Избранное: закрепление важных клипов с цветными метками (Ctrl+Click)
- Маскировка паролей/чувствительных данных (Shift+Click)
- Автостарт при входе в систему
- Тёмная/светлая тема иконки (автоопределение)

## Зависимости

| Пакет | Назначение |
|-------|-----------|
| Qt 6.2+ (Widgets) | UI framework |
| CMake 3.16+ | Система сборки |
| Компилятор C++17 | GCC 9+, Clang 10+ |

### Установка зависимостей

**Ubuntu/Debian:**
```bash
sudo apt install qt6-base-dev cmake g++
```

**Fedora:**
```bash
sudo dnf install qt6-qtbase-devel cmake gcc-c++
```

**Arch:**
```bash
sudo pacman -S qt6-base cmake
```

**macOS (Homebrew):**
```bash
brew install qt@6 cmake
```

## Сборка

### Быстрый старт (через Makefile)

```bash
# Сборка (Qt6 определяется автоматически)
make build

# Сборка с ручным указанием Qt
make build QT_PATH=/opt/Qt/6.7.0/gcc_64

# Debug-сборка
make build BUILD_TYPE=Debug
```

### Ручная сборка (CMake)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# При необходимости указать Qt:
cmake -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x.x/gcc_64
cmake --build build
```

## Установка

### Linux

```bash
# Системная установка (требует sudo для /usr/local)
sudo cmake --install build

# Или через Makefile:
sudo make install

# Установка в пользовательскую директорию (без sudo):
cmake -B build -DCMAKE_INSTALL_PREFIX=$HOME/.local
cmake --build build
cmake --install build
```

После установки приложение появится в меню приложений DE (категория «Утилиты»).

### macOS

```bash
# Сборка создаёт .app bundle в build/SmartClip.app
make build
make run  # или: open build/SmartClip.app
```

## Запуск

```bash
# Linux
make run
# или: ./build/SmartClip

# macOS
make run
# или: open build/SmartClip.app
```

## Автостарт

- **macOS**: создаётся LaunchAgent `~/Library/LaunchAgents/com.yoshapihoff.smartclip.plist`
- **Linux**: создаётся `.desktop` файл в `~/.config/autostart/smartclip.desktop` (XDG Autostart)

Настройка переключается через Settings → «Launch at startup».

## Особенности Linux

- Поддерживаются DE с реализацией freedesktop.org System Tray (StatusNotifier/SNI): GNOME, KDE Plasma, XFCE, Cinnamon и др.
- На Wayland используется polling буфера обмена (500ms) для обхода ограничений Qt
- Иконка в трее поддерживает автоопределение тёмной темы (Qt 6.5+)

## Тестирование

```bash
# Все тесты
make test

# С подробным выводом
make test-verbose

# Ручной запуск отдельных тестов
cd build && ctest --output-on-failure
./build/tests/LaunchAgentManagerTest
```

## Структура проекта

```
.
├── CMakeLists.txt          # Основной CMake
├── Makefile                # Удобные цели сборки/тестов/установки
├── assets/
│   └── smartclip.desktop   # Linux: интеграция в DE
├── icons/                  # Иконки (.png, .svg, .icns)
├── tests/                  # Модульные тесты
├── SmartClipApp.cpp/h      # Основная логика приложения
├── HistoryManager.cpp/h    # Управление историей буфера
├── SettingsManager.cpp/h   # Настройки
├── SettingsDialog.cpp/h    # Диалог настроек
├── HelpDialog.cpp/h        # Справочный диалог
├── LaunchAgentManager.cpp/h # Автостарт (macOS/Linux)
└── main.cpp                # Точка входа
```
