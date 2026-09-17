#!/usr/bin/env bash
set -euo pipefail

APP=SmartClip
BINARY=build-linux/$APP
OUT=SmartClip-portable
QTPREFIX=/usr
QTPLUGINS=/usr/lib/qt6/plugins

[ -f "$BINARY" ] || { echo "❌ Build binary first: make build"; exit 1; }

echo "==> Creating bundle: $OUT"
rm -rf "$OUT"
mkdir -p "$OUT/lib" "$OUT/plugins/platforms" "$OUT/plugins/iconengines" "$OUT/plugins/imageformats"

# 1. Binary
cp "$BINARY" "$OUT/$APP"

# 2. Qt libs
for lib in libQt6Core.so.6 libQt6Gui.so.6 libQt6Widgets.so.6 libQt6Svg.so.6 libQt6DBus.so.6 libQt6XcbQpa.so.6; do
    cp -L "$QTPREFIX/lib/$lib" "$OUT/lib/"
done

# 3. Transitive deps of Qt libs (everything except glibc core)
declare -A SEEN
for f in "$OUT"/lib/*.so*; do
    while IFS= read -r line; do
        path=$(echo "$line" | awk '{print $3}')
        [ -z "$path" ] && continue
        [ ! -f "$path" ] && continue
        base=$(basename "$path")
        # Skip glibc essentials & linker
        case "$base" in
            ld-linux-*|linux-vdso*|libc.so.*|libm.so.*|libdl.so.*|libpthread*|librt*|libresolv*|libnss*) continue ;;
        esac
        [ -n "${SEEN[$base]:-}" ] && continue
        SEEN[$base]=1
        [ -f "$OUT/lib/$base" ] && continue
        cp -L "$path" "$OUT/lib/"
    done < <(ldd "$f" 2>/dev/null)
done

# 4. Plugins
for plug in platforms/libqxcb.so platforms/libqoffscreen.so \
            iconengines/libqsvgicon.so \
            imageformats/libqsvg.so imageformats/libqjpeg.so; do
    src="$QTPLUGINS/$plug"
    [ -f "$src" ] && cp -L "$src" "$OUT/plugins/$plug"
done

# 5. Plugin transitive deps
for f in "$OUT"/plugins/*/*.so; do
    while IFS= read -r line; do
        path=$(echo "$line" | awk '{print $3}')
        [ -z "$path" ] && continue
        [ ! -f "$path" ] && continue
        base=$(basename "$path")
        case "$base" in
            ld-linux-*|linux-vdso*|libc.so.*|libm.so.*|libdl.so.*|libpthread*|librt*|libresolv*|libnss*) continue ;;
        esac
        [ -n "${SEEN[$base]:-}" ] && continue
        SEEN[$base]=1
        [ -f "$OUT/lib/$base" ] && continue
        cp -L "$path" "$OUT/lib/"
    done < <(ldd "$f" 2>/dev/null)
done

# 6. Remove full-versioned dupes (keep .6, remove .6.11.2)
for f in "$OUT"/lib/*.so.*.*; do
    [ -f "$f" ] || continue
    base=$(basename "$f")
    shortver="${base##*.so.}"; shortver="${shortver%%.*}"
    short="${base%%.*}.so.${shortver}"
    [ -f "$OUT/lib/$short" ] && rm -f "$f"
done

# 7. Do NOT patch binary rpath — use LD_LIBRARY_PATH in launcher instead
# RUNPATH on Qt .so prevents proper fallback to system libc/libm/etc

# 8. Launcher
cat > "$OUT/run.sh" << 'RSCRIPT'
#!/usr/bin/env bash
DIR="$(cd "$(dirname "$0")" && pwd)"
export LD_LIBRARY_PATH="$DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_QPA_PLATFORM_PLUGIN_PATH="$DIR/plugins"
exec "$DIR/SmartClip" "$@"
RSCRIPT
chmod +x "$OUT/run.sh"

# 9. Desktop file
cat > "$OUT/smartclip.desktop" << 'DESKTOP'
[Desktop Entry]
Type=Application
Name=SmartClip
Comment=Clipboard History Manager (portable)
Exec=run.sh
Icon=smartclip
Terminal=false
Categories=Utility;
Keywords=clipboard;history;tray;
DESKTOP

# 10. Sanity
echo "  Sanity check..."
declare -i UNRES=0
for f in "$OUT/$APP" "$OUT"/lib/*.so* "$OUT"/plugins/*/*.so; do
    [ -f "$f" ] || continue
    out=$(LD_LIBRARY_PATH="$OUT/lib" ldd "$f" 2>&1 | grep 'not found') || true
    if [ -n "$out" ]; then
        echo "    ! ${f#$OUT/}"
        while IFS= read -r line; do echo "      $line"; done <<< "$out"
        UNRES+=1
    fi
done

echo ""
echo "✅ Portable bundle: $OUT"
du -sh "$OUT"
find "$OUT" -type f | sort | while read -r f; do
    echo "  $(du -h "$f" | cut -f1)  ${f#$OUT/}"
done
[ "$UNRES" -gt 0 ] && echo "⚠️  $UNRES file(s) with unresolved symbols (may fail on other systems)" || echo "✅ No unresolved symbols"