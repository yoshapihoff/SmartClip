#!/usr/bin/env bash
# Сборка (и при желании установка) SmartClip из исходников под Linux.
#
# Использование:
#   scripts/build-linux.sh [--install] [--prefix DIR] [--debug] [--jobs N]
#     --install      после сборки выполнить установку (cmake --install)
#     --prefix DIR   префикс установки (по умолчанию $HOME/.local)
#     --debug        сборка Debug (по умолчанию Release)
#     --jobs N       параллелизм (по умолчанию: nproc)
#
# Установка в /usr/local (системно) — запускай с sudo:
#   sudo scripts/build-linux.sh --install --prefix /usr/local
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

BUILD_DIR="build-linux"
BUILD_TYPE="Release"
PREFIX="$HOME/.local"
DO_INSTALL=0
JOBS="$(nproc 2>/dev/null || echo 2)"

while [ $# -gt 0 ]; do
    case "$1" in
        --install) DO_INSTALL=1 ;;
        --prefix) shift; PREFIX="${1:?--prefix требует путь}" ;;
        --debug) BUILD_TYPE="Debug" ;;
        --jobs) shift; JOBS="${1:?--jobs требует число}" ;;
        -h|--help) sed -n '2,15p' "$0"; exit 0 ;;
        *) die "неизвестный аргумент: $1" ;;
    esac
    shift
done

cd "$PROJECT_DIR"

# Проверка базовых зависимостей (даём понятную подсказку, а не падение cmake).
command -v cmake >/dev/null || die "cmake не найден. Установи: pacman -S cmake / apt install cmake / dnf install cmake"
if ! pkg-config --exists openssl 2>/dev/null && ! ls /usr/lib/*/cmake/OpenSSL/OpenSSLConfig.cmake >/dev/null 2>&1; then
    warn "OpenSSL не найден в системе — сборка упадёт (OpenSSL обязателен для шифрования)."
    warn "  Arch: pacman -S openssl   Debian/Ubuntu: apt install libssl-dev   Fedora: dnf install openssl-devel"
fi

log "Конфигурация ($BUILD_TYPE, prefix=$PREFIX)…"
cmake -S . -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_INSTALL_PREFIX="$PREFIX"

log "Сборка ($JOBS потоков)…"
cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --parallel "$JOBS"

log "Бинарник: $BUILD_DIR/$APP_NAME"

if [ "$DO_INSTALL" -eq 1 ]; then
    log "Установка в $PREFIX …"
    if [ -w "$PREFIX" ] || [ -w "$(dirname "$PREFIX")" ]; then
        cmake --install "$BUILD_DIR"
    elif [ "$(id -u)" -eq 0 ]; then
        cmake --install "$BUILD_DIR"
    else
        warn "нет прав на запись в $PREFIX — пробую через sudo"
        sudo cmake --install "$BUILD_DIR"
    fi
    echo
    log "Установлено. Запуск: $PREFIX/bin/$APP_NAME"
else
    echo
    log "Готово. Запуск: $BUILD_DIR/$APP_NAME"
    echo "Установить: scripts/build-linux.sh --install --prefix \$HOME/.local"
fi
