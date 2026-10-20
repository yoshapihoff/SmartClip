#!/usr/bin/env bash
# Деплой релиза SmartClip на ВСЕ платформы: Linux + Windows + macOS.
#
# Инициируется ВРУЧНУЮ (правило Лёши, 20.10.2026): сначала убедиться, что
# все машины в нужной сети, потом запускать. Скрипт НЕ трогает сеть сам,
# но заранее проверяет доступность хостов и падает с понятным сообщением,
# если машина недоступна.
#
# Что делает:
#   1. Проверяет версию в VERSION.txt и доступность SSH-хостов.
#   2. Linux  — собирает AppImage/deb/tar.gz ЛОКАЛЬНО (на этой машине).
#   3. Windows — rsync/scp исходников на winpc, сборка там, артефакт обратно.
#   4. macOS  — rsync/scp исходников на imac, сборка там, артефакты обратно.
#   5. Собирает общий каталог dist/release-<version>/ + SHA256SUMS.
#   6. Публикует/обновляет релиз v<version> в Forgejo и заливает ассеты.
#
# Использование:
#   scripts/release-all.sh [опции]
#     --version X.Y.Z     версия (по умолчанию — из VERSION.txt)
#     --linux-only        только Linux
#     --skip-linux        не собирать Linux (взять готовые из dist/)
#     --skip-windows      пропустить Windows
#     --skip-macos        пропустить macOS
#     --no-publish        не публиковать в Forgejo (только собрать локально)
#     --dry-run           показать, что будет сделано, и выйти
#     --host-win HOST     SSH-алиас для Windows (по умолчанию winpc)
#     --host-mac HOST     SSH-алиас для macOS   (по умолчанию imac)
#     --jobs N            параллелизм сборки (по умолчанию: nproc)
#
# Переменные окружения:
#   SMARTCLIP_QT_DIR    путь к Qt (пробрасывается в сборку; на Linux — и в
#                       linuxdeploy через QMAKE) — включает MQTT, если в этом
#                       Qt есть модуль Qt6Mqtt
#   RELEASE_TOKEN       токен Forgejo для публикации (иначе берётся из
#                       ~/.openclaw/forgejo.token)
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

readonly FORGEJO_API="https://git.halfpi.ru/api/v1/repos/mario/smartclip"
readonly REMOTE_TMP="/tmp/smartclip-release"

DO_LINUX=1
DO_WINDOWS=1
DO_MACOS=1
DO_PUBLISH=1
DRY_RUN=0
REL_VER=""
JOBS="$(nproc 2>/dev/null || echo 4)"
HOST_WIN="winpc"
HOST_MAC="imac"

while [ $# -gt 0 ]; do
    case "$1" in
        --linux-only)    DO_WINDOWS=0; DO_MACOS=0 ;;
        --skip-linux)    DO_LINUX=0 ;;
        --skip-windows)  DO_WINDOWS=0 ;;
        --skip-macos)    DO_MACOS=0 ;;
        --no-publish)    DO_PUBLISH=0 ;;
        --dry-run)       DRY_RUN=1 ;;
        --version)       shift; REL_VER="${1:?--version требует X.Y.Z}" ;;
        --jobs)          shift; JOBS="${1:?--jobs требует число}" ;;
        --host-win)      shift; HOST_WIN="${1:?--host-win требует хост}" ;;
        --host-mac)      shift; HOST_MAC="${1:?--host-mac требует хост}" ;;
        -h|--help)       sed -n '2,40p' "$0"; exit 0 ;;
        *) die "неизвестный аргумент: $1" ;;
    esac
    shift
done

cd "$PROJECT_DIR"

if [ -z "$REL_VER" ]; then
    REL_VER="$(tr -d '[:space:]' < "$PROJECT_DIR/VERSION.txt")"
fi
[[ "$REL_VER" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "некорректная версия: '$REL_VER'"

REL_DIR="$DIST_DIR/release-$REL_VER"
ensure_dist
mkdir -p "$REL_DIR"

# ── 0. Предполётная проверка ─────────────────────────────────────────────
log "Релиз SmartClip $REL_VER"
[ -n "${SMARTCLIP_QT_DIR:-}" ] && log "Qt: $SMARTCLIP_QT_DIR" || warn "SMARTCLIP_QT_DIR не задан — Linux соберётся на системном Qt (MQTT, скорее всего, недоступен)"
log "Платформы: linux=$DO_LINUX windows=$DO_WINDOWS macos=$DO_MACOS publish=$DO_PUBLISH"

check_ssh() {
    # Проверка «машина в сети и пускает» — ДО любой сборки.
    # winpc отдаёт cmd в кодировке CP866, а не POSIX-shell: `ssh host true`
    # падает с «не является внутренней командой», хотя доступ жив (грабля
    # 20.10.2026). Поэтому печатаем ASCII-метку и ищем её.
    local host="$1" out
    out="$(timeout 25 ssh -o ConnectTimeout=10 -o BatchMode=yes "$host" \
        'echo SMARTCLIP_SSH_OK' 2>&1 || true)"
    # cmd отвечает на русском в CP866, поэтому вместо поиска «error» ищем метку.
    grep -q SMARTCLIP_SSH_OK <<<"$out"
}

if [ "$DO_WINDOWS" = 1 ]; then
    log "Проверяю $HOST_WIN …"
    check_ssh "$HOST_WIN" || die "нет SSH-доступа к '$HOST_WIN'. Проверь, что машина включена и в нужной сети (правило: деплой только когда все машины в сети)."
    # Виртуальная метка: на самом деле это cmd-проверка, см. check_ssh.
    log "$HOST_WIN доступен"
fi

if [ "$DO_MACOS" = 1 ]; then
    log "Проверяю $HOST_MAC …"
    check_ssh "$HOST_MAC" || die "нет SSH-доступа к '$HOST_MAC'. Проверь, что iMac включён и его Wi-Fi в сети RT-GPON-7A88 (иначе он выпадает из 192.168.0.0/24)."
    log "$HOST_MAC доступен"
fi

if [ "$DRY_RUN" = 1 ]; then
    echo
    log "---dry-run: план---"
    [ "$DO_LINUX" = 1 ]   && echo "  Linux:   scripts/release-linux.sh --jobs $JOBS"
    [ "$DO_WINDOWS" = 1 ] && echo "  Windows: ssh $HOST_WIN → scripts/release-windows.sh --jobs $JOBS → забрать артефакты"
    [ "$DO_MACOS" = 1 ]   && echo "  macOS:   ssh $HOST_MAC → scripts/release-macos.sh --jobs $JOBS → забрать артефакты"
    [ "$DO_PUBLISH" = 1 ] && echo "  Публикация: релиз v$REL_VER в Forgejo ($REL_DIR/*)"
    exit 0
fi

# ── 1. Linux (локально) ──────────────────────────────────────────────────
if [ "$DO_LINUX" = 1 ]; then
    log "[1] Linux: сборка локально…"
    "$SCRIPT_DIR/release-linux.sh" --jobs "$JOBS"
else
    warn "[1] Linux пропущен"
fi

# ── 2. Windows (winpc по SSH) ────────────────────────────────────────────
# Схема: копируем рабочую копию (git archive — чисто и без мусора), собираем
# удалённо в MSYS2-bash, забираем готовый zip. rsync на winpc НЕТ, поэтому
# tar через ssh.
if [ "$DO_WINDOWS" = 1 ]; then
    log "[2] Windows: отправляю исходники на $HOST_WIN…"
    SRC_TAR="$REMOTE_TMP-src.tar.gz"
    git archive --format=tar.gz -o "$SRC_TAR" HEAD
    scp -q "$SRC_TAR" "$HOST_WIN:C:/Users/mario/smartclip-src.tar.gz"
    rm -f "$SRC_TAR"

    log "[2] Windows: сборка на $HOST_WIN (это долго: Qt + windeployqt)…"
    # Отдельный bash-скрипт: sshd на winpc отдаёт cmd, нужен MSYS2-bash и
    # нормальный PATH (иначе не находятся mkdir/tar и т.п.).
    cat > /tmp/sc-win-build.sh <<'WINEOF'
export PATH="/usr/bin:/bin:/usr/local/bin:$PATH"
set -euo pipefail
cd /c/Users/mario
rm -rf smartclip-src && mkdir -p smartclip-src
tar -xzf smartclip-src.tar.gz -C smartclip-src
cd smartclip-src
export QT_PATH="${QT_PATH:-C:/Qt/6.8.3/mingw_64}"
export PATH="/c/Qt/Tools/mingw1310_64/bin:/c/Qt/Tools/CMake/bin:/c/Qt/Tools/Ninja:$PATH"
bash scripts/release-windows.sh --jobs 4
ls -l dist/release-*/
WINEOF
    scp -q /tmp/sc-win-build.sh "$HOST_WIN:C:/Users/mario/sc-win-build.sh"
    ssh "$HOST_WIN" 'cmd /c "C:\msys64\usr\bin\bash.exe /c/Users/mario/sc-win-build.sh 2>&1"' \
        || die "сборка Windows упала (см. вывод выше)"

    log "[2] Windows: забираю артефакты…"
    WIN_ZIP="SmartClip-$REL_VER-windows-x86_64.zip"
    scp -q "$HOST_WIN:C:/Users/mario/smartclip-src/dist/release-$REL_VER/$WIN_ZIP" "$REL_DIR/" \
        || warn "не забрал $WIN_ZIP — проверь вывод сборки выше"
else
    warn "[2] Windows пропущен"
fi

# ── 3. macOS (imac по SSH) ───────────────────────────────────────────────
if [ "$DO_MACOS" = 1 ]; then
    log "[3] macOS: отправляю исходники на $HOST_MAC…"
    SRC_TAR="$REMOTE_TMP-src.tar.gz"
    git archive --format=tar.gz -o "$SRC_TAR" HEAD
    scp -q "$SRC_TAR" "$HOST_MAC:$REMOTE_TMP-src.tar.gz"
    rm -f "$SRC_TAR"

    log "[3] macOS: сборка на $HOST_MAC (это долго: Qt + macdeployqt)…"
    ssh "$HOST_MAC" "set -eu; rm -rf $REMOTE_TMP && mkdir -p $REMOTE_TMP && \
        tar -xzf $REMOTE_TMP-src.tar.gz -C $REMOTE_TMP && cd $REMOTE_TMP && \
        bash scripts/release-macos.sh --jobs 4 && ls -l dist/release-*/" \
        || die "сборка macOS упала (см. вывод выше)"

    log "[3] macOS: забираю артефакты…"
    for f in "SmartClip-$REL_VER-macos-x86_64.zip" "SmartClip-$REL_VER-macos-x86_64.dmg"; do
        scp -q "$HOST_MAC:$REMOTE_TMP/dist/release-$REL_VER/$f" "$REL_DIR/" \
            || warn "не забрал $f"
    done
else
    warn "[3] macOS пропущен"
fi

# ── 4. Сводный каталог + контрольные суммы ───────────────────────────────
log "[4] Собираю итоговый каталог $REL_DIR"
shopt -s nullglob
ASSETS=("$REL_DIR"/*)
[ ${#ASSETS[@]} -gt 0 ] || die "в $REL_DIR пусто — нечего публиковать"
rm -f "$REL_DIR/SHA256SUMS"
( cd "$REL_DIR" && sha256sum ./* > SHA256SUMS )

echo
log "Готовые артефакты:"
ls -lh "$REL_DIR"

# ── 5. Публикация в Forgejo ──────────────────────────────────────────────
if [ "$DO_PUBLISH" = 0 ]; then
    warn "[5] Публикация пропущена (--no-publish)"
    exit 0
fi

TOKEN="${RELEASE_TOKEN:-$(cat "$HOME/.openclaw/forgejo.token" 2>/dev/null || true)}"
[ -n "$TOKEN" ] || die "нет токена: задай RELEASE_TOKEN или положи токен в ~/.openclaw/forgejo.token"

log "[5] Публикация релиза v$REL_VER в Forgejo…"
TAG="v$REL_VER"
RELEASE_ID="$(curl -sSf -H "Authorization: token $TOKEN" "$FORGEJO_API/releases/tags/$TAG" \
    | python3 -c "import sys,json;print(json.load(sys.stdin).get('id',''))" 2>/dev/null || true)"

if [ -n "$RELEASE_ID" ]; then
    log "релиз $TAG уже есть (id=$RELEASE_ID) — обновляю ассеты"
else
    RELEASE_ID="$(curl -sSf -H "Authorization: token $TOKEN" \
        -H 'Content-Type: application/json' -X POST "$FORGEJO_API/releases" \
        -d "{\"tag_name\":\"$TAG\",\"name\":\"SmartClip $REL_VER\",\"body\":\"Релиз SmartClip $REL_VER: Linux, Windows, macOS.\",\"draft\":false,\"prerelease\":false,\"target_commitish\":\"main\"}" \
        | python3 -c "import sys,json;print(json.load(sys.stdin)['id'])")"
    log "создан релиз $TAG (id=$RELEASE_ID)"
fi

for f in "$REL_DIR"/*; do
    [ -f "$f" ] || continue
    name="$(basename "$f")"
    OLD_ID="$(curl -s -H "Authorization: token $TOKEN" "$FORGEJO_API/releases/$RELEASE_ID/assets?name=$name" \
        | python3 -c "import sys,json;d=json.load(sys.stdin);print(d.get('id','') if isinstance(d,dict) else '')" 2>/dev/null || true)"
    [ -n "$OLD_ID" ] && curl -sS -o /dev/null -X DELETE -H "Authorization: token $TOKEN" \
        "$FORGEJO_API/releases/$RELEASE_ID/assets/$OLD_ID"
    code="$(curl -sS -o /dev/null -w '%{http_code}' -H "Authorization: token $TOKEN" \
        -F "attachment=@$f" "$FORGEJO_API/releases/$RELEASE_ID/assets?name=$name")"
    echo "  asset $name: HTTP $code"
    case "$code" in 2*) ;; *) die "не удалось залить $name (HTTP $code)";; esac
done

echo
log "Готово: https://git.halfpi.ru/mario/smartclip/releases/tag/$TAG"
