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

# ── Поиск Qt6 ────────────────────────────────────────────────────────────
QT_PREFIX=""
if command -v qmake6 >/dev/null; then
    QT_PREFIX="$(qmake6 -query QT_INSTALL_PREFIX)"
elif command -v qtpaths6 >/dev/null; then
    QT_PREFIX="$(qtpaths6 --install-prefix)"
elif command -v brew >/dev/null && brew --prefix qt@6 >/dev/null 2>&1; then
    QT_PREFIX="$(brew --prefix qt@6)"
fi
[ -n "$QT_PREFIX" ] || die "Qt6 не найден. Установи: brew install qt@6  (или задай QT_PATH=/path/to/Qt/6.x)"

# ── Поиск OpenSSL (обязателен: AES-256-GCM для шифрования истории) ────────
OPENSSL_PREFIX=""
if command -v brew >/dev/null && brew --prefix openssl@3 >/dev/null 2>&1; then
    OPENSSL_PREFIX="$(brew --prefix openssl@3)"
elif pkg-config --exists openssl 2>/dev/null; then
    OPENSSL_PREFIX="$(pkg-config --variable=prefix openssl)"
fi
[ -n "$OPENSSL_PREFIX" ] || die "OpenSSL не найден (обязателен для шифрования). Установи: brew install openssl@3"

log "Qt6:     $QT_PREFIX"
log "OpenSSL: $OPENSSL_PREFIX"

PREFIX_PATH="$QT_PREFIX;$OPENSSL_PREFIX"
[ -n "${QT_PATH:-}" ] && PREFIX_PATH="$QT_PATH;$OPENSSL_PREFIX"

log "Конфигурация ($BUILD_TYPE)…"
cmake -S . -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_PREFIX_PATH="$PREFIX_PATH"

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
