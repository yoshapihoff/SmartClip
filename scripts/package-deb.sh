#!/usr/bin/env bash
# Сборка .deb-пакета SmartClip из уже собранного бинарника.
#
# Пакет ставится как обычная система/пользовательская установка:
#   dpkg -i dist/release-<ver>/smartclip_<ver>_amd64.deb
#   apt-get -f install     # дотянет зависимости
# и даёт файлы:
#   /usr/bin/SmartClip
#   /usr/share/applications/smartclip.desktop
#   /usr/share/icons/hicolor/512x512/apps/smartclip.png
#
# Использование:
#   scripts/package-deb.sh [--version X.Y.Z] [--binary PATH] [--out FILE]
#
# ВАЖНО (грабля 21.10.2026): до этой правки в .deb уезжал бинарник с RUNPATH
# /opt/qt-6.10.2/gcc_64/lib — путь машины сборки. На любой другой системе
# пакет не запускался: «error while loading shared libraries: libQt6Widgets.so.6».
# Depends при этом тянул системный libqt6mqtt6, которого в Ubuntu/Debian НЕТ
# (пакет есть только в KDE-neon/Arch), т.е. apt вообще не мог его поставить.
# Теперь пакет самодостаточен: Qt/OpenSSL кладутся внутрь /opt/smartclip/lib,
# а у бинарника RUNPATH = $ORIGIN/../lib.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

command -v dpkg-deb >/dev/null || die "dpkg-deb не найден (нужен для сборки .deb)"

BUILD_DIR="build-linux"
BINARY=""
OUT=""
PKG_VER=""

while [ $# -gt 0 ]; do
    case "$1" in
        --version) shift; PKG_VER="${1:?--version требует X.Y.Z}" ;;
        --binary)  shift; BINARY="${1:?--binary требует путь}" ;;
        --out)     shift; OUT="${1:?--out требует путь}" ;;
        -h|--help) sed -n '2,18p' "$0"; exit 0 ;;
        *) die "неизвестный аргумент: $1" ;;
    esac
    shift
done

cd "$PROJECT_DIR"

# Версия: аргумент → файл VERSION.txt.
if [ -z "$PKG_VER" ]; then
    PKG_VER="$(tr -d '[:space:]' < "$PROJECT_DIR/VERSION.txt")"
fi
[[ "$PKG_VER" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "некорректная версия: '$PKG_VER'"

# Бинарник: аргумент → build-linux/SmartClip.
if [ -z "$BINARY" ]; then
    BINARY="$PROJECT_DIR/$BUILD_DIR/$APP_NAME"
fi
[ -f "$BINARY" ] || die "нет бинарника $BINARY — собери: scripts/build-linux.sh"

ensure_dist
if [ -z "$OUT" ]; then
    OUT="$DIST_DIR/release-$PKG_VER/smartclip_${PKG_VER}_amd64.deb"
fi
mkdir -p "$(dirname "$OUT")"

# ── Зависимости пакета ────────────────────────────────────────────────────
# Альтернативы через «|» покрывают разные схемы имён (Debian 12 vs Ubuntu 24.04+,
# где появились t64-пакеты). Переопределяется переменной SMARTCLIP_DEB_DEPENDS.
# Qt и OpenSSL теперь ВНУТРИ пакета (см. блок «Бандл зависимостей»), поэтому
# зависимости по Qt из Depends убраны: их не было в стандартных репозиториях
# (libqt6mqtt6 отсутствует в Ubuntu/Debian → apt не мог поставить пакет).
DEB_DEPENDS="${SMARTCLIP_DEB_DEPENDS:-libc6, libstdc++6, libxkbcommon0, libxkbcommon-x11-0, libglib2.0-0t64 | libglib2.0-0, libfontconfig1, libfreetype6, libgl1, libegl1, libx11-6, libx11-xcb1, libice6, libsm6, libxcb-cursor0, libxcb-icccm4, libxcb-image0, libxcb-keysyms1, libxcb-randr0, libxcb-render-util0, libxcb-shape0, libxcb-sync1, libxcb-xfixes0, libxcb-xinerama0, libxcb-xkb1, libdbus-1-3, libzstd1, libbrotli1, libb2-1}"

readonly LIBDIR="/opt/smartclip/lib"
readonly PLUGINDIR="/opt/smartclip/plugins"

# ── Каркас пакета ─────────────────────────────────────────────────────────
STAGE="$BUILD_DIR/deb-stage"
rm -rf "$STAGE"
mkdir -p "$STAGE/DEBIAN" \
         "$STAGE/usr/bin" \
         "$STAGE/usr/share/applications" \
         "$STAGE/usr/share/icons/hicolor/512x512/apps" \
         "$STAGE$LIBDIR" \
         "$STAGE$PLUGINDIR/platforms" \
         "$STAGE$PLUGINDIR/imageformats" \
         "$STAGE$PLUGINDIR/iconengines"

cp "$BINARY" "$STAGE/usr/bin/$APP_NAME"
strip --strip-unneeded "$STAGE/usr/bin/$APP_NAME" 2>/dev/null || true
chmod 0755 "$STAGE/usr/bin/$APP_NAME"

# ── Бандл зависимостей ────────────────────────────────────────────────────
# Копируем Qt-библиотеки (по ldd + рекурсивно их собственные зависимости) и
# нужные плагины внутрь пакета, затем правим RUNPATH бинарника на
# $ORIGIN/../lib. Требует patchelf; без него — предупреждение, но пакет
# всё равно соберётся (и упадёт на чужой машине — поэтому лучше fatal).
if command -v patchelf >/dev/null 2>&1; then
    # Системные библиотеки, которые НЕ надо класть в пакет (glibc, драйверы).
    SKIP_RE='^(libc\.so|libm\.so|libdl\.so|libpthread\.so|libresolv\.so|libnsl\.so|ld-linux|libGLX|libGL\.so|libEGL\.so|libOpenGL|libGLdispatch|libdrm|libgcc_s|libstdc\+\+|libmvec)'

    copy_deps() {
        local target="$1" depth="${2:-0}"
        [ "$depth" -gt 3 ] && return 0
        while read -r line; do
            local path
            path="$(printf '%s' "$line" | awk '{ if ($2=="=>") print $3; else print $1 }')"
            case "$path" in
                /*) ;;
                *) continue ;;   # linux-vdso, ld-linux и прочее «не файлы»
            esac
            [ -f "$path" ] || continue
            local base
            base="$(basename "$path")"
            printf '%s' "$base" | grep -qE "$SKIP_RE" && continue
            [ -f "$STAGE$LIBDIR/$base" ] && continue
            cp -L "$path" "$STAGE$LIBDIR/$base"
            copy_deps "$path" $((depth + 1))
        done < <(ldd "$target" 2>/dev/null)
    }
    copy_deps "$BINARY"

    # Плагины: xcb (запуск в сессии), imageformats/iconengines (иконки трея).
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
                    cp -L "$pl" "$STAGE$PLUGINDIR/$sub/"
                    copy_deps "$pl"
                done
        done
    else
        warn "каталог Qt-плагинов не найден — в .deb не будет platforms/libqxcb.so"
    fi

    # $ORIGIN — это /usr/bin, а библиотеки лежат в /opt/smartclip/lib,
    # поэтому путь ОТНОСИТЕЛЬНО $ORIGIN выглядит так (грабля 21.10.2026:
    # «$ORIGIN/../lib» указывал в /usr/lib и Qt не находился).
    patchelf --set-rpath '$ORIGIN/../../opt/smartclip/lib:/opt/smartclip/lib' \
        "$STAGE/usr/bin/$APP_NAME"
    QT_CONF="$STAGE$PLUGINDIR/qt.conf"
    printf '[Paths]\nPrefix=/opt/smartclip\nPlugins=plugins\n' > "$QT_CONF"
    log "Зависимости упакованы в $LIBDIR ($(ls "$STAGE$LIBDIR" | wc -l) файлов)"
else
    die "patchelf не найден — .deb будет слинкован с Qt машины сборки и не запустится в другом месте (pacman -S patchelf / apt install patchelf)."
fi

cp "$PROJECT_DIR/assets/smartclip.desktop" "$STAGE/usr/share/applications/"
cp "$PROJECT_DIR/icons/icon.png" "$STAGE/usr/share/icons/hicolor/512x512/apps/smartclip.png"
chmod 0644 "$STAGE/usr/share/applications/smartclip.desktop" \
           "$STAGE/usr/share/icons/hicolor/512x512/apps/smartclip.png"

# Installed-Size в КБ (округление вверх).
SIZE_KB=$(( ( $(du -sk "$STAGE" | cut -f1) + 1 ) ))

cat > "$STAGE/DEBIAN/control" <<EOF
Package: smartclip
Version: $PKG_VER
Section: utils
Priority: optional
Architecture: amd64
Maintainer: ${SMARTCLIP_DEB_MAINTAINER:-SmartClip <smartclip@halfpi.ru>}
Installed-Size: $SIZE_KB
Depends: $DEB_DEPENDS
Homepage: https://git.halfpi.ru/mario/smartclip
Description: SmartClip - clipboard history manager
 История буфера обмена с шифрованием (AES-256-GCM), трей-интерфейсом,
 избранным, комментариями и опциональной сетевой синхронизацией по MQTT.
EOF

log "Собираю .deb (версия $PKG_VER)…"
dpkg-deb --root-owner-group -Zxz --build "$STAGE" "$OUT" >/dev/null
rm -rf "$STAGE"

log "Готово: $OUT"
ls -lh "$OUT"
echo "Проверка: dpkg-deb -I \"$OUT\""
