#!/usr/bin/env bash
# Сборка релизных артефактов SmartClip под macOS с ВЛОЖЕННЫМИ зависимостями
# (Qt6/OpenSSL внутрь .app через macdeployqt + санитария rpath).
#
# Артефакты (в dist/release-<version>/):
#   • SmartClip-<version>-macos-<arch>.zip  — .app с вложенными либами (всегда)
#   • SmartClip-<version>-macos-<arch>.dmg  — если доступен hdiutil (штатно на macOS)
#
# ТРЕБУЕТ macOS (macdeployqt/otool/install_name_tool/codesign). На Linux .app
# с зависимостями не собрать — для этого нужен macOS-раннер (см. CI).
#
# Использование:
#   scripts/release-macos.sh [--no-dmg] [--jobs N] [--version X.Y.Z]
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

DO_DMG=1
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
REL_VER=""

while [ $# -gt 0 ]; do
    case "$1" in
        --no-dmg) DO_DMG=0 ;;
        --jobs) shift; JOBS="${1:?--jobs требует число}" ;;
        --version) shift; REL_VER="${1:?--version требует X.Y.Z}" ;;
        -h|--help) sed -n '2,16p' "$0"; exit 0 ;;
        *) die "неизвестный аргумент: $1" ;;
    esac
    shift
done

[ "$(uname -s)" = "Darwin" ] || die "Этот скрипт — только для macOS (нужны macdeployqt/otool/codesign)."

cd "$PROJECT_DIR"

if [ -z "$REL_VER" ]; then
    REL_VER="$(tr -d '[:space:]' < "$PROJECT_DIR/VERSION")"
fi
[[ "$REL_VER" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "некорректная версия: '$REL_VER'"

ARCH="$(uname -m)"   # arm64 | x86_64
REL_DIR="$DIST_DIR/release-$REL_VER"
ensure_dist
mkdir -p "$REL_DIR"

echo
log "=== Релиз SmartClip $REL_VER (macOS/$ARCH) → $REL_DIR ==="

# ── 1. Сборка + упаковка Qt/OpenSSL внутрь .app ───────────────────────────
log "[1/3] Сборка .app с вложенными зависимостями (build-macos.sh --bundle)…"
"$SCRIPT_DIR/build-macos.sh" --bundle --jobs "$JOBS"

APP="$PROJECT_DIR/build-macos/$APP_NAME.app"
[ -d "$APP" ] || die "не найден бандл $APP"

# ── 2. Проверка самодостаточности (нет внешних Qt-ссылок) ─────────────────
log "[2/3] Проверка самодостаточности бандла…"
bad=0
while IFS= read -r f; do
    [ -f "$f" ] || continue
    # Внешняя зависимость = абсолютный путь ВНЕ бандла и вне /usr/lib,
    # /System (Homebrew/opt, /usr/local, /Volumes/Qt/…). Ссылки на файлы
    # ВНУТРИ .app допустимы — sanitize переводит их в @rpath.
    ext="$(otool -L "$f" 2>/dev/null | tail -n +2 | awk '{print $1}' \
           | grep -E '^/' | grep -vE '^/usr/lib/|^/System/' \
           | grep -vF "$APP/" || true)"
    if [ -n "$ext" ]; then
        warn "$(basename "$f") ссылается на внешние пути:"; echo "$ext" | sed 's/^/    /' >&2
        bad=1
    fi
done < <(find "$APP/Contents/MacOS" "$APP/Contents/Frameworks" "$APP/Contents/PlugIns" -type f 2>/dev/null)
if [ "$bad" -eq 0 ]; then
    log "OK: бандл самодостаточен."
else
    die "бандл НЕ самодостаточен — на машине без этих библиотек запуск упадёт (проверь macdeployqt/OpenSSL)."
fi

# ── 3. Архивы: zip (всегда) + dmg (если есть hdiutil) ─────────────────────
ZIP="$REL_DIR/$APP_NAME-$REL_VER-macos-$ARCH.zip"
log "[3/3] Архив .zip…"
rm -f "$ZIP"
# ditto сохраняет расширенные атрибуты и симлинки внутри .app (в отличие от zip).
ditto -c -k --sequesterRsrc --keepParent "$APP" "$ZIP"

if [ "$DO_DMG" -eq 1 ]; then
    if command -v hdiutil >/dev/null 2>&1; then
        DMG="$REL_DIR/$APP_NAME-$REL_VER-macos-$ARCH.dmg"
        log "Образ .dmg…"
        rm -f "$DMG" "$REL_DIR/$APP_NAME.app"
        ln -s /Applications "$REL_DIR/Applications" 2>/dev/null || true
        hdiutil create -volname "$APP_NAME $REL_VER" -srcfolder "$REL_DIR" \
            -ov -format UDZO "$DMG" >/dev/null
        rm -f "$REL_DIR/Applications"
    else
        warn "hdiutil не найден — .dmg пропущен (оставляю .zip)."
    fi
fi

( cd "$REL_DIR" && shasum -a 256 ./* > SHA256SUMS )

echo
log "Готовые артефакты:"
ls -lh "$REL_DIR"
echo
echo "Контрольные суммы:"; cat "$REL_DIR/SHA256SUMS"
