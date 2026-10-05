# Сборка и установка SmartClip на macOS

Пошаговая инструкция: от зависимостей до рабочего приложения с сетевой
синхронизацией по MQTT. Пакеты — через **Homebrew**. Результат сборки —
нативный `.app`-бандл.

> Общее описание проекта, шифрования и правил слияния — в [`README.md`](../README.md).
> Для Linux см. [`BUILD-LINUX.md`](BUILD-LINUX.md).

---

## Шаг 0. Зависимости (один раз)

```bash
# Homebrew (если ещё не стоит)
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Зависимости сборки
brew install qt@6 cmake openssl@3
```

Что зачем:

| Пакет | Зачем |
|-------|-------|
| `qt@6` | Qt Widgets + Qt Svg — UI приложения (**обязательно**) |
| `openssl@3` | AES-256-GCM для шифрования истории (**обязательно**, fail-closed) |
| `cmake` | Система сборки |

Нужен ли на macOS `qt6-mqtt`? **Нет.** Метаформула Homebrew `qt` (`qt@6`)
собирает модули Qt **из исходников** и ставит только те, что описаны в
формуле. Пакета `qt6-mqtt` в Homebrew нет, а модуль MQTT `QMqttClient` не
входит в базовый набор (в формуле есть `qtwebsockets`, но не MQTT). Поэтому
`qt@6` из Homebrew даёт **Widgets/Svg, но не Mqtt**.

Приложение спроектировано так, что зависимость **опциональна**: без Qt MQTT
бинарник собирается и полностью работает — просто сетевой синхронизации в нём
не будет (см. ниже, как её включить через Qt Online Installer).

Дополнительно на macOS задействованы системные средства:

- **OpenSSL** (`openssl@3` через Homebrew) — AES-256-GCM для шифрования
  истории и секретов. Это **обязательная зависимость**: без неё сборка
  падает с ошибкой, потому что история буфера обмена обязана храниться
  зашифрованной;
- **Keychain** — для ключа шифрования истории (утилита `security`).

---

## Шаг 1. Собрать приложение

```bash
cd ~/projects/cpp/SmartClip          # или свой клон репы
git switch develop
git pull forgejo develop             # опционально: свежие доки

cmake -S . -B build-macos -DCMAKE_BUILD_TYPE=Release
cmake --build build-macos -j$(sysctl -n hw.ncpu)
```

Либо через Makefile (сам определит платформу и создаст `build-macos`):

```bash
make -C ~/projects/cpp/SmartClip macos-build
```

При успешной конфигурации CMake выводит строку про MQTT:

```
-- SmartClip: Qt6::Mqtt найден — сетевая синхронизация включена
```

или

```
-- SmartClip: Qt6::Mqtt НЕ найден — сетевая синхронизация недоступна
```

Если Qt не нашёлся автоматически — укажи префикс явно (путь из `brew --prefix qt@6`):

```bash
cmake -S . -B build-macos -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH="$(brew --prefix qt@6)"
cmake --build build-macos -j$(sysctl -n hw.ncpu)
```

Результат — **`build-macos/SmartClip.app`**.

---

## Шаг 2. Запустить приложение

```bash
make -C ~/projects/cpp/SmartClip macos-run
# или просто:
open ~/projects/cpp/SmartClip/build-macos/SmartClip.app
```

Приложение живёт в трее (menu bar); иконка в Dock не отображается —
это трей-приложение (`LSUIElement` в `Info.plist`). Дальше — клик по иконке в
menu bar → **Settings**.

### Установка в /Applications

```bash
cp -R build-macos/SmartClip.app /Applications/
```

---

## Шаг 3. Заполнить блок «Network sync (MQTT)»

*(Если синка в бинаре есть — см. Шаг 1.)*

| Поле | Значение |
|---|---|
| ☑ **Enable network sync** | поставить галочку |
| **Broker host** | `mqtt.halfpi.ru` |
| **Port** | `8883` |
| ☑ **Use TLS** | поставить галочку |
| **Login** | `smartclip` |
| **Password** | пароль брокера *(см. `memory/projects/mserver.md`)* |
| **Encryption password** | **придумай свой** (например `leshka-love-cats-2026`) |
| **Role** | `Master (leading)` — на этом устройстве |
| **Room** | `smartclip` (или своё слово — одинаковое на всех) |

Жми **OK**. Настройки применяются сразу, синк перезапускается.

> Пароли (брокера и общий пароль шифрования) пишутся в `settings.yml`
> **зашифрованными** ключом из Keychain.

---

## Шаг 4. Второе устройство

Повтори шаги 1–3, но:

- **Role** → `Slave (follower)` — мастер в группе **ровно один**;
- **Encryption password** — **точно тот же**, что на мастере;
- **Room** — **точно тот же**;
- Login/Password брокера — тоже те же.

---

## Шаг 5. Проверить, что работает

Просто: на одном устройстве скопируй что-нибудь уникальное (например
`sync-test-123`) — через пару секунд открой меню в menu bar на другом, запись
должна появиться.

Терминалом (по желанию) — что брокер жив:

```bash
# подписка (Ctrl+C для выхода); нужен клиент mosquitto
brew install mosquitto
mosquitto_sub -h mqtt.halfpi.ru -p 8883 --cafile "$(brew --prefix)/etc/openssl@3/cert.pem" \
  -u smartclip -P '<пароль брокера>' -t 'smartclip/smartclip/state' -v
```

*(второй сегмент топика — это твой **Room**; увидишь base64-шифртекст,
содержимое брокер не видит).*

---

## Включить MQTT-синк на macOS (опционально)

> **Старый macOS (Monterey и ниже) / Qt 6.5.x:** официальный Qt-инсталлятор для
> ветки 6.5 **не предлагает модуль Qt MQTT** (для `mac_x64/…/qt6_653` пакета
> `…addons.qtmqtt` нет). Поэтому модуль собирается **из исходников** против
> установленного Qt 6.5.3 — см. Путь C ниже. API, который использует SmartClip,
> в 6.5.3 присутствует (проверено).
>
> ⚠️ **Бери ТЕГ, а не ветку.** Ветка `6.5` содержит уже 6.5.10 — CMake потребует
> Qt 6.5.10 и упадёт на версии. Нужен именно тег `v6.5.3` (или коммит с
> `QT_REPO_MODULE_VERSION "6.5.3"`). Номер версии модуля обязан **точно
> совпадать** с твоим Qt (6.5.3 ↔ 6.5.3).

Если нужна именно сетевая синхронизация на Mac, `qt@6` из Homebrew не подойдёт —
модуля MQTT в нём нет. Два пути:

**Путь A — Qt Online Installer (рекомендуется).**
1. Скачай Qt Online Installer → <https://www.qt.io/download-qt-installer>.
2. Установи Qt 6.x (например `6.11`) **с галочкой `Qt MQTT`** (и базовыми
   `Qt Widgets`, `Qt Svg`).
3. Пересобери, указав префикс этого Qt:

```bash
cmake -S . -B build-macos -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH="$HOME/Qt/6.11.0/macos"
cmake --build build-macos -j$(sysctl -n hw.ncpu)
```

В выводе configure должно появиться `Qt6::Mqtt найден`.

**Путь B — собрать Qt MQTT из исходников (для Qt 6.11 и т.п.).**
```bash
git clone --branch v6.11.0 git://code.qt.io/qt/qtmqtt.git
cmake -S qtmqtt -B qtmqtt/build -DCMAKE_PREFIX_PATH="$(brew --prefix qt@6)"
cmake --build qtmqtt/build
cmake --install qtmqtt/build
```

**Путь C — для Qt 6.5.3 (старый macOS, напр. Monterey).**
Ветка `6.5` модуля поддерживается до `v6.5.10-lts-lgpl`. Собираем против
твоего Qt 6.5.3:

```bash
# Путь к Qt 6.5.3 (Online Installer обычно ~/Qt/6.5.3/macos; у Лёши —
# /Volumes/HDD/qt/6.5.3/macos).
QTP=/Volumes/HDD/qt/6.5.3/macos

git clone https://github.com/qt/qtmqtt.git
cd qtmqtt
git fetch --tags
git checkout v6.5.3          # ИМЕННО тег: версия модуля = 6.5.3
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QTP"
cmake --build build -j$(sysctl -n hw.ncpu)
cmake --install build        # в тот же префикс Qt (может понадобиться sudo)
```

В офлайне (приватный форк) то же самое: `code.qt.io/qt/qtmqtt.git`.

Затем пересобери SmartClip (см. Шаг 1) — в конфигурации должно появиться
`Qt6::Mqtt найден`. Если ставил модуль в **отдельный** префикс, добавь и его в
`CMAKE_PREFIX_PATH`: `-DCMAKE_PREFIX_PATH="$QTP;/path/to/mqtt/prefix"`.

После любого из путей `find_package(Qt6 COMPONENTS Mqtt)` в проекте найдёт модуль.

---

## Памятка по поведению

- Фича **опциональна**: без галочки Enable ничего никуда не ходит.
- **Master авторитетен**: его History Size назначается всем ведомым; избранное
  сохраняет свои цвета. У ведомых избранное тоже остаётся, но получает другие
  свободные цвета (всего **32**).
- `usageCount` берётся **максимумом**; удаления синхронятся в обе стороны
  (повторная копия «воскрешает» запись).
- Комментарий и маска пароля — **приоритет мастера**, **кроме** случая, когда
  у мастера комментарий пустой (тогда берётся комментарий ведомого).

---

## Особенности macOS

- **Приложение трей-типа:** без иконки в Dock (`LSUIElement=true`), меню — в
  menu bar.
- **Автостарт:** LaunchAgent
  `~/Library/LaunchAgents/com.yoshapihoff.smartclip.plist` (включается через
  Settings → «Launch at startup»).
- **Ключ шифрования** — в Keychain; AES-256-GCM — через OpenSSL (`openssl@3`).
- **Тема иконки** (светлая/тёмная) определяется автоматически.
- **Ctrl+клик по элементу** закрепляет его в избранном (быстрый путь).

---

## Обновление и удаление

Обновление — повторить Шаг 1 (пересобрать `build-macos/SmartClip.app`):

```bash
cd ~/projects/cpp/SmartClip && git pull forgejo develop
make macos-build
```

Удаление:

```bash
rm -rf /Applications/SmartClip.app
rm -f ~/Library/LaunchAgents/com.yoshapihoff.smartclip.plist   # если включён автостарт
```

---

## Если синк не поднимается

- **Сначала глянь статус прямо в приложении:** Settings → Network sync →
  строка **Connection status** + кнопка **«Проверить…»**. Если написано
  «Недоступно: сборка без модуля Qt6::Mqtt» — бинарь собран без Qt MQTT
  (см. раздел выше про Homebrew/Online Installer), синка в нём нет.
- Запусти приложение из терминала, чтобы видеть лог:
  `~/projects/cpp/SmartClip/build-macos/SmartClip.app/Contents/MacOS/SmartClip`
  — там будут ошибки MQTT/TLS.
- Проверь порт 8883 снаружи:
  `mosquitto_pub -h mqtt.halfpi.ru -p 8883 --cafile "$(brew --prefix)/etc/openssl@3/cert.pem" -u smartclip -P '<пароль>' -t test -m hi`
- Убедись, что при configure было `Qt6::Mqtt найден` (иначе см. раздел выше).
