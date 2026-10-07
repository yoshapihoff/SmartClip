#!/usr/bin/env bash
# Сборка ВСЕХ релизных артефактов SmartClip под Linux в один прогон,
# с привязкой к версии из файла VERSION.
#
# Артефакты (в dist/release-<version>/):
#   • SmartClip-<version>-x86_64.AppImage   — «образ», самодостаточный, без установки
#   • smartclip_<version>_amd64.deb         — инсталлятор для Debian/Ubuntu
#   • SmartClip-<version>-linux-x86_64.tar.gz — портативный tar.gz
#   • SHA256SUMS                            — контрольные суммы
#
# Использование:
#   scripts/release-linux.sh [--skip-build] [--jobs N] [--version X.Y.Z]
#     --skip-build  не пересобирать бинарник (взять build-linux/SmartClip)
#     --jobs N      параллелизм сборки
#     --version V   версия (по умолчанию — из файла VERSION)
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

DO_BUILD=1
JOBS="$(nproc 2>/dev/null || echo 2)"
REL_VER=""

while [ $# -gt 0 ]; do
    case "$1" in
        --skip-build) DO_BUILD=0 ;;
        --jobs) shift; JOBS="${1:?--jobs требует число}" ;;
        --version) shift; REL_VER="${1:?--version требует X.Y.Z}" ;;
        -h|--help) sed -n '2,18p' "$0"; exit 0 ;;
        *) die "неизвестный аргумент: $1" ;;
    esac
    shift
done

[ "$(uname -s)" = "Linux" ] || die "Этот скрипт — для Linux. На macOS используй scripts/release-macos.sh."

cd "$PROJECT_DIR"

if [ -z "$REL_VER" ]; then
    REL_VER="$(tr -d '[:space:]' < "$PROJECT_DIR/VERSION")"
fi
[[ "$REL_VER" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "некорректная версия: '$REL_VER'"

REL_DIR="$DIST_DIR/release-$REL_VER"
ensure_dist
mkdir -p "$REL_DIR"

BIN="$PROJECT_DIR/build-linux/$APP_NAME"
if [ "$DO_BUILD" -eq 1 ]; then
    log "Сборка бинарника (Release)…"
    cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_INSTALL_PREFIX="$HOME/.local" >/dev/null
    cmake --build build-linux --config Release --parallel "$JOBS"
fi
[ -f "$BIN" ] || die "нет бинарника $BIN (собери без --skip-build)."

echo
log "=== Релиз SmartClip $REL_VER → $REL_DIR ==="

# ── 1. AppImage ───────────────────────────────────────────────────────────
log "[1/3] AppImage…"
"$SCRIPT_DIR/build-appimage.sh" --skip-build \
    --out "$REL_DIR/$APP_NAME-$REL_VER-x86_64.AppImage"

# ── 2. .deb ───────────────────────────────────────────────────────────────
log "[2/3] .deb…"
"$SCRIPT_DIR/package-deb.sh" --version "$REL_VER" --binary "$BIN" \
    --out "$REL_DIR/smartclip_${REL_VER}_amd64.deb"

# ── 3. tar.gz (портативный) ───────────────────────────────────────────────
log "[3/3] tar.gz…"
STAGE="$PROJECT_DIR/build-linux/tar-stage"
rm -rf "$STAGE"
mkdir -p "$STAGE/$APP_NAME"
cp "$BIN" "$STAGE/$APP_NAME/$APP_NAME"
cp "$PROJECT_DIR/assets/smartclip.desktop" "$STAGE/$APP_NAME/"
cp "$PROJECT_DIR/icons/icon.png" "$STAGE/$APP_NAME/smartclip.png"
cat > "$STAGE/$APP_NAME/README.txt" <<EOF
$APP_NAME $REL_VER (Linux x86_64)
Запуск: ./$APP_NAME
Зависимости: Qt6 (Core/Gui/Widgets/Svg/Network/DBus, опц. Mqtt), OpenSSL 3.
Документация: https://git.halfpi.ru/mario/smartclip
EOF
TARBALL="$REL_DIR/$APP_NAME-$REL_VER-linux-x86_64.tar.gz"
tar -C "$STAGE" -czf "$TARBALL" "$APP_NAME"
rm -rf "$STAGE"

# ── Контрольные суммы ─────────────────────────────────────────────────────
( cd "$REL_DIR" && sha256sum ./* > SHA256SUMS )

echo
log "Готовые артефакты:"
ls -lh "$REL_DIR"
echo
echo "Контрольные суммы:"
cat "$REL_DIR/SHA256SUMS"
