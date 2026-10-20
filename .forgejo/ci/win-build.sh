#!/usr/bin/env bash
# Сборочный скрипт для Windows-машины (winpc). Запускается MSYS2-bash'ем
# по SSH из CI-джобы windows. Раскладывается в C:/Users/mario/sc-win-build.sh.
#
# Почему так: sshd на winpc отдаёт cmd, поэтому нужен именно bash из MSYS2,
# и PATH приходится собирать руками — иначе не находятся базовые утилиты.
# Qt кладётся ПЕРЕД MSYS2-минтой: иначе подхватится чужой g++/DLL.
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

bash scripts/release-windows.sh --jobs 4

echo "== артефакты =="
ls -l dist/release-*/
