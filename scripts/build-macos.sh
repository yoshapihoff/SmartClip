#!/usr/bin/env bash
# Сборка SmartClip.app под macOS + подсказка по зависимостям для запуска.
#
# Использование:
#   scripts/build-macos.sh [--bundle] [--dist] [--debug] [--jobs N]
#     --bundle   упаковать Qt/OpenSSL внутрь .app через macdeployqt
#                (чтобы приложение запускалось без установленного Qt)
#     --dist     скопировать готовый .app в dist/ (артефакт для раздачи)
#     --debug    сборка Debug (по умолчанию Release)
#     --jobs N   параллелизм (по умолчанию: все ядра)
#
# Зависимости (установить один раз):
#   brew install qt@6 cmake openssl@3
# (qt6-mqtt для синхронизации — через Qt Online Installer; см. docs/BUILD-MACOS.md)
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

BUILD_DIR="build-macos"
BUILD_TYPE="Release"
DO_BUNDLE=0
DO_DIST=0
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

while [ $# -gt 0 ]; do
    case "$1" in
        --bundle) DO_BUNDLE=1 ;;
        --dist) DO_DIST=1 ;;
        --debug) BUILD_TYPE="Debug" ;;
        --jobs) shift; JOBS="${1:?--jobs требует число}" ;;
        -h|--help) sed -n '2,18p' "$0"; exit 0 ;;
        *) die "неизвестный аргумент: $1" ;;
    esac
    shift
done

[ "$(uname -s)" = "Darwin" ] || die "Этот скрипт — для macOS. На Linux используй scripts/build-linux.sh / build-appimage.sh."
command -v cmake >/dev/null || die "cmake не найден: brew install cmake"

cd "$PROJECT_DIR"

# ── Поиск Qt6 (с ВАЛИДАЦИЕЙ: в префиксе должен быть lib/cmake/Qt6) ───────
# Просто `qmake6 -query` недостаточно: он может указать на префикс без
# CMake-конфига (напр. /usr/local/opt/qt без библиотеки cmake/Qt6), и CMake
# падает с «Could not find Qt6Config.cmake». Перебираем варианты и берём
# первый, где реально лежит Qt6Config.cmake.
qt6_config_dir() {
    local p="$1" c
    [ -n "$p" ] || return 1
    # pwd -P разрешает симлинки (brew /usr/local/opt/qt → /usr/local/Cellar/qt/…),
    # и работает и на macOS (BSD), в отличие от `readlink -f`.
    c="$(cd "$p" 2>/dev/null && pwd -P)" || return 1
    local f
    for f in "$c/lib/cmake/Qt6/Qt6Config.cmake" "$c/lib64/cmake/Qt6/Qt6Config.cmake"; do
        [ -f "$f" ] && { dirname "$f"; return 0; }
    done
    return 1
}

QT_PREFIX=""
QT6_DIR=""
try_qt() {
    local p="$1" cfg
    [ -n "$p" ] || return 1
    cfg="$(qt6_config_dir "$p")" && { QT_PREFIX="$p"; QT6_DIR="$cfg"; return 0; }
    return 1
}

QT_CANDS=()
[ -n "${QT_PATH:-}" ] && QT_CANDS+=("$QT_PATH")
# Приоритет — brew (по докам ставим qt@6), ЗАТЕМ qmake6: на macOS в PATH может
# оказаться чужой qmake6 (напр. из conda) и увести на префикс без cmake/Qt6.
if command -v brew >/dev/null 2>&1; then
    QT_CANDS+=("$(brew --prefix qt@6 2>/dev/null)" "$(brew --prefix qt 2>/dev/null)" \
               "$(brew --prefix 2>/dev/null)/opt/qt" "$(brew --prefix 2>/dev/null)/opt/qt@6")
fi
command -v qmake6   >/dev/null 2>&1 && QT_CANDS+=("$(qmake6 -query QT_INSTALL_PREFIX 2>/dev/null)")
command -v qtpaths6 >/dev/null 2>&1 && QT_CANDS+=("$(qtpaths6 --install-prefix 2>/dev/null)")
QT_CANDS+=(/usr/local/opt/qt@6 /opt/homebrew/opt/qt@6 /usr/local/opt/qt /opt/homebrew/opt/qt)
# Qt Online Installer (типовые локации; у Лёши — /Volumes/HDD/qt)
for d in /Volumes/HDD/qt /opt/Qt "$HOME/Qt"; do
    [ -d "$d" ] || continue
    hit="$(find "$d" -maxdepth 4 -path '*/macos/lib/cmake/Qt6/Qt6Config.cmake' -not -path '*/Examples/*' 2>/dev/null | head -1)"
    [ -z "$hit" ] && hit="$(find "$d" -maxdepth 6 -name Qt6Config.cmake -path '*/cmake/*' \
        -not -path '*/android*' -not -path '*/ios*' -not -path '*/wasm*' -not -path '*/qnx*' -not -path '*/Examples/*' 2>/dev/null | head -1)"
    [ -n "$hit" ] && QT_CANDS+=("$(cd "$(dirname "$(dirname "$(dirname "$(dirname "$hit")")")")" 2>/dev/null && pwd)")
done

for c in ${QT_CANDS[@]+"${QT_CANDS[@]}"}; do try_qt "$c" && break; done
if [ -z "$QT6_DIR" ]; then
    warn "Qt6 с lib/cmake/Qt6 не найден. Проверены префиксы:"
    for c in ${QT_CANDS[@]+"${QT_CANDS[@]}"}; do [ -n "$c" ] && warn "  - $c"; done
    die "Установи 'brew install qt@6' или задай QT_PATH=/путь/к/Qt/6.x (напр. \$HOME/Qt/6.11.0/macos).\n     Найти реальный префикс: find /usr/local /opt/homebrew \\$HOME/Qt -name Qt6Config.cmake 2>/dev/null"
fi

# ── Поиск OpenSSL (обязателен: AES-256-GCM для шифрования истории) ────────
OPENSSL_PREFIX=""
for c in "$(brew --prefix openssl@3 2>/dev/null)" "$(brew --prefix openssl 2>/dev/null)" \
         /usr/local/opt/openssl@3 /opt/homebrew/opt/openssl@3 /usr/local/opt/openssl /opt/homebrew/opt/openssl; do
    [ -n "$c" ] && { [ -f "$c/lib/libcrypto.dylib" ] || [ -f "$c/lib/libcrypto.a" ]; } && { OPENSSL_PREFIX="$c"; break; }
done
[ -n "$OPENSSL_PREFIX" ] || die "OpenSSL не найден (обязателен для шифрования). Установи: brew install openssl@3"

log "Qt6:     $QT_PREFIX"
log "Qt6_DIR: $QT6_DIR"
log "OpenSSL: $OPENSSL_PREFIX"

log "Конфигурация ($BUILD_TYPE)…"
cmake -S . -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_PREFIX_PATH="$QT_PREFIX;$OPENSSL_PREFIX" \
    -DQt6_DIR="$QT6_DIR" \
    -DOPENSSL_ROOT_DIR="$OPENSSL_PREFIX"

log "Сборка ($JOBS потоков)…"
cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --parallel "$JOBS"

APP="$BUILD_DIR/$APP_NAME.app"
[ -d "$APP" ] || die "не найден бандл $APP"

if [ "$DO_BUNDLE" -eq 1 ]; then
    MACDEPLOYQT="$QT_PREFIX/bin/macdeployqt"
    [ -x "$MACDEPLOYQT" ] || command -v macdeployqt >/dev/null || \
        die "macdeployqt не найден (нужен для --bundle)"
    : "${MACDEPLOYQT:=$(command -v macdeployqt)}"
    log "Упаковка Qt/OpenSSL внутрь .app (macdeployqt)…"
    "$MACDEPLOYQT" "$APP" -always-overwrite \
        -executable="$APP/Contents/MacOS/$APP_NAME"
fi

echo
log "Готово: $APP"
if [ "$DO_DIST" -eq 1 ]; then
    ensure_dist
    rm -rf "$DIST_DIR/$APP_NAME.app"
    cp -R "$APP" "$DIST_DIR/$APP_NAME.app"
    log "Артефакт: $DIST_DIR/$APP_NAME.app"
fi
echo "Запуск:  open \"$APP\"   (или: $APP/Contents/MacOS/$APP_NAME)"
cat <<'DEPS'

────────────────────────────────────────────────────────────────────────
Зависимости для ЗАПУСКА готового приложения:
  • macOS 12+ (шрифты/тема — системные).
  • Qt6 (Core/Gui/Widgets/Svg) — либо системный, либо внутри .app (--bundle).
  • OpenSSL 3 (libcrypto) — нужен для шифрования истории.
  • Qt6 MQTT — ТОЛЬКО если пользуешься сетевой синхронизацией.
  • Keychain — встроен в macOS (хранение ключа шифрования).
Если собирал без --bundle, на машине без Qt запусти:
  brew install qt@6 openssl@3
────────────────────────────────────────────────────────────────────────
DEPS
