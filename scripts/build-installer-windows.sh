#!/usr/bin/env bash
# Сборка установщика SmartClip для Windows (Inno Setup 6).
#
# Делает всё по порядку:
#   1. собирает портативный набор (scripts/release-windows.sh, windeployqt);
#   2. если установлен Inno Setup — пакует его в один установщик.
#
# Артефакты (в dist/release-<version>/):
#   • SmartClip-<version>-windows-x86_64-setup.exe — установщик
#   • SmartClip-<version>-windows-x86_64.zip       — портативная версия
#   • SHA256SUMS                                    — контрольные суммы
#
# Использование:
#   scripts/build-installer-windows.sh [--skip-build] [--jobs N] [--version X.Y.Z]
#                                      [--qt DIR] [--openssl DIR] [--no-zip]
#     --skip-build  не пересобирать бинарник (взять готовый из build-windows/)
#     --jobs N      параллелизм сборки
#     --version V   версия (по умолчанию — из VERSION.txt)
#     --qt DIR      префикс Qt (по умолчанию $QT_PATH либо C:/Qt/6.8.3/mingw_64)
#     --openssl DIR корень OpenSSL (по умолчанию $OPENSSL_ROOT_DIR)
#     --no-zip      не собирать портативный ZIP (только установщик)
#
# Inno Setup 6 ищется в стандартных местах (Program Files) или в $ISCC.
# Скачать: https://jrsoftware.org/isdl.php (бесплатно).
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

DO_BUILD=1
DO_ZIP_ARG=""
JOBS="$(nproc 2>/dev/null || echo 4)"
REL_VER=""
QT_DIR="${QT_PATH:-C:/Qt/6.8.3/mingw_64}"
OPENSSL_DIR="${OPENSSL_ROOT_DIR:-}"

while [ $# -gt 0 ]; do
    case "$1" in
        --skip-build) DO_BUILD=0 ;;
        --no-zip) DO_ZIP_ARG="--no-zip" ;;
        --jobs) shift; JOBS="${1:?--jobs требует число}" ;;
        --version) shift; REL_VER="${1:?--version требует X.Y.Z}" ;;
        --qt) shift; QT_DIR="${1:?--qt требует путь}" ;;
        --openssl) shift; OPENSSL_DIR="${1:?--openssl требует путь}" ;;
        -h|--help) sed -n '2,26p' "$0"; exit 0 ;;
        *) die "неизвестный аргумент: $1" ;;
    esac
    shift
done

cd "$PROJECT_DIR"

if [ -z "$REL_VER" ]; then
    REL_VER="$(tr -d '[:space:]' < "$PROJECT_DIR/VERSION.txt")"
fi
[[ "$REL_VER" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "некорректная версия: '$REL_VER'"

# ── 1. Портативный набор (он же вход для установщика) ─────────────────────
REL_ARGS=( --version "$REL_VER" --jobs "$JOBS" --qt "$QT_DIR" )
[ "$DO_BUILD" -eq 0 ] && REL_ARGS+=( --skip-build )
[ -n "$OPENSSL_DIR" ] && REL_ARGS+=( --openssl "$OPENSSL_DIR" )
# Набор всегда собираем в каталог: из него берёт файлы Inno Setup.
"$SCRIPT_DIR/release-windows.sh" --no-zip "${REL_ARGS[@]}"

STAGE="$PROJECT_DIR/build-windows/win-stage"
[ -d "$STAGE/$APP_NAME" ] || die "нет каталога набора: $STAGE/$APP_NAME"

# ── 2. Inno Setup ────────────────────────────────────────────────────────
find_iscc() {
    local cands=(
        "${ISCC:-}"
        "/c/Program Files (x86)/Inno Setup 6/ISCC.exe"
        "/c/Program Files/Inno Setup 6/ISCC.exe"
        "C:/Program Files (x86)/Inno Setup 6/ISCC.exe"
        "C:/Program Files/Inno Setup 6/ISCC.exe"
    )
    local c
    for c in "${cands[@]}"; do
        [ -n "$c" ] && [ -f "$c" ] && { printf '%s' "$c"; return 0; }
    done
    command -v iscc 2>/dev/null && return 0
    return 1
}

ISCC_BIN="$(find_iscc || true)"
if [ -z "$ISCC_BIN" ]; then
    warn "Inno Setup 6 не найден — установщик не собран."
    warn "  Скачай https://jrsoftware.org/isdl.php (бесплатно) или передай ISCC=/path/ISCC.exe"
    warn "  Портативный ZIP всё равно готов."
    [ -n "$DO_ZIP_ARG" ] || "$SCRIPT_DIR/release-windows.sh" --skip-build --version "$REL_VER" \
        --qt "$QT_DIR" ${OPENSSL_DIR:+--openssl "$OPENSSL_DIR"} >/dev/null 2>&1 || true
    exit 0
fi

log "Inno Setup: $ISCC_BIN"
REL_DIR="$DIST_DIR/release-$REL_VER"
mkdir -p "$REL_DIR"

# Inno Setup ждёт windows-пути; cygpath есть в Git Bash/MSYS2.
to_win_path() {
    if command -v cygpath >/dev/null 2>&1; then
        cygpath -w "$1"
    else
        printf '%s' "$1"
    fi
}

log "Сборка установщика (версия $REL_VER)…"
"$ISCC_BIN" \
    "/DAppVersion=$REL_VER" \
    "/DSourceDir=$(to_win_path "$STAGE/$APP_NAME")" \
    "/DOutputDir=$(to_win_path "$REL_DIR")" \
    "$(to_win_path "$PROJECT_DIR/installer/windows/smartclip.iss")"

# ── 3. ZIP (если не отключён) ────────────────────────────────────────────
if [ -z "$DO_ZIP_ARG" ]; then
    "$SCRIPT_DIR/release-windows.sh" --skip-build --version "$REL_VER" --qt "$QT_DIR" \
        ${OPENSSL_DIR:+--openssl "$OPENSSL_DIR"} >/dev/null
fi

# ── 4. Контрольные суммы ─────────────────────────────────────────────────
( cd "$REL_DIR" && sha256sum ./* > SHA256SUMS )

echo
log "Готовые артефакты:"
ls -lh "$REL_DIR"
echo
cat "$REL_DIR/SHA256SUMS"
