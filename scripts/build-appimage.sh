#!/usr/bin/env bash
# Сборка SmartClip-x86_64.AppImage (портативно, БЕЗ системного Qt6/OpenSSL).
#
# Одной командой: скачивает linuxdeploy + appimagetool (если нужно), собирает
# бинарник, деплоит в него Qt6/OpenSSL/libsecret и упаковывает AppDir в AppImage.
#
# Использование:
#   scripts/build-appimage.sh [--skip-build] [--jobs N] [--out FILE]
#     --skip-build  использовать уже собранный build-linux/SmartClip
#     --jobs N      параллелизм сборки (по умолчанию: nproc)
#     --out FILE    имя выходного файла (по умолчанию SmartClip-x86_64.AppImage)
#
# Зависимости инструментов: curl, file, patchelf (для linuxdeploy), bash.
# Всё остальное Qt/OpenSSL linuxdeploy забирает из системы автоматически.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

BUILD_DIR="build-linux"
BINARY="$BUILD_DIR/$APP_NAME"
OUT="$APP_NAME-x86_64.AppImage"
JOBS="$(nproc 2>/dev/null || echo 2)"
DO_BUILD=1

while [ $# -gt 0 ]; do
    case "$1" in
        --skip-build) DO_BUILD=0 ;;
        --jobs) shift; JOBS="${1:?--jobs требует число}" ;;
        --out) shift; OUT="${1:?--out требует имя файла}" ;;
        -h|--help) sed -n '2,14p' "$0"; exit 0 ;;
        *) die "неизвестный аргумент: $1" ;;
    esac
    shift
done

[ "$(uname -s)" = "Linux" ] || die "AppImage собирается только на Linux."

cd "$PROJECT_DIR"

# ── 1. Инструменты упаковки ──────────────────────────────────────────────
TOOL_BASE="https://github.com"
fetch_tool "$TOOL_BASE/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage" \
           linuxdeploy-x86_64.AppImage
fetch_tool "$TOOL_BASE/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage" \
           linuxdeploy-plugin-qt-x86_64.AppImage
fetch_tool "$TOOL_BASE/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage" \
           appimagetool-x86_64.AppImage

# ── 2. Сборка бинарника ──────────────────────────────────────────────────
if [ "$DO_BUILD" -eq 1 ]; then
    log "Сборка бинарника (Release)…"
    cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release >/dev/null
    cmake --build "$BUILD_DIR" --config Release --parallel "$JOBS"
fi
[ -f "$BINARY" ] || die "нет бинарника $BINARY (собери без --skip-build)."

# ── 3. Каркас AppDir ─────────────────────────────────────────────────────
APPDIR="$APP_NAME.AppDir"
log "Готовлю AppDir…"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" \
         "$APPDIR/usr/share/applications" \
         "$APPDIR/usr/share/icons/hicolor/512x512/apps"

cp "$BINARY" "$APPDIR/usr/bin/$APP_NAME"
cp assets/smartclip.desktop "$APPDIR/usr/share/applications/"
sed -i "s#^Exec=.*#Exec=$APP_NAME#" "$APPDIR/usr/share/applications/smartclip.desktop"
cp icons/icon.png "$APPDIR/usr/share/icons/hicolor/512x512/apps/smartclip.png"
cp icons/icon.png "$APPDIR/smartclip.png"
cp assets/smartclip.desktop "$APPDIR/smartclip.desktop"
sed -i "s#^Exec=.*#Exec=$APP_NAME#" "$APPDIR/smartclip.desktop"

# ── 4. Деплой Qt6/OpenSSL/libsecret ──────────────────────────────────────
log "Деплой зависимостей (linuxdeploy + qt plugin)…"
# linuxdeploy-plugin-qt подхватывается автоматически из того же каталога.
export PATH="$TOOLS_DIR:$PATH"
export QMAKE="$(command -v qmake6 || command -v qmake)"
# Qt-библиотеки имеют RUNPATH $ORIGIN → strip на них падает и валит
# “Failed to execute deferred operations”. Отключаем strip.
export NO_STRIP=1 DONT_STRIP=1
run_appimage_tool "$TOOLS_DIR/linuxdeploy-x86_64.AppImage" \
    --appdir "$APPDIR" \
    --plugin qt \
    --executable "$APPDIR/usr/bin/$APP_NAME" \
    --desktop-file "$APPDIR/smartclip.desktop" \
    --icon-file "$APPDIR/smartclip.png" \
    2>&1 | tail -5

# Offscreen-плагин: linuxdeploy тянет только xcb, а нам нужен headless-режим
# (тесты/CI, `QT_QPA_PLATFORM=offscreen`). Копируем из системы, если есть.
QTPLUGINS="$(qmake6 -query QT_INSTALL_PLUGINS 2>/dev/null || echo /usr/lib/qt6/plugins)"
if [ -f "$APPDIR/usr/plugins/platforms/libqxcb.so" ] \
   && [ ! -f "$APPDIR/usr/plugins/platforms/libqoffscreen.so" ] \
   && [ -f "$QTPLUGINS/platforms/libqoffscreen.so" ]; then
    cp "$QTPLUGINS/platforms/libqoffscreen.so" "$APPDIR/usr/plugins/platforms/"
    log "Добавлен offscreen-плагин (headless-режим)."
fi

# ── 5. Упаковка ──────────────────────────────────────────────────────────
log "Упаковываю AppImage…"
run_appimage_tool "$TOOLS_DIR/appimagetool-x86_64.AppImage" "$APPDIR" "$OUT" 2>&1 \
    | grep -E 'Creating|Embedding|Success|Marking|bytes' || true

[ -f "$OUT" ] || die "AppImage не создан."
echo
log "Готово: $OUT"
ls -lh "$OUT"
echo
echo "Запуск:            ./$OUT"
echo "Проверка (headless): QT_QPA_PLATFORM=offscreen ./$OUT"
