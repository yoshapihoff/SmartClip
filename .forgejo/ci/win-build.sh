#!/usr/bin/env bash
# Сборочный скрипт для Windows-машины (winpc). Запускается MSYS2-bash'ем
# по SSH из CI-джобы windows. Раскладывается в C:/Users/mario/sc-win-build.sh.
#
# Почему так: sshd на winpc отдаёт cmd, поэтому нужен именно bash из MSYS2,
# и PATH приходится собирать руками — иначе не находятся базовые утилиты.
# Qt кладётся ПЕРЕД MSYS2-минтой: иначе подхватится чужой g++/DLL.
#
# С 21.10.2026 собирает И портативный ZIP, И установщик Inno Setup —
# через единый scripts/build-installer-windows.sh (он сам находит ISCC).
set -euo pipefail

export PATH="/usr/bin:/bin:/usr/local/bin:$PATH"

QT_VER="${QT_VER:-6.8.3}"
QT_DIR="${QT_DIR:-C:/Qt/$QT_VER/mingw_64}"
WORK="$(cygpath -u 'C:/Users/mario' 2>/dev/null || echo /c/Users/mario)"
SRC="$WORK/smartclip-src"

echo "== Windows build: Qt=$QT_DIR =="

rm -rf "$SRC"
mkdir -p "$SRC"
tar -xzf "$WORK/smartclip-src.tar.gz" -C "$SRC"
cd "$SRC"

export QT_PATH="$QT_DIR"
export PATH="/c/Qt/Tools/mingw1310_64/bin:/c/Qt/Tools/CMake/bin:/c/Qt/Tools/Ninja:$PATH"

# OpenSSL 3 для MinGW: CMake его сам не находит (грабля 20.10.2026 и 21.10.2026 —
# сборка падала «Could NOT find OpenSSL»). Пакет ставится msys2:
#   C:\msys64\usr\bin\pacman.exe -S --noconfirm mingw-w64-x86_64-openssl
# ВАЖНО: /c/msys64/mingw64/bin в PATH НЕ добавляем — подхватится чужой g++/DLL,
# только указываем корень через OPENSSL_ROOT_DIR (--openssl).
export OPENSSL_ROOT_DIR="${OPENSSL_ROOT_DIR:-C:/msys64/mingw64}"
echo "== OpenSSL: $OPENSSL_ROOT_DIR =="

# Inno Setup 6 на winpc живёт в профиле пользователя (установка /CURRENTUSER,
# без UAC). Ищем его здесь явно: в PATH ISCC не попадает.
if [ -z "${ISCC:-}" ]; then
    for c in /c/Users/mario/InnoSetup6/ISCC.exe \
             "/c/Program Files (x86)/Inno Setup 6/ISCC.exe" \
             "/c/Program Files/Inno Setup 6/ISCC.exe"; do
        if [ -f "$c" ]; then export ISCC="$c"; break; fi
    done
fi
if [ -n "${ISCC:-}" ]; then
    echo "== ISCC: $ISCC =="
else
    echo "::warning::Inno Setup не найден — соберётся только портативный ZIP" >&2
fi

# Собирает ZIP + setup.exe (+ SHA256SUMS) в dist/release-<ver>/
bash scripts/build-installer-windows.sh --jobs 4

echo "== артефакты =="
ls -l dist/release-*/
