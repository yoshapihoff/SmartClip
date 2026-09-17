#!/usr/bin/env bash
# Build SmartClip AppImage (portable, no system Qt6 required)
set -euo pipefail

APP=SmartClip
BINARY=build-linux/$APP
OUT=SmartClip-x86_64.AppImage
QTPREFIX=/usr
QTPLUGINS=/usr/lib/qt6/plugins

[ -f "$BINARY" ] || { echo "Build binary first: make build"; exit 1; }

echo "==> Creating AppDir"
rm -rf *.AppDir
mkdir -p SmartClip.AppDir/usr/bin SmartClip.AppDir/usr/lib
mkdir -p SmartClip.AppDir/usr/share/applications SmartClip.AppDir/usr/share/icons/hicolor/256x256/apps

cp "$BINARY" SmartClip.AppDir/usr/bin/

cat > SmartClip.AppDir/usr/share/applications/smartclip.desktop << 'EOF'
[Desktop Entry]
Type=Application
Name=SmartClip
Comment=Clipboard History Manager
Exec=SmartClip
Icon=smartclip
Terminal=false
Categories=Utility;
EOF

cp icons/icon.png SmartClip.AppDir/usr/share/icons/hicolor/256x256/apps/smartclip.png
cp SmartClip.AppDir/usr/share/icons/hicolor/256x256/apps/smartclip.png SmartClip.AppDir/

cat > SmartClip.AppDir/AppRun << 'BINRUN'
#!/usr/bin/env bash
DIR="$(cd "$(dirname "$0")" && pwd)"
export QT_QPA_PLATFORM_PLUGIN_PATH="$DIR/usr/plugins"
exec "$DIR/usr/bin/SmartClip" "$@"
BINRUN
chmod +x SmartClip.AppDir/AppRun

# Step 1: Deploy Qt6 using linuxdeploy
echo "==> Deploying Qt6 via linuxdeploy..."
export DONT_STRIP=1 NO_STRIP=1 QMAKE=/usr/bin/qmake6
/tmp/linuxdeploy-x86_64.AppImage \
  --appdir SmartClip.AppDir \
  --executable SmartClip.AppDir/usr/bin/SmartClip \
  --plugin qt 2>&1 | tail -1

# Step 2: Add blacklisted system libs (needed because Qt .so have RUNPATH=$ORIGIN)
echo "==> Adding system libs..."
for lib in libstdc++.so.6 libgcc_s.so.1 libc.so.6 libm.so.6 libz.so.1 \
           libEGL.so.1 libGLX.so.0 libOpenGL.so.0 libX11.so.6 libxcb.so.1 \
           libfontconfig.so.1 libfreetype.so.6 libharfbuzz.so.0 libexpat.so.1 \
           libGLdispatch.so.0 libSM.so.6 libICE.so.6 libuuid.so.1 libX11-xcb.so.1; do
    src="/usr/lib/$lib"
    dst="SmartClip.AppDir/usr/lib/$lib"
    [ -f "$src" ] && [ ! -f "$dst" ] && cp -L "$src" "$dst"
done

# Step 3: Restore original binary (linuxdeploy adds rpath+strip)
echo "==> Restoring original binary..."
cp "$BINARY" SmartClip.AppDir/usr/bin/SmartClip

# Step 4: Package into AppImage
echo "==> Packaging..."
/tmp/appimagetool-x86_64.AppImage SmartClip.AppDir "$OUT" 2>&1 | grep -E 'Creating|Embedding|Success|Marking'

# Step 5: Verify
echo ""
echo "✅ $OUT"
ls -lh "$OUT"
echo ""
echo "Run: ./$OUT"
echo "Test (headless): QT_QPA_PLATFORM=offscreen ./$OUT"