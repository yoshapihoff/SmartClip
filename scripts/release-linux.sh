#!/usr/bin/env bash
# Сборка ВСЕХ релизных артефактов SmartClip под Linux в один прогон,
# с привязкой к версии из файла VERSION.txt.
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
#     --version V   версия (по умолчанию — из файла VERSION.txt)
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

DO_BUILD=1
JOBS="$(nproc 2>/dev/null || echo 2)"
REL_VER=""
QT_DIR="${SMARTCLIP_QT_DIR:-}"

while [ $# -gt 0 ]; do
    case "$1" in
        --skip-build) DO_BUILD=0 ;;
        --jobs) shift; JOBS="${1:?--jobs требует число}" ;;
        --version) shift; REL_VER="${1:?--version требует X.Y.Z}" ;;
        --qt) shift; QT_DIR="${1:?--qt требует путь}" ;;
        -h|--help) sed -n '2,18p' "$0"; exit 0 ;;
        *) die "неизвестный аргумент: $1" ;;
    esac
    shift
done

[ "$(uname -s)" = "Linux" ] || die "Этот скрипт — для Linux. На macOS используй scripts/release-macos.sh."

cd "$PROJECT_DIR"

if [ -z "$REL_VER" ]; then
    REL_VER="$(tr -d '[:space:]' < "$PROJECT_DIR/VERSION.txt")"
fi
[[ "$REL_VER" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "некорректная версия: '$REL_VER'"

REL_DIR="$DIST_DIR/release-$REL_VER"
ensure_dist
mkdir -p "$REL_DIR"

BIN="$PROJECT_DIR/build-linux/$APP_NAME"
if [ "$DO_BUILD" -eq 1 ]; then
    log "Сборка бинарника (Release)…"
    CMAKE_ARGS=( -DCMAKE_BUILD_TYPE=Release
                 -DCMAKE_INSTALL_PREFIX="$HOME/.local" )
    if [ -n "$QT_DIR" ]; then
        log "Qt: $QT_DIR"
        CMAKE_ARGS+=( -DCMAKE_PREFIX_PATH="$QT_DIR" )
    fi
    cmake -S . -B build-linux "${CMAKE_ARGS[@]}" >/dev/null
    cmake --build build-linux --config Release --parallel "$JOBS"
fi
[ -f "$BIN" ] || die "нет бинарника $BIN (собери без --skip-build)."

echo
log "=== Релиз SmartClip $REL_VER → $REL_DIR ==="

# ── 1. AppImage ───────────────────────────────────────────────────────────
log "[1/3] AppImage…"
AI_ARGS=( --skip-build --out "$REL_DIR/$APP_NAME-$REL_VER-x86_64.AppImage" )
[ -n "$QT_DIR" ] && AI_ARGS+=( --qt "$QT_DIR" )
"$SCRIPT_DIR/build-appimage.sh" "${AI_ARGS[@]}"

# ── 2. .deb ───────────────────────────────────────────────────────────────
log "[2/3] .deb…"
"$SCRIPT_DIR/package-deb.sh" --version "$REL_VER" --binary "$BIN" \
    --out "$REL_DIR/smartclip_${REL_VER}_amd64.deb"

# ── 3. tar.gz (портативный, самодостаточный) ──────────────────────────────
# ГРАБЛЯ 21.10.2026: раньше в архив клался только бинарник (5 файлов, 292 КБ)
# с RUNPATH /opt/qt-6.10.2/gcc_64/lib. На любой другой машине он не стартовал:
# «error while loading shared libraries: libQt6Widgets.so.6». Теперь внутрь
# кладутся Qt-библиотеки и плагины, а RUNPATH = $ORIGIN/lib.
log "[3/3] tar.gz…"
STAGE="$PROJECT_DIR/build-linux/tar-stage"
rm -rf "$STAGE"
mkdir -p "$STAGE/$APP_NAME" "$STAGE/$APP_NAME/lib" \
         "$STAGE/$APP_NAME/plugins/platforms" \
         "$STAGE/$APP_NAME/plugins/imageformats" \
         "$STAGE/$APP_NAME/plugins/iconengines"
cp "$BIN" "$STAGE/$APP_NAME/$APP_NAME"
cp "$PROJECT_DIR/assets/smartclip.desktop" "$STAGE/$APP_NAME/"
cp "$PROJECT_DIR/icons/icon.png" "$STAGE/$APP_NAME/smartclip.png"

command -v patchelf >/dev/null 2>&1 || \
    die "patchelf не найден — портативный tar.gz получится нерабочим (pacman -S patchelf / apt install patchelf)."

SKIP_RE='^(libc\.so|libm\.so|libdl\.so|libpthread\.so|libresolv\.so|libnsl\.so|ld-linux|libGLX|libGL\.so|libEGL\.so|libOpenGL|libGLdispatch|libdrm|libgcc_s|libstdc\+\+|libmvec)'
copy_deps_tar() {
    local target="$1" depth="${2:-0}" path base
    [ "$depth" -gt 3 ] && return 0
    while read -r line; do
        path="$(printf '%s' "$line" | awk '{ if ($2=="=>") print $3; else print $1 }')"
        case "$path" in /*) ;; *) continue ;; esac
        [ -f "$path" ] || continue
        base="$(basename "$path")"
        printf '%s' "$base" | grep -qE "$SKIP_RE" && continue
        [ -f "$STAGE/$APP_NAME/lib/$base" ] && continue
        cp -L "$path" "$STAGE/$APP_NAME/lib/$base"
        copy_deps_tar "$path" $((depth + 1))
    done < <(ldd "$target" 2>/dev/null)
}
copy_deps_tar "$BIN"

QT_PLUGINS_SRC=""
for cand in "${QT_DIR:-}/plugins" /opt/qt-6.10.2/gcc_64/plugins \
            /usr/lib/x86_64-linux-gnu/qt6/plugins; do
    [ -d "$cand" ] && { QT_PLUGINS_SRC="$cand"; break; }
done
if [ -n "$QT_PLUGINS_SRC" ]; then
    for sub in platforms imageformats iconengines; do
        [ -d "$QT_PLUGINS_SRC/$sub" ] || continue
        find "$QT_PLUGINS_SRC/$sub" -maxdepth 1 -name '*.so' -print0 \
            | while IFS= read -r -d '' pl; do
                cp -L "$pl" "$STAGE/$APP_NAME/plugins/$sub/"
                copy_deps_tar "$pl"
            done
    done
else
    warn "каталог Qt-плагинов не найден — в архиве не будет platforms/libqxcb.so"
fi

patchelf --set-rpath '$ORIGIN/lib' "$STAGE/$APP_NAME/$APP_NAME"
printf '[Paths]\nPrefix=.\nPlugins=plugins\n' > "$STAGE/$APP_NAME/qt.conf"

cat > "$STAGE/$APP_NAME/README.txt" <<EOF
$APP_NAME $REL_VER (Linux x86_64)
Запуск: ./$APP_NAME
Все зависимости (Qt6, OpenSSL 3) — внутри архива, в каталоге lib/.
Отдельная установка Qt не нужна. Нужны базовые системные библиотеки
(xkbcommon, X11, fontconfig, libglib) — они есть в любом десктопном дистрибутиве.
Документация: https://git.halfpi.ru/mario/smartclip
EOF
TARBALL="$REL_DIR/$APP_NAME-$REL_VER-linux-x86_64.tar.gz"
tar -C "$STAGE" -czf "$TARBALL" "$APP_NAME"
rm -rf "$STAGE"
log "  В архиве библиотек: $(tar -tzf "$TARBALL" | grep -c '/lib/.*\.so')"

# ── Контрольные суммы ─────────────────────────────────────────────────────
# ВАЖНО: сам SHA256SUMS исключаем. При `sha256sum ./* > SHA256SUMS` файл уже
# создан оболочкой и попадает в свой же список — от прогона к прогону он растёт
# (и содержит свой хеш). Грабли 21.10.2026: из-за этого в релизе появлялись
# дубли ассетов, а суммы не совпадали между сервисами.
rm -f "$REL_DIR/SHA256SUMS"
( cd "$REL_DIR" && find . -maxdepth 1 -type f ! -name 'SHA256SUMS' -printf '%f\0' \
    | sort -z | xargs -0 sha256sum > SHA256SUMS )

echo
log "Готовые артефакты:"
ls -lh "$REL_DIR"
echo
echo "Контрольные суммы:"
cat "$REL_DIR/SHA256SUMS"
