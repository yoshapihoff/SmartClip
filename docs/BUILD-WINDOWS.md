# Сборка SmartClip под Windows (MinGW-w64)

Пошаговый гайд: как из исходников получить `SmartClip.exe` и портативный
ZIP-набор со всеми зависимостями. Целевая версия Qt — **6.11.2** (та же, что на
Linux), тулчейн — **MinGW-w64 (x64)**.

> **Кратко.** Поставь Qt 6.11.2 (MinGW 64-bit) с модулем **Qt MQTT**, собери
> OpenSSL 3.x тем же MinGW, затем `scripts/build-windows.sh` (или
> `scripts/release-windows.sh` для ZIP-набора). Сборка и тесты — на тебе.

---

## 1. Что нужно

| Компонент | Версия | Откуда |
|-----------|--------|--------|
| Qt 6.11.2 (MinGW 64-bit) + **Qt MQTT** | 6.11.2 | [Qt Online Installer](https://www.qt.io/download-qt-installer) |
| MinGW-w64 (x64) | входит в Qt | тот же инсталлятор («Developer and Designer Tools» → MinGW) |
| OpenSSL | 3.x | собираем из исходников (см. §3) |
| CMake | ≥ 3.16 | входит в Qt («Developer and Designer Tools» → CMake) |

### Установка Qt

1. Скачай Qt Online Installer, запусти, войди/создай бесплатный аккаунт.
2. **Select components** → раскрой ветку **Qt → Qt 6.11.2**:
   - **MinGW 64-bit** (основной набор),
   - **Qt MQTT** (нужен для сетевой синхронизации),
   - **Qt Shader Tools** / **Qt SVG** / **Qt Image Formats** — по вкусу.
3. В **Developer and Designer Tools** отметь **MinGW 13.x 64-bit** и **CMake**.
4. Ставь в путь без пробелов, например `C:\Qt`. Тогда Qt окажется в
   `C:\Qt\6.11.2\mingw_64`, а MinGW — в `C:\Qt\Tools\mingw1310_64`.

> Точный номер MinGW-каталога может отличаться (он зависит от версии GCC в
> инсталляторе). Проверь по факту: `dir C:\Qt\Tools`.

### Как открыть окружение сборки

Дальше все команды выполняются в окружении, где в `PATH` есть и Qt, и MinGW.
Самый простой способ — **меню «Пуск» → Qt 6.11.2 (MinGW …) → «Qt 6.11.2 (MinGW
….)»** — это cmd-ярлык с уже настроенным `PATH`. Либо Git Bash/MSYS2 с
добавленными каталогами:

```bash
export PATH="/c/Qt/Tools/mingw1310_64/bin:/c/Qt/6.11.2/mingw_64/bin:$PATH"
```

Скрипты в `scripts/` — это bash (`#!/usr/bin/env bash`), их удобно запускать из
**Git Bash** (ставится вместе с Git for Windows) или из **MSYS2**. В cmd/PowerShell
можно вызывать `cmake` напрямую (см. §5).

---

## 2. Проверка окружения

```bash
g++ --version          # MinGW-w64, GCC
cmake --version
qmake6 -query QT_VERSION   # должно быть 6.11.2 (или: qmake -query QT_VERSION)
ls "$(dirname "$(which qmake6)")/Qt6MqttConfig.cmake"   # MQTT-модуль на месте
```

Если `Qt6MqttConfig.cmake` не найден — модуль MQTT не установлен; перезапусти
Qt Maintenance Tool и добавь компонент `Qt MQTT`.

---

## 3. OpenSSL 3.x (собираем тем же MinGW)

Qt **не поставляет** OpenSSL (лицензия). Нужны `libcrypto-3-x64.dll` и
`libssl-3-x64.dll`, собранные **совместимым** MinGW — иначе возможны конфликты
рантайма (`libstdc++`/`libwinpthread`). Поэтому собираем сами.

```bash
# 1. Скачай исходники (пример — 3.0 LTS; подойдёт любая 3.x)
curl -LO https://github.com/openssl/openssl/releases/download/openssl-3.0.15/openssl-3.0.15.tar.gz
tar xzf openssl-3.0.15.tar.gz
cd openssl-3.0.15

# 2. Настрой против MinGW из Qt (путь — свой!)
perl Configure mingw64 no-shared no-tests \
     --prefix=C:/dev/openssl --openssldir=C:/dev/openssl/ssl

# 3. Собери и установи
mingw32-make -j"$(nproc)"
mingw32-make install_sw
```

Итог: `C:\dev\openssl\` содержит `include/`, `lib/`, `bin/`.

> **Почему `no-shared`?** SmartClip линкует OpenSSL **статически** в `.exe` —
> так надёжнее с MinGW. Если собираешь динамически (без `no-shared`), скрипт
> релиза сам скопирует `libcrypto-3-x64.dll` / `libssl-3-x64.dll` рядом с `.exe`
> из переданного `--openssl DIR`.

---

## 4. Сборка приложения

```bash
cd SmartClip

# QT_PATH и OPENSSL_ROOT_DIR — подставь свои
scripts/build-windows.sh \
    --qt "C:/Qt/6.11.2/mingw_64" \
    --openssl "C:/dev/openssl" \
    --jobs "$(nproc)" \
    --test          # опционально: прогнать ctest (offscreen)
```

Бинарник появится в `build-windows/SmartClip.exe`.

Полезные флаги:

| Флаг | Что делает |
|------|------------|
| `--debug` | сборка Debug |
| `--test` | прогон `ctest` (платформо-независимо, offscreen) |
| `--qt DIR` / переменная `QT_PATH` | префикс Qt |
| `--openssl DIR` / `OPENSSL_ROOT_DIR` | корень OpenSSL |
| `--generator NAME` | CMake-генератор (по умолчанию «MinGW Makefiles») |

### Сборка «руками» (cmd/PowerShell)

```bat
cmake -S . -B build-windows -G "MinGW Makefiles" ^
      -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_PREFIX_PATH="C:/Qt/6.11.2/mingw_64" ^
      -DOPENSSL_ROOT_DIR="C:/dev/openssl"
cmake --build build-windows --config Release --parallel
```

### Отладка: консоль с логами

По умолчанию `.exe` собирается как GUI-приложение (**без** чёрного окна
консоли). Чтобы видеть `qInfo`/`qWarning`, собери с консолью:

```bash
cmake -S . -B build-windows -DSMARTCLIP_WINDOWS_CONSOLE=ON ...
```

---

## 5. Портативный ZIP (релиз)

```bash
scripts/release-windows.sh --qt "C:/Qt/6.11.2/mingw_64" --openssl "C:/dev/openssl"
# → dist/release-<версия>/SmartClip-<версия>-windows-x86_64.zip
```

Скрипт:

1. собирает `SmartClip.exe`;
2. запускает **`windeployqt --release --compiler-runtime`** — подкладывает
   рядом Qt-DLL, плагины (platforms/styles/imageformats) и рантайм MinGW
   (`libgcc`, `libstdc++`, `libwinpthread`);
3. копирует `libcrypto-3-x64.dll` / `libssl-3-x64.dll` (из `--openssl` или из `PATH`);
4. упаковывает результат в ZIP + кладёт `SHA256SUMS`.

Готовый ZIP самодостаточен: распаковал → запустил `SmartClip.exe`, Qt/OpenSSL
ставить не нужно.

---

## 6. Автозапуск, ключ и данные

- **Автозапуск** (Settings → «Launch at startup») пишется в
  `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` (значение `SmartClip`).
  Видно в «Диспетчере задач → Автозагрузка». Прав администратора не требует.
- **Ключ шифрования** истории и паролей лежит в **Windows Credential Manager**
  (цель `SmartClip/history-aes-key`, тип Generic). Посмотреть: «Панель
  управления → Диспетчер учётных данных → Учётные данные Windows».
- **Данные приложения**: `%USERPROFILE%\.smartclip\` (`settings.yml`,
  `history.yml`). История — только шифртекст (AES-256-GCM).
- **Смена раскладки** клипа использует раскладки, активные в сессии
  пользователя (через Win32 `ToUnicodeEx`); если система не отдала карты —
  работает встроенный фолбэк us↔ru.

---

## 7. Синхронизация (MQTT, опционально)

Требуется модуль **Qt MQTT** (см. §1). Без него приложение собирается, но синк
выключен (`MqttClient::available() == false`). Настройки брокера — в
Settings-диалоге; тот же брокер, что на macOS/Linux (`mqtt.halfpi.ru:8883`, TLS).

---

## 8. Тесты

```bash
scripts/build-windows.sh --test
# или
cd build-windows && QT_QPA_PLATFORM=offscreen ctest --output-on-failure
```

Тесты типа `LayoutConverterTest` требуют **двух и более** активных раскладок
(например us + ru) — иначе соответствующие проверки помечаются `SKIP` и набор
остаётся зелёным.

---

## 9. Частые проблемы

| Симптом | Причина / решение |
|---------|-------------------|
| `Could NOT find OpenSSL` при configure | не задан `OPENSSL_ROOT_DIR`; передай `--openssl C:/dev/openssl` |
| `Qt6Mqtt` не найден | не установлен компонент «Qt MQTT» в Qt Installer |
| Приложение запускается и сразу падает | рядом нет `libcrypto-3-x64.dll`/`libssl-3-x64.dll` — используй `release-windows.sh` (он их кладёт) |
| `windeployqt: не найден` | неверный `--qt`; проверь `C:\Qt\6.11.2\mingw_64\bin\windeployqt.exe` |
| Нет логов в консоли | `-DSMARTCLIP_WINDOWS_CONSOLE=ON` |
| `mingw32-make: command not found` | MinGW не в `PATH` — открой Qt-ярлык с MinGW из «Пуск» |
