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

# ── Исходники: winpc тянет их САМ по git (решение 24.10.2026) ──────
# Раньше CI клал на машину tar-архив (smartclip-src.tar.gz) — приходилось
# каждый раз заливать его вручную. Теперь клон делается здесь же:
#   • ключ машины: ~/.ssh/id_ed25519_smartclip (deploy-ключ Forgejo, read-only,
#     добавлен в репозиторий mario/smartclip);
#   • SSH из MSYS2 НЕ читает ~/.ssh/config (ssh -G упорно отдаёт порт 22 и
#     стандартные identity), поэтому порт/ключ передаём через GIT_SSH_COMMAND.
#     Грабля 24.10.2026: без этого git clone падал «Permission denied (publickey)».
#   • повторный прогон — fetch + reset --hard, чтобы не клонировать с нуля.
REPO_URL="${SMARTCLIP_REPO:-ssh://git@git.halfpi.ru:2222/mario/smartclip.git}"
SSH_KEY="${SMARTCLIP_SSH_KEY:-/c/Users/mario/.ssh/id_ed25519_smartclip}"
export GIT_SSH_COMMAND="ssh -i $SSH_KEY -o IdentitiesOnly=yes -o StrictHostKeyChecking=accept-new -p 2222"

if [ -d "$SRC/.git" ]; then
    echo "== git fetch (обновление существующего клона) =="
    git -C "$SRC" fetch --depth 1 origin main
    git -C "$SRC" reset --hard FETCH_HEAD
else
    echo "== git clone $REPO_URL =="
    rm -rf "$SRC"
    git clone --depth 1 "$REPO_URL" "$SRC"
fi
cd "$SRC"
echo "== исходники: $(cat VERSION.txt 2>/dev/null) @ $(git rev-parse --short HEAD) =="

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
