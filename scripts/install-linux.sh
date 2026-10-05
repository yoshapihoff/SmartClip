#!/usr/bin/env bash
# Установка SmartClip из исходников под Linux (сборка + install + проверки).
#
# Использование:
#   scripts/install-linux.sh [--prefix DIR] [--debug] [--no-build]
#     --prefix DIR   префикс установки (по умолчанию $HOME/.local)
#     --debug        сборка Debug (по умолчанию Release)
#     --no-build     не пересобирать, только установить уже собранное
#
# Системная установка (в /usr/local):
#   sudo scripts/install-linux.sh --prefix /usr/local
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

BUILD_DIR="build-linux"
BUILD_TYPE="Release"
PREFIX="$HOME/.local"
NO_BUILD=0

while [ $# -gt 0 ]; do
    case "$1" in
        --prefix) shift; PREFIX="${1:?--prefix требует путь}" ;;
        --debug) BUILD_TYPE="Debug" ;;
        --no-build) NO_BUILD=1 ;;
        -h|--help) sed -n '2,13p' "$0"; exit 0 ;;
        *) die "неизвестный аргумент: $1" ;;
    esac
    shift
done

cd "$PROJECT_DIR"

if [ "$NO_BUILD" -eq 0 ]; then
    log "Сборка перед установкой…"
    cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
          -DCMAKE_INSTALL_PREFIX="$PREFIX" >/dev/null
    cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --parallel "$(nproc 2>/dev/null || echo 2)"
fi

[ -f "$BUILD_DIR/$APP_NAME" ] || die "нет бинарника $BUILD_DIR/$APP_NAME — сначала собери (scripts/build-linux.sh)."

log "Установка в $PREFIX …"
if [ -w "$PREFIX" ] || [ -w "$(dirname "$PREFIX")" ] || [ "$(id -u)" -eq 0 ]; then
    cmake --install "$BUILD_DIR"
else
    warn "нет прав на запись в $PREFIX — пробую через sudo"
    sudo cmake --install "$BUILD_DIR"
fi

# Обновляем кэш desktop-базы и иконок, если инструменты есть (не критично).
command -v update-desktop-database >/dev/null && \
    update-desktop-database "$PREFIX/share/applications" 2>/dev/null || true
command -v gtk-update-icon-cache >/dev/null && \
    gtk-update-icon-cache -qtf "$PREFIX/share/icons/hicolor" 2>/dev/null || true

echo
log "Установлено: $PREFIX/bin/$APP_NAME"
case ":$PATH:" in
    *":$PREFIX/bin:"*) : ;;
    *) warn "$PREFIX/bin нет в PATH — добавь в ~/.profile: export PATH=\"$PREFIX/bin:\$PATH\"" ;;
esac
echo "Запуск: $PREFIX/bin/$APP_NAME"
echo "Удаление: scripts/uninstall-linux.sh --prefix $PREFIX"
