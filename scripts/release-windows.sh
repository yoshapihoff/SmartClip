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
#     --qt DIR      префикс Qt (по умолчанию $QT_PATH либо C:/Qt/6.8.3/mingw_64)
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
QT_DIR="${QT_PATH:-C:/Qt/6.8.3/mingw_64}"
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

# --dir и путь к .exe — это ОДИН аргумент windeployqt, поэтому каталог
# назначения остаётся чистым ($WIN_PREFIX). Каталоги доп. поиска DLL передаются
# ОТДЕЛЬНЫМИ флагами -libdir (Qt ищет в них зависимости). Раньше путь Qt/OpenSSL
# дописывался через «;» прямо В КАТАЛОГ НАЗНАЧЕНИЯ — из MSYS это превращалось в
# «\c\Users\…\SmartClip;\c\msys64…», и windeployqt падал.
WIN_PREFIX="$STAGE/$APP_NAME"
WIN_LIBDIRS=()
if command -v cygpath >/dev/null 2>&1; then
    QTW="$(cygpath -w "$QT_DIR")"
    [ -n "$OPENSSL_DIR" ] && {
        OPENSSL_W="$(cygpath -w "$OPENSSL_DIR")"
        WIN_LIBDIRS+=( "-libdir=$OPENSSL_W/bin" "-libdir=$OPENSSL_W/lib" )
    }
else
    QTW="$QT_DIR"
    [ -n "$OPENSSL_DIR" ] && WIN_LIBDIRS+=( "-libdir=$OPENSSL_DIR/bin" "-libdir=$OPENSSL_DIR/lib" )
fi

WINDEPLOYQT="$(cygpath -w "$QT_DIR/bin/windeployqt.exe" 2>/dev/null || echo "$QT_DIR/bin/windeployqt.exe")"
[ -f "$QT_DIR/bin/windeployqt.exe" ] || WINDEPLOYQT="$QT_DIR/bin/windeployqt"
[ -f "$QT_DIR/bin/windeployqt.exe" ] || [ -f "$QT_DIR/bin/windeployqt" ] || \
    die "windeployqt не найден в $QT_DIR/bin (проверь --qt)."

# --release: только релизные DLL; --compiler-runtime: тянет libgcc/libstdc++/libwinpthread.
#
# ГРАБЛЯ 21.10.2026 (сломанный релиз 1.1.5): windeployqt МОЛЧА скопировал только
# плагины и не положил ни одной Qt-DLL — в ZIP уехали 15 файлов без Qt6Core/
# Qt6Gui/…, и на Windows приложение падало со «не найдена библиотека».
# Причины: (1) windeployqt не видел MinGW-toolchain в PATH → --compiler-runtime
# ничего не дал; (2) часть DLL (Qt6Svg/Qt6Mqtt) он пропускает, если зависимость
# приходит через статический плагин. Поэтому PATH собираем явно, а после деплоя
# ОБЯЗАТЕЛЬНО проверяем набор (блок «Проверка полноты»).
MIN_GW_BIN=""
for d in "$QT_DIR/../Tools"/mingw*_64/bin "$QT_DIR"/../../Tools/mingw*_64/bin; do
    [ -d "$d" ] && { MIN_GW_BIN="$d"; break; }
done
if [ -n "$MIN_GW_BIN" ]; then
    log "  MinGW: $MIN_GW_BIN"
    export PATH="$MIN_GW_BIN:$PATH"
else
    warn "MinGW-toolchain рядом с Qt не найден — --compiler-runtime может ничего не скопировать."
fi

W_PREFIX="$(cygpath -m "$WIN_PREFIX" 2>/dev/null || echo "$WIN_PREFIX")"
"$WINDEPLOYQT" --release --compiler-runtime --no-translations \
    ${WIN_LIBDIRS[@]+"${WIN_LIBDIRS[@]}"} \
    --plugindir "$W_PREFIX" \
    --dir "$W_PREFIX" \
    "$(cygpath -m "$WIN_PREFIX/$APP_NAME.exe" 2>/dev/null || echo "$WIN_PREFIX/$APP_NAME.exe")"

# ── 1b. Дотяжка DLL, которые windeployqt пропускает ──────────────────────
# Обходим все .exe/.dll набора и добираем отсутствующие зависимости из
# каталогов Qt/MinGW/OpenSSL. Идемпотентно: уже скопированное не трогаем.
HOST_PREFIX="$WIN_PREFIX"

dll_search_dirs() {
    printf '%s\n' "$QT_DIR/bin"
    [ -n "$MIN_GW_BIN" ] && printf '%s\n' "$MIN_GW_BIN"
    [ -n "$OPENSSL_DIR" ] && printf '%s\n' "$OPENSSL_DIR/bin" "$OPENSSL_DIR/lib"
}

find_dll() {
    local name="$1" d
    [ -f "$HOST_PREFIX/$name" ] && return 1   # уже есть
    while read -r d; do
        [ -n "$d" ] && [ -f "$d/$name" ] && { printf '%s' "$d/$name"; return 0; }
    done < <(dll_search_dirs)
    return 1
}

if command -v objdump >/dev/null 2>&1; then
    log "  Проверка зависимостей (objdump)…"
    CHANGED=1
    while [ "$CHANGED" -eq 1 ]; do
        CHANGED=0
        while IFS= read -r -d '' f; do
            while read -r dep; do
                case "$dep" in
                    *.dll|*.DLL) ;;
                    *) continue ;;
                esac
                if src="$(find_dll "$dep")"; then
                    cp -f "$src" "$HOST_PREFIX/"
                    log "  + $dep"
                    CHANGED=1
                fi
            done < <(objdump -p "$f" 2>/dev/null | awk '/DLL Name:/{print $3}')
        done < <(find "$HOST_PREFIX" -maxdepth 2 -type f \( -name '*.exe' -o -name '*.dll' \) -print0)
    done
else
    warn "objdump не найден — пропускаю дотяжку зависимостей."
fi

# ── 1c. Проверка полноты (guard) ─────────────────────────────────────────
# Минимальный набор, без которого приложение на «чистой» Windows не стартует.
# Если чего-то нет — сборка ПАДАЕТ, чтобы сломанный пакет не уехал в релиз.
REQUIRED_DLLS=(
    Qt6Core.dll Qt6Gui.dll Qt6Widgets.dll Qt6Network.dll Qt6Svg.dll
    libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll
)
MISSING=()
for dll in "${REQUIRED_DLLS[@]}"; do
    [ -f "$HOST_PREFIX/$dll" ] || MISSING+=( "$dll" )
done
[ -f "$HOST_PREFIX/platforms/qwindows.dll" ] || MISSING+=( "platforms/qwindows.dll" )
# Qt6Mqtt — опциональная зависимость: предупреждаем, но не роняем.
[ -f "$HOST_PREFIX/Qt6Mqtt.dll" ] || warn "нет Qt6Mqtt.dll — сетевая синхронизация (MQTT) на Windows не заработает."
if [ ${#MISSING[@]} -gt 0 ]; then
    warn "В наборе НЕ ХВАТАЕТ обязательных файлов:"
    printf '    - %s\n' "${MISSING[@]}" >&2
    die "набор не самодостаточен — на Windows без установленного Qt приложение не запустится. Проверь --qt/--openssl и PATH MinGW."
fi
log "  Набор полон: все обязательные DLL на месте."

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
        # -LiteralPath: в Path PowerShell трактует [ ] как wildcard.
        powershell.exe -NoProfile -Command \
            "Compress-Archive -LiteralPath '$(cygpath -w "$STAGE/$APP_NAME")' -DestinationPath '$(cygpath -w "$ZIP")' -Force"
    fi
    rm -rf "$STAGE"
else
    log "[2/2] Архив пропущен (--no-zip). Набор: $WIN_PREFIX"
fi

# ── Контрольные суммы ─────────────────────────────────────────────────────
# Сам SHA256SUMS исключаем (см. release-linux.sh, грабля 21.10.2026).
# xargs без входных данных всё равно запускает sha256sum с пустым аргументом —
# при `xargs -0 sha256sum` это даёт «sha256sum: './*': No such file» и падение
# по set -e (грабля 21.10.2026, режим --no-zip). Поэтому -r, а при отсутствии
# файлов ставим failure guard: сумма просто не создаётся.
rm -f "$REL_DIR/SHA256SUMS"
( cd "$REL_DIR" && find . -maxdepth 1 -type f -printf '%f\0' | sort -z \
    | xargs -0 -r sha256sum > SHA256SUMS.tmp && mv -f SHA256SUMS.tmp SHA256SUMS )

echo
log "Готовые артефакты:"
ls -lh "$REL_DIR"
echo
[ -s "$REL_DIR/SHA256SUMS" ] && { echo "Контрольные суммы:"; cat "$REL_DIR/SHA256SUMS"; }
