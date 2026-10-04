# Сборка и установка SmartClip на Linux

Пошаговая инструкция: от зависимостей до рабочего приложения с сетевой
синхронизацией по MQTT. Дистрибутив-пример — **Arch / Manjaro**; команды для
Debian/Ubuntu и Fedora приводятся рядом.

> Общее описание проекта, шифрования и правил слияния — в [`README.md`](../README.md).

---

## Шаг 0. Зависимости (один раз)

**Arch / Manjaro:**

```bash
sudo pacman -S --needed qt6-base qt6-mqtt cmake gcc openssl libsecret
```

**Debian / Ubuntu:**

```bash
sudo apt install qt6-base-dev qt6-mqtt-dev cmake g++ libssl-dev libsecret-1-dev
```

**Fedora:**

```bash
sudo dnf install qt6-qtbase-devel qt6-qtmqtt-devel cmake gcc-c++ openssl-devel libsecret-devel
```

Проверить, что всё на месте:

```bash
pacman -Q qt6-mqtt qt6-base openssl libsecret
```

Что зачем:

| Пакет | Зачем |
|-------|-------|
| `qt6-base` | Qt Widgets — UI приложения (**обязательно**) |
| `qt6-mqtt` | Qt MQTT — **только для** сетевой синхронизации (опционально) |
| `cmake` | Система сборки |
| `gcc` | Компилятор C++17 |
| `openssl` | AES-256-GCM (шифрование истории) |
| `libsecret` | Хранилище ключа шифрования (Secret Service, gnome-keyring / KWallet) |

> **`qt6-mqtt` не обязателен.** Без него приложение собирается и работает —
> просто сетевой синхронизации в бинаре не будет.

---

## Шаг 1. Собрать и поставить приложение

Рабочая ветка — `develop`, всё запушено в Forgejo. На ноуте:

```bash
cd ~/projects/cpp/SmartClip
git switch develop          # если вдруг не на ней
git pull forgejo develop    # подтянуть свежий README/доки (опционально)

cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=$HOME/.local
cmake --build build-linux -j$(nproc)
cmake --install build-linux
```

Либо одной строкой через Makefile:

```bash
make -C ~/projects/cpp/SmartClip install PREFIX=$HOME/.local
```

⚠️ **При configure смотри на строку в конце** — должно быть:

```
-- SmartClip: Qt6::Mqtt найден — сетевая синхронизация включена
```

Если написано `НЕ найден` — синка в бинаре не будет: ставь `qt6-mqtt` и
пересобирай (`rm -rf build-linux`).

Устанавливается:

- бинарь → `~/.local/bin/SmartClip`
- ярлык → `~/.local/share/applications/`
- иконки → `~/.local/share/icons/`

*(Опционально)* portable-сборка **AppImage** (без системного Qt6):

```bash
make -C ~/projects/cpp/SmartClip appimage
# → SmartClip-x86_64.AppImage
```

---

## Шаг 2. Запустить и открыть настройки

```bash
~/.local/bin/SmartClip
```

Либо через меню приложений — пункт **«SmartClip»** (категория «Утилиты»).

Дальше — правый клик по иконке в трее → **Settings**.

---

## Шаг 3. Заполнить блок «Network sync (MQTT)»

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
> **зашифрованными** ключом из системного хранилища (libsecret).

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
`sync-test-123`) — через пару секунд открой меню трея на другом, запись должна
появиться.

Терминалом (по желанию) — что брокер жив:

```bash
# подписка (Ctrl+C для выхода)
mosquitto_sub -h mqtt.halfpi.ru -p 8883 --cafile /etc/ssl/certs/ca-certificates.crt \
  -u smartclip -P '<пароль брокера>' -t 'smartclip/smartclip/state' -v
```

*(второй сегмент топика — это твой **Room**; увидишь base64-шифртекст,
содержимое брокер не видит).*

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

## Особенности Linux

- **Трей** требует DE с реализацией freedesktop.org System Tray
  (StatusNotifier/SNI): GNOME, KDE Plasma, XFCE, Cinnamon и др.
- **Wayland:** приложение **форсирует бэкенд XWayland (`xcb`)** — под нативным
  Wayland Qt отдаёт буфер обмена только при фокусе клавиатуры, а у трей-
  приложения фокуса нет, и новые копии не попадали в историю. Отключить:
  `SMARTCLIP_NO_XCB=1` (или задать свой `QT_QPA_PLATFORM`).
- **Иконка** адаптируется под светлую/тёмную тему панели.
- **Автостарт:** `.desktop`-файл в `~/.config/autostart/smartclip.desktop`
  (включается через Settings → «Launch at startup»).
- **Ключ шифрования** — в Secret Service (gnome-keyring / KWallet).

---

## Обновление и удаление

Обновление — повторить Шаг 1 (старый бинарь перезапишется):

```bash
cd ~/projects/cpp/SmartClip && git pull forgejo develop
make install PREFIX=$HOME/.local
```

Удаление:

```bash
make -C ~/projects/cpp/SmartClip uninstall PREFIX=$HOME/.local
```

---

## Если синк не поднимается

- Глянь лог в консоли, где запускал бинарь (`~/.local/bin/SmartClip`) — там
  будут ошибки MQTT/TLS.
- Проверь порт 8883 снаружи:
  `mosquitto_pub -h mqtt.halfpi.ru -p 8883 --cafile /etc/ssl/certs/ca-certificates.crt -u smartclip -P '<пароль>' -t test -m hi`
- Убедись, что при configure было `Qt6::Mqtt найден` (см. Шаг 1).
