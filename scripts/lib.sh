#!/usr/bin/env bash
# Общие функции для скриптов сборки/упаковки/установки SmartClip.
# Подключается через:  source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
readonly SCRIPT_DIR PROJECT_DIR

readonly APP_NAME="SmartClip"

# Каталог готовых артефактов (AppImage/.app).
DIST_DIR="$PROJECT_DIR/dist"

# Гарантировать существование dist/.
ensure_dist() { mkdir -p "$DIST_DIR"; }

# Каталог для скачиваемых инструментов упаковки (linuxdeploy/appimagetool).
readonly TOOLS_DIR="${SMARTCLIP_TOOLS_DIR:-$HOME/.cache/smartclip-tools}"

log()  { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m!!\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31mERROR:\033[0m %s\n' "$*" >&2; exit 1; }

# Скачать инструмент, если его ещё нет. $1=url $2=имя_файла
fetch_tool() {
    local url="$1" name="$2" dest="$TOOLS_DIR/$2"
    [ -x "$dest" ] && return 0
    mkdir -p "$TOOLS_DIR"
    log "Скачиваю $name …"
    curl -fL --retry 3 --connect-timeout 20 -o "$dest.part" "$url" \
        || { rm -f "$dest.part"; die "не удалось скачать $name ($url)"; }
    mv "$dest.part" "$dest"
    chmod +x "$dest"
}

# Запуск AppImage-инструмента даже без FUSE (распаковка во временный каталог).
run_appimage_tool() {
    APPIMAGE_EXTRACT_AND_RUN=1 "$@"
}
