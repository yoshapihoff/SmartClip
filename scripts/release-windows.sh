#!/usr/bin/env bash
# Сборка релизного артефакта SmartClip под Windows: портативный ZIP
# с приложением и всеми нужными DLL (Qt/OpenSSL/MQTT) внутри.
#
# Артефакты (в dist/release-<version>/):
#   • SmartClip-<version>-windows-x86_64.zip — распаковал и запустил
#   • SHA256SUMS                             — контрольные суммы
#
# Использование:
#   scripts/release-windows.sh [--skip-build] [--jobs N] [--version X.Y.Z]
#                              [--qt DIR] [--openssl DIR] [--no-zip]
#     --skip-build  не пересобирать бинарник (взять build-windows/SmartClip.exe)
#     --jobs N      параллелизм сборки
#     --version V   версия (по умолчанию — из файла VERSION.txt)
#     --qt DIR      префикс Qt (по умолчанию $QT_PATH либо C:/Qt/6.11.2/mingw_64)
#     --openssl DIR корень OpenSSL (по умолчанию $OPENSSL_ROOT_DIR)
#     --no-zip      только сложить набор в каталог, без архива
#
# windeployqt кладёт рядом с .exe Qt-DLL и плагины; OpenSSL-DLL (libcrypto-3-x64.dll,
# libssl-3-x64.dll) копируются вручную из --openssl (или из PATH Qt).
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

DO_BUILD=1
DO_ZIP=1
JOBS="$(nproc 2>/dev/null || echo 4)"
REL_VER=""
QT_DIR="${QT_PATH:-C:/Qt/6.11.2/mingw_64}"
OPENSSL_DIR="${OPENSSL_ROOT_DIR:-}"

while [ $# -gt 0 ]; do
    case "$1" in
        --skip-build) DO_BUILD=0 ;;
        --no-zip) DO_ZIP=0 ;;
        --jobs) shift; JOBS="${1:?--jobs требует число}" ;;
        --version) shift; REL_VER="${1:?--version требует X.Y.Z}" ;;
        --qt) shift; QT_DIR="${1:?--qt требует путь}" ;;
        --openssl) shift; OPENSSL_DIR="${1:?--openssl требует путь}" ;;
        -h|--help) sed -n '2,22p' "$0"; exit 0 ;;
        *) die "неизвестный аргумент: $1" ;;
    esac
    shift
done

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*|Windows_NT) ;;
    *) warn "Скрипт рассчитан на Windows/MinGW-окружение (обнаружено: $(uname -s))." ;;
esac

cd "$PROJECT_DIR"

if [ -z "$REL_VER" ]; then
    REL_VER="$(tr -d '[:space:]' < "$PROJECT_DIR/VERSION.txt")"
fi
[[ "$REL_VER" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "некорректная версия: '$REL_VER'"

REL_DIR="$DIST_DIR/release-$REL_VER"
ensure_dist
mkdir -p "$REL_DIR"

BIN="$PROJECT_DIR/build-windows/$APP_NAME.exe"
if [ "$DO_BUILD" -eq 1 ]; then
    log "Сборка бинарника (Release)…"
    BUILD_ARGS=( --jobs "$JOBS" --qt "$QT_DIR" )
    [ -n "$OPENSSL_DIR" ] && BUILD_ARGS+=( --openssl "$OPENSSL_DIR" )
    "$SCRIPT_DIR/build-windows.sh" "${BUILD_ARGS[@]}"
fi
[ -f "$BIN" ] || die "нет бинарника $BIN (собери без --skip-build)."

echo
log "=== Релиз SmartClip $REL_VER → $REL_DIR ==="

# ── 1. Портативный набор ─────────────────────────────────────────────────
log "[1/2] Портативный набор (windeployqt)…"
STAGE="$PROJECT_DIR/build-windows/win-stage"
rm -rf "$STAGE"
mkdir -p "$STAGE/$APP_NAME"
cp "$BIN" "$STAGE/$APP_NAME/"

WIN_PREFIX="$STAGE/$APP_NAME"
[ -n "$OPENSSL_DIR" ] && WIN_PREFIX="$WIN_PREFIX;$OPENSSL_DIR"

WINDEPLOYQT="$QT_DIR/bin/windeployqt.exe"
[ -f "$WINDEPLOYQT" ] || WINDEPLOYQT="$QT_DIR/bin/windeployqt"
[ -f "$WINDEPLOYQT" ] || die "windeployqt не найден в $QT_DIR/bin (проверь --qt)."

# --release: только релизные DLL; --compiler-runtime: тянет libgcc/libstdc++/libwinpthread.
"$WINDEPLOYQT" --release --compiler-runtime --no-translations \
    --dir "$WIN_PREFIX" "$WIN_PREFIX/$APP_NAME.exe" || \
    "$WINDEPLOYQT" --release --compiler-runtime --dir "$WIN_PREFIX" \
        "$WIN_PREFIX/$APP_NAME.exe"

# ── OpenSSL DLL ──────────────────────────────────────────────────────────
# Qt не поставляет OpenSSL. Копируем libcrypto/libssl из --openssl, иначе —
# ищем в PATH (Qt MinGW-toolchain кладёт их рядом).
copy_openssl_dll() {
    local name="$1" src=""
    if [ -n "$OPENSSL_DIR" ]; then
        for cand in "$OPENSSL_DIR/bin/$name" "$OPENSSL_DIR/$name" \
                    "$OPENSSL_DIR/lib/$name"; do
            [ -f "$cand" ] && { src="$cand"; break; }
        done
    fi
    if [ -z "$src" ]; then
        src="$(command -v "$name" 2>/dev/null || true)"
    fi
    if [ -n "$src" ] && [ -f "$src" ]; then
        cp -f "$src" "$WIN_PREFIX/"
        log "  + $name"
    else
        warn "не найден $name — приложение может не запуститься (нужен OpenSSL)."
        warn "  Передай --openssl DIR (см. docs/BUILD-WINDOWS.md)."
    fi
}
copy_openssl_dll "libcrypto-3-x64.dll"
copy_openssl_dll "libssl-3-x64.dll"

cat > "$WIN_PREFIX/README.txt" <<EOF
$APP_NAME $REL_VER (Windows x86_64)
Запуск: $APP_NAME.exe
Все зависимости (Qt6, OpenSSL 3, MQTT) — рядом с исполняемым файлом,
отдельная установка не нужна.
Документация: https://git.halfpi.ru/mario/smartclip
EOF

# ── 2. ZIP ───────────────────────────────────────────────────────────────
if [ "$DO_ZIP" -eq 1 ]; then
    log "[2/2] ZIP…"
    ZIP="$REL_DIR/$APP_NAME-$REL_VER-windows-x86_64.zip"
    rm -f "$ZIP"
    if command -v zip >/dev/null 2>&1; then
        ( cd "$STAGE" && zip -qr "$ZIP" "$APP_NAME" )
    else
        # Фолбэк: powershell Compress-Archive (Git Bash/во многих окружениях).
        powershell.exe -NoProfile -Command \
            "Compress-Archive -Path '$(cygpath -w "$STAGE/$APP_NAME")' -DestinationPath '$(cygpath -w "$ZIP")' -Force"
    fi
    rm -rf "$STAGE"
else
    log "[2/2] Архив пропущен (--no-zip). Набор: $WIN_PREFIX"
fi

# ── Контрольные суммы ─────────────────────────────────────────────────────
( cd "$REL_DIR" && sha256sum ./* > SHA256SUMS )

echo
log "Готовые артефакты:"
ls -lh "$REL_DIR"
echo
echo "Контрольные суммы:"
cat "$REL_DIR/SHA256SUMS"
