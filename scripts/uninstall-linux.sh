#!/usr/bin/env bash
# Удаление SmartClip, установленного из исходников (по манифесту установки).
#
# Использование:
#   scripts/uninstall-linux.sh [--prefix DIR]
#     --prefix DIR   префикс, куда ставили (по умолчанию $HOME/.local)
#
# Системная установка:  sudo scripts/uninstall-linux.sh --prefix /usr/local
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

BUILD_DIR="build-linux"
PREFIX="$HOME/.local"

while [ $# -gt 0 ]; do
    case "$1" in
        --prefix) shift; PREFIX="${1:?--prefix требует путь}" ;;
        -h|--help) sed -n '2,9p' "$0"; exit 0 ;;
        *) die "неизвестный аргумент: $1" ;;
    esac
    shift
done

cd "$PROJECT_DIR"
MANIFEST="$BUILD_DIR/install_manifest.txt"

if [ -f "$MANIFEST" ]; then
    log "Удаляю файлы из манифеста…"
    if [ -w "$(dirname "$MANIFEST")" ]; then
        xargs -r -a "$MANIFEST" rm -v
    else
        sudo xargs -r -a "$MANIFEST" rm -v
    fi
else
    warn "нет $MANIFEST — удаляю по известным путям"
    rm -f "$PREFIX/bin/$APP_NAME" \
          "$PREFIX/share/applications/smartclip.desktop" \
          "$PREFIX/share/icons/hicolor/512x512/apps/smartclip.png" \
          "$PREFIX/share/icons/hicolor/scalable/apps/smartclip.svg" 2>/dev/null || true
fi

# Автостарт (создаётся приложением при включении «Launch at startup»).
AUTOSTART="$HOME/.config/autostart/smartclip.desktop"
[ -f "$AUTOSTART" ] && { log "Удаляю автостарт: $AUTOSTART"; rm -f "$AUTOSTART"; } || true

command -v update-desktop-database >/dev/null && \
    update-desktop-database "$PREFIX/share/applications" 2>/dev/null || true

log "Готово."
