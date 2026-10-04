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

> Пошаговые гайды (зависимости → сборка → установка → включение MQTT-синка):
> **[Linux](docs/BUILD-LINUX.md)** · **[macOS](docs/BUILD-MACOS.md)**.

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
- На Wayland приложение **форсирует бэкенд XWayland (`xcb`)**: Qt на нативном
  Wayland отдаёт буфер обмена только при фокусе клавиатуры, а у трей-
  приложения фокуса нет — новые копии не попадали в историю. Под xcb буфер
  читается нормально. Отключить: `SMARTCLIP_NO_XCB=1` (или задать свой
  `QT_QPA_PLATFORM`)
- Иконка в трее адаптируется под тему панели (светлая/тёмная), т.к. Qt на
  Wayland часто не отдаёт `colorScheme` — тема читается через gsettings

## Меню и режимы

Интерфейс приложения — **единый английский** (пункты меню, режимы, поля
настроек, кнопки диалогов). В меню трея три пункта-режима: **Favorite mode**,
**Reveal passwords**, **Comments**. Оформлены **без значков**: включённый режим
помечается штатной галочкой, выключенный — галочки нет (клик по пункту
переключает режим).

Все режимы **одноразовые**: включил → кликнул один элемент истории → действие
применилось → режим сам выключился (галочка снялась). Для режима **Comments**
гашение происходит при закрытии окна ввода — по **OK** **и** по **Cancel**.

Когда все режимы выключены, клик по элементу просто копирует его в буфер.

- **Favorite mode** — клик закрепляет элемент в избранном (рядом появляется
  цветная метка) и повторный проход снимает метку. На macOS и на X11 доступен
  и быстрый путь: `Ctrl`+клик.
- **Reveal passwords** — клик показывает скрытый текст целиком либо, наоборот,
  маскирует его (видны первые и последние символы, остальное — `***`). Скрытые
  элементы маскируются и в самом меню.
- **Comments** — клик открывает небольшое окно с полем ввода; **OK**
  сохраняет заметку, **Cancel** — отбрасывает. Заметка показывается в скобках
  после текста элемента.

Поля в **Settings**: `History size`, `Launch at startup`, `Save history on exit`,
блок **Network sync (MQTT)** (см. ниже), кнопки `OK` / `Cancel`. Подписи заданы
в коде явно — Qt не подменяет их переводом по локали системы.

Та же справка доступна в приложении: **Help** в меню трея.

## Шифрование истории

Содержимое истории буфера **шифруется** одной схемой на обеих платформах:

- **Алгоритм:** AES-256-GCM (реализация OpenSSL EVP). Каждый элемент —
  `nonce(12) || ciphertext || tag(16)`.
- **Ключ** (32 случайных байта) хранится в системном хранилище:
  - **macOS** — Keychain (утилита `security`);
  - **Linux** — Secret Service через libsecret (`secret-tool`,
    gnome-keyring / KWallet).
- **Формат файла** `~/.smartclip/history.yml`: при шифровании `version: 2`,
  элементы пишутся как `text_b64: v2:<base64(blob)>`. Комментарии (режим
  «Комментарии») хранятся так же зашифрованными — поле `comment_b64: v2:…`.
  Старый открытый формат (`version: 1`) читается и **автоматически
  мигрируется** в шифрованный при первом запуске (если доступен ключ).
- Если OpenSSL нет при сборке или хранилище ключей недоступно — приложение
  продолжает работать, но история остаётся в открытом виде (с предупреждением
  в лог).

Зависимости для сборки: OpenSSL dev (`libssl-dev` / `openssl`) и, для Linux,
libsecret (`secret-tool` из пакета `libsecret`).

## Синхронизация по сети (MQTT, опционально)

Обмен историей между устройствами через **MQTT-брокер** в интернете. Фича
**полностью опциональна**: по умолчанию выключена, включается флагом в Settings.

- **Топология — «звезда»**: одно устройство назначается **Master (leading)**,
  остальные — **Slave (follower)**. Роль задаётся в настройках.
- **Топик:** `smartclip/<room>/state` (`room` — из настроек).
- **Шифрование полезной нагрузки:** AES-256-GCM; ключ = `PBKDF2-HMAC-SHA256`
  от общего пароля (`Encryption password`, соль — константа). На брокере виден
  только топик и base64-шифртекст — содержимое скрыто.
- **Идентификация:** `Login`/`Password` — учётка брокера; для различения
  устройств каждое вкладывает в пакет свой `deviceId` (собственное эхо
  игнорируется).

### Правила слияния

- Ключ записи — **текст**.
- `usageCount` — берётся **максимум** из двух версий.
- **Избранное:** избранные **мастера** сохраняют свои цвета как есть; избранные
  ведомого тоже остаются избранными и получают **следующие свободные** цвета.
  Всего поддерживается **32** избранных (цвета 0..31); «лишние» теряют пометку.
- **Скрытие пароля (`mask`) и комментарий** — приоритет у **мастера**; если у
  мастера комментарий пустой — берётся комментарий ведомого.
- **Удаления** синхронны (tombstone'ы): очистка истории «не избранного» на одном
  устройстве убирает эти элементы и на всех клиентах. Повторная копия записи
  «воскрешает» её.
- **History Size** задаёт мастер — его значение назначается всем ведомым.
- После слияния список обрезается по лимиту (сначала удаляются неизбранные,
  с меньшим `usageCount`, затем самые старые).

### Настройки (Settings → Network sync)

`Enable network sync`, `Broker host`, `Port`, `Use TLS`, `Login`, `Password`,
`Encryption password`, `Role` (Master/Slave), `Room`. Пароли брокера и общий
пароль шифрования пишутся в `settings.yml` **зашифрованными** ключом из
системного хранилища.

**Зависимость:** пакет **Qt MQTT** (`qt6-mqtt` в Arch/Manjaro,
`libqt6mqtt6`/`qt6-mqtt-dev` в Debian/Ubuntu). Модуль **опционален**: без него
приложение собирается и работает, синк просто выключен (`─ Qt6::Mqtt` не найден —
предупреждение в конфигурации CMake). Транспорт — `QMqttClient`, брокер может
быть любым (публичный, напр. HiveMQ/EMQX, или свой).

### Свой брокер (пример: mosquitto на VPS)

Проверенная рабочая связка — **mosquitto 2.x** с TLS и парольной авторизацией:

- сертификат Let's Encrypt на поддомен (напр. `mqtt.example.com`),
- слушатель `8883` (MQTT+TLS) с `password_file`,
- при желании — WebSocket-листенер + reverse-proxy на 443 (для сетей, где 8883
  закрыт). **Важно:** `QMqttClient` не умеет MQTT поверх WebSocket нативно, поэтому
  штатный путь — **прямой TLS на 8883**; порт должен быть доступен снаружи.

Поля в Settings: `Broker host` = `mqtt.example.com`, `Port` = `8883`,
`Use TLS` = ✔, `Login`/`Password` — учётка брокера, `Encryption password` —
общий секрет (задаётся одинаково на всех устройствах, **отдельно** от пароля
брокера), `Role` и `Room` — одинаковые для устройств одной группы.

## Тестирование

```bash
# Все тесты
make test

# С подробным выводом
make test-verbose

# Ручной запуск отдельных тестов
cd build && ctest --output-on-failure
./build/tests/LaunchAgentManagerTest
./build/tests/SyncEngineTest
```

Тесты (CTest): `SettingsDialogTest`, `HistoryManagerTest`,
`LaunchAgentManagerTest`, `SmartClipAppTest`, **`SyncEngineTest`** (слияние
master/slave: usage=max, приоритет избранного/цветов/маски/комментария мастера,
лимит 32 избранных, обрезка истории, удаления/tombstone'ы, round-trip сетевых
настроек с шифрованием паролей).

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
├── HistoryManager.cpp/h    # Управление историей буфера (+ tombstone'ы)
├── SyncEngine.cpp/h        # Ядро слияния master/slave (без сети)
├── SyncManager.cpp/h       # Оркестратор синка (MQTT, шифрование, роли)
├── MqttClient.cpp/h        # Обёртка над QMqttClient (опционально)
├── SettingsManager.cpp/h   # Настройки
├── SettingsDialog.cpp/h    # Диалог настроек
├── HelpDialog.cpp/h        # Справочный диалог
├── LaunchAgentManager.cpp/h # Автостарт (macOS/Linux)
└── main.cpp                # Точка входа
```
