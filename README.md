# SmartClip — Clipboard History Manager

Кроссплатформенный менеджер истории буфера обмена, живущий в system tray.
Поддерживает macOS и Linux.

## About

- **Название:** SmartClip — Clipboard History Manager
- **Версия:** 1.0.16 (актуальное значение — в файле [`VERSION`](VERSION))
- **Автор:** Aleksey Zhmikhov ([@yoshapihoff](https://github.com/yoshapihoff))
- **Лицензия:** [MIT](LICENSE)

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

## Сборка и установка

Все сценарии — в каталоге **[`scripts/`](scripts/)**. Пошаговые гайды по
зависимостям: **[Linux](docs/BUILD-LINUX.md)** · **[macOS](docs/BUILD-MACOS.md)**.

| Скрипт | Что делает |
|--------|------------|
| `scripts/build-linux.sh` | Сборка под Linux (+ `--install`) |
| `scripts/install-linux.sh` | Сборка + установка из исходников (в `~/.local` или `/usr/local`) |
| `scripts/uninstall-linux.sh` | Удаление установленного приложения |
| `scripts/build-appimage.sh` | Портативный **AppImage** (Qt6/OpenSSL внутрь, без системного Qt) |
| `scripts/build-macos.sh` | Сборка **SmartClip.app** на macOS (+ `--bundle`) |

### Linux — AppImage (портативно, без системного Qt6)

```bash
scripts/build-appimage.sh            # скачает linuxdeploy/appimagetool и соберёт
# → dist/SmartClip-x86_64.AppImage   (Qt6/OpenSSL/MQTT/libsecret внутри)
```

Готовые артефакты складываются в **`dist/`** (в корне — ничего не litter'ится,
AppDir живёт в `build-linux/`). Путь можно переопределить: `--out FILE`.

Требуется только `curl`, `file`, `patchelf`; инструменты упаковки скрипт
скачивает сам в `~/.cache/smartclip-tools`. Приложение запускается на любом
дистрибутиве без установки Qt.

### Linux — из исходников

```bash
# Собрать + установить в ~/.local (без sudo):
scripts/install-linux.sh

# Системно:
sudo scripts/install-linux.sh --prefix /usr/local

# Только сборка:
scripts/build-linux.sh
```

Ставится: бинарь → `<prefix>/bin/SmartClip`, ярлык → `share/applications/`,
иконки → `share/icons/`. Удаление: `scripts/uninstall-linux.sh --prefix …`.

Зависимости сборки — `qt6-base`, `openssl` (**обязательно**), `cmake`, компилятор;
для синхронизации — `qt6-mqtt`; для ключа шифрования — `libsecret` (Linux).

### macOS — сборка приложения

```bash
scripts/build-macos.sh            # → build-macos/SmartClip.app
scripts/build-macos.sh --bundle   # + macdeployqt: Qt/OpenSSL внутрь .app
scripts/build-macos.sh --bundle --dist   # + копия в dist/SmartClip.app
```

**Зависимости для сборки:** `brew install qt@6 cmake openssl@3`.
**Для запуска** собранного приложения нужны Qt6 (Core/Gui/Widgets/Svg),
OpenSSL 3 и, при использовании синхронизации, Qt6 MQTT (см.
[docs/BUILD-MACOS.md](docs/BUILD-MACOS.md)). С `--bundle` Qt/OpenSSL кладутся
внутрь `.app`, и отдельная установка Qt на машине не нужна.

### Через Makefile (обёртки)

```bash
make build                 # сборка (Qt6 определяется автоматически)
make build QT_PATH=/opt/Qt/6.7.0/gcc_64
make build BUILD_TYPE=Debug
make install PREFIX=$HOME/.local   # или: sudo make install
make appimage              # → dist/SmartClip-x86_64.AppImage
make dist                  # то же + показать артефакты в dist/
make bundle-macos          # macOS: .app с Qt/OpenSSL → dist/
make test                  # тесты (offscreen)
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
настроек, кнопки диалогов). В меню трея четыре пункта-режима: **Favorite mode**,
**Reveal passwords**, **Comments**, **Delete mode**. Оформлены **без значков**:
включённый режим помечается штатной галочкой, выключенный — галочки нет (клик по
пункту переключает режим).

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
  сохраняет заметку, **Cancel** — отбрасывает. Заметка показывается после
  текста элемента через **тире** (`— комментарий`).
- **Delete mode** — клик удаляет элемент из списка (режим одноразовый).
  Удаление ставит tombstone и уезжает на другие устройства через синк —
  **в обе стороны**, без приоритетов ведущий/ведомый.

Поля в **Settings**: `History size`, `Launch at startup`, `Save history on exit`,
блок **Network sync (MQTT)** (см. ниже), кнопки `OK` / `Cancel`. Подписи заданы
в коде явно — Qt не подменяет их переводом по локали системы.

Та же справка доступна в приложении: **Help** в меню трея. Рядом — пункт
**About**: небольшое окно с иконкой, названием, версией, автором и лицензией.
Оба пункта есть и в меню трея, и в футере попапа.

## Шифрование истории

Содержимое истории буфера **всегда шифруется** — одной схемой на обеих
платформах. Открытым текстом история **не пишется и не читается никогда**
(fail-closed).

- **Алгоритм:** AES-256-GCM (реализация OpenSSL EVP). Каждый элемент —
  `nonce(12) || ciphertext || tag(16)`.
- **Ключ** (32 случайных байта) хранится в системном хранилище:
  - **macOS** — Keychain (утилита `security`);
  - **Linux** — Secret Service через libsecret (`secret-tool`,
    gnome-keyring / KWallet).
- **Формат файла** `~/.smartclip/history.yml`: `version: 3`, элементы пишутся
  как `text_b64: v2:<base64(blob)>`. Комментарии и метки правки полей
  (LWW-синхронизация) хранятся так же. Файл сохраняется с правами **0600**.
- **Строгая гарантия (fail-closed):**
  - **OpenSSL — обязательная зависимость сборки.** Без него конфигурация
    CMake падает с ошибкой (не «тихая» сборка без шифрования).
  - Если системное хранилище ключа недоступно — приложение **не сохраняет и
    не читает** историю (а не пишет её открытым текстом). В лог идёт явное
    предупреждение. Параметры синка (пароль брокера, общий пароль) в этом
    случае тоже **не сохраняются**.
  - Генератор ключей — только CSPRNG OpenSSL; предсказуемого фолбэка нет
    (при сбое генерации ключ не создаётся).
- Старый открытый формат (`version: 1`) читается **разово** и автоматически
  **мигрируется** в шифрованный при первом запуске (если доступен ключ) —
  чтобы апгрейд не терял уже накопленную историю.

Зависимости для сборки: **OpenSSL dev обязателен** (`libssl-dev` / `openssl`
/ `openssl@3`), и для Linux — libsecret (`secret-tool` из пакета `libsecret`).

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
- **Доставка:** публикация с **QoS 1** (at-least-once) и **retain** — брокер
  хранит последнее состояние и отдаёт его новому подписчику сразу при connect,
  так что новый/отставший клиент догоняет без ожидания периодического announce.

### Правила слияния

- Ключ записи — **текст**.
- `usageCount` — берётся **максимум** из двух версий; `addedAtMs` — максимум.
- **Поля `mask` / `comment` / «в избранном» сливаются по Last-Writer-Wins.**
  Каждое поле несёт *метку времени последней правки* (`maskChangedAtMs`,
  `commentChangedAtMs`, `favChangedAtMs` в `history.yml`; в вайр-пакете —
  `mc`/`pc`/`fc`). При слиянии побеждает версия с большей меткой — **в любую
  сторону**, т.е. снятие/добавление избранного, очистка комментария и
  тумблер «скрыть пароль» доезжают до всех устройств.
  - **Метка `0` = поле ни разу не меняли** → действует старое легаси-правило:
    приоритет ведущего; пустой комментарий ведущего **не** затирает непустой
    ведомого; избранное — объединение обеих сторон.
  - **Равные ненулевые метки** → побеждает ведущий (детерминированно).
- **Избранное:** LWW решает только «в избранном или нет». Конкретный **цвет
  раздаётся централизованно**: цвета избранного ведущего сохраняются, избранным
  ведомого достаются **следующие свободные** цвета. Всего поддерживается **32**
  избранных (цвета 0..31); «лишние» теряют пометку.
- **Удаления** синхронны (tombstone'ы): очистка истории «не избранного» на одном
  устройстве убирает эти элементы и на всех клиентах. Повторная копия записи
  «воскрешает» её.
- **Приватность на диске:** `~/.smartclip/history.yml` и `settings.yml`
  сохраняются с правами **0600** (только владелец); содержимое истории всегда
  зашифровано, а секреты синка — зашифрованы ключом из keyring.
- **History Size** задаёт мастер — его значение назначается всем ведомым.
- После слияния список обрезается по лимиту (сначала удаляются неизбранные,
  с меньшим `usageCount`, затем самые старые).

> **Совместимость:** правки полей полагаются на метки времени, которых нет в
> старых сборках. При смешанном кластере (часть устройств со старой версией)
> старый пир при ретрансляции теряет метки, и живой клиент может откатить
> правку. **Обновляйте все устройства разом.**

### Настройки (Settings → Network sync)

`Enable network sync`, `Broker host`, `Port`, `Use TLS`, `Login`, `Password`,
`Encryption password`, `Role` (Master/Slave), `Room`, а также строка
**Connection status** с живым индикатором (Подключение… / Подключено /
Ошибка: …) и кнопкой **«Проверить…»** — подключается к брокеру, не закрывая
диалог. Пароли брокера и общий пароль шифрования пишутся в `settings.yml`
**зашифрованными** ключом из системного хранилища.

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

## Версионирование

Версия хранится в файле **`VERSION`** в корне проекта — это единственный
источник правды. Формат — `MAJOR.MINOR.PATCH` (SemVer). Текущую версию
смотри в самом файле `VERSION` (стартовая была **1.0.0**).

Схема:

- **PATCH** (третья цифра) — инкрементится автоматически на **каждый обычный
  коммит**: `1.0.0 → 1.0.1 → 1.0.2 → …`
- **MINOR** (средняя цифра) и **MAJOR** (старшая) — меняются **только по явному
  указанию** (вручную или переменной окружения), когда это понадобится:
  `1.0.5 → 1.1.0` (minor), `1.4.2 → 2.0.0` (major).

Как это работает технически: CMake читает `VERSION` до `project()`, поэтому
`PROJECT_VERSION` совпадает с файлом; версия прокидывается в код через
`SMARTCLIP_VERSION` (см. `Version.h`), показывается в окне **About** и в
подсказке трея, и задаёт `CFBundleShortVersionString`/`CFBundleVersion` для
macOS-бандла.

### Авто-бамп на коммитах (git-хук)

В репозитории лежит хук `.githooks/pre-commit`, который перед каждым коммитом
инкрементит PATCH и добавляет `VERSION` в коммит. Включить один раз:

```bash
make hooks        # git config core.hooksPath .githooks
git config core.hooksPath .githooks   # то же вручную
```

Управление (переменные окружения для конкретного коммита):

```bash
SMARTCLIP_NO_BUMP=1 git commit ...        # не бампать этот коммит
git commit ...                            # обычный коммит -> patch
SMARTCLIP_BUMP=minor git commit ...       # поднять среднюю цифру
SMARTCLIP_BUMP=major git commit ...       # поднять старшую цифру
```

### Ручной бамп

```bash
make bump              # patch: 1.0.0 -> 1.0.1
make bump PART=minor   # minor: 1.0.0 -> 1.1.0
make bump PART=major   # major: 1.0.0 -> 2.0.0

# или напрямую:
scripts/bump-version.sh            # patch
scripts/bump-version.sh minor      # 1.1.0
scripts/bump-version.sh major      # 2.0.0
scripts/bump-version.sh --set 1.2.3
```

> Хук срабатывает только там, где включён `core.hooksPath` (это локальная
> настройка). Поэтому схемы совместимы: где хуки не включены — версия бампается
> вручную (`make bump`).

## Релизные артефакты

Артефакты собираются **в папке `dist/release-<version>/`**, где `<version>`
берётся из файла `VERSION`. То есть обновил версию (например, patch-бампом на
коммите) — собрал релиз — получил комплект с этой версией в имени и в метаданных.

| Платформа | Артефакт | Что это |
|-----------|----------|---------|
| Linux | `SmartClip-<ver>-x86_64.AppImage` | «образ»: самодостаточный, без установки и без системного Qt |
| Linux | `smartclip_<ver>_amd64.deb` | инсталлятор Debian/Ubuntu (`dpkg -i`) |
| Linux | `SmartClip-<ver>-linux-x86_64.tar.gz` | портативный архив (бинарник + .desktop + иконка) |
| macOS | `SmartClip-<ver>-macos-<arch>.zip` | `.app` со **вложенными** Qt/OpenSSL (запуск без доп. установок) |
| macOS | `SmartClip-<ver>-macos-<arch>.dmg` | то же в виде образa-установщика (если доступен `hdiutil`) |
| обе | `SHA256SUMS` | контрольные суммы |

### Linux

```bash
make release-linux        # или: scripts/release-linux.sh
# → dist/release-<version>/…
```

Что делает: собирает бинарник (Release) → AppImage → `.deb` → `.tar.gz` →
`SHA256SUMS`. AppImage паковывается `linuxdeploy`+`linuxdeploy-plugin-qt`
(скачиваются в кэш автоматически).

Отдельные цели: `make appimage`, `make deb` (только `.deb` из готового бинарника).

> **Стек:** Qt6 (Widgets/Svg/Network/DBus; MQTT — опционально) + OpenSSL 3.
> AppImage несёт зависимости внутри. `.deb` ставит бинарник в `/usr/bin` и
> тянет системный Qt6/OpenSSL (`Depends`). Для «дистрибутивного» `.deb` лучше
> собирать на Debian/Ubuntu (или в CI-образе с системным Qt6).

### macOS (требует macOS)

```bash
make release-macos        # или: scripts/release-macos.sh   (только на macOS)
```

`.app` с вложенными библиотеками собирается через `macdeployqt` с последующей
санитарией `rpath` и ad-hoc подписью — запускается на машине **без** Qt/OpenSSL.
Далее — `.zip` через `ditto` и `.dmg` через `hdiutil`.

> **Почему только на macOS:** `macdeployqt`, `otool`, `install_name_tool`,
> `codesign`, `hdiutil` — это инструменты Apple. На Linux `.app` с вложенными
> dylib не собрать. Варианты: локально на маке **или** macOS-раннер в CI.

### CI

Сборка привязана к версии и запускается на push в `main`
(т.е. на каждый patch-бамп, см. «Версионирование»):

- **Linux** — `.forgejo/workflows/release.yml` (self-hosted Forgejo Actions)
  и `.github/workflows/release.yml` (ubuntu-24.04).
- **macOS** — `.github/workflows/release.yml` (раннер `macos-14`; brew Qt6+OpenSSL,
  затем `scripts/release-macos.sh`).
- Оба варианта прикрепляют артефакты к релизу с тегом `v<version>`
  (Linux — в Forgejo, macOS — в GitHub).

> **QtMqtt:** ни в `qt6-base-dev`, ни в brew-формуле `qt` модуля Mqtt нет.
> В CI он ставится best-effort (Linux `qt6-mqtt-dev`) или через секрет
> `QT_MACOS_PREFIX` (путь к Qt Online Installer с модулем Mqtt). Без него
> приложение собирается и работает, просто без сетевой синхронизации.

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
master/slave: usage=max, LWW по меткам правки для mask/comment/favorite,
приоритет избранного/цветов мастера и легаси-правило при метке 0,
лимит 32 избранных, обрезка истории, удаления/tombstone'ы, round-trip сетевых
настроек с шифрованием паролей).

## Структура проекта

```
.
├── CMakeLists.txt          # Основной CMake
├── LICENSE                 # Лицензия (MIT)
├── VERSION                 # Версия приложения (единственный источник правды)
├── Version.h               # C++-обёртка над версией (SMARTCLIP_VERSION_STRING)
├── .githooks/pre-commit    # Авто-бамп patch-версии перед коммитом
├── Makefile                # Удобные цели (обёртки над scripts/)
├── scripts/                # Сборка/установка/упаковка (build-linux, install-linux,
│                           #   uninstall-linux, build-appimage, build-macos,
│                           #   bump-version.sh, lib.sh)
├── docs/                   # Гайды по сборке (BUILD-LINUX/MACOS)
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
├── AboutDialog.cpp/h       # Окно «О программе» (название/версия/автор)
├── LaunchAgentManager.cpp/h # Автостарт (macOS/Linux)
└── main.cpp                # Точка входа
```
