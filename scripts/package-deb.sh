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
# Важно: пакет содержит бинарник, слинкованный с системным Qt6 (см. build-linux.sh).
# Для «дистрибутивного» .deb его лучше собирать на Debian/Ubuntu (или в CI-образе
# с системным Qt6). Здесь же — удобный рабочий пакет под текущий стек.
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

# Версия: аргумент → файл VERSION.
if [ -z "$PKG_VER" ]; then
    PKG_VER="$(tr -d '[:space:]' < "$PROJECT_DIR/VERSION")"
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
DEB_DEPENDS="${SMARTCLIP_DEB_DEPENDS:-libc6, libstdc++6, libssl3 | libssl3t64, libqt6core6 | libqt6core6t64, libqt6gui6, libqt6widgets6, libqt6svg6, libqt6network6, libqt6dbus6, libqt6mqtt6}"

# ── Каркас пакета ─────────────────────────────────────────────────────────
STAGE="$BUILD_DIR/deb-stage"
rm -rf "$STAGE"
mkdir -p "$STAGE/DEBIAN" \
         "$STAGE/usr/bin" \
         "$STAGE/usr/share/applications" \
         "$STAGE/usr/share/icons/hicolor/512x512/apps"

cp "$BINARY" "$STAGE/usr/bin/$APP_NAME"
strip --strip-unneeded "$STAGE/usr/bin/$APP_NAME" 2>/dev/null || true
chmod 0755 "$STAGE/usr/bin/$APP_NAME"

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
