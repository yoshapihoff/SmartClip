#!/usr/bin/env bash
# Установка CI deploy-ключа из секрета SSH_DEPLOY_KEY на winpc/imac.
#
# Зачем: setup-ssh.sh готовит ключ только для раннера, но сама машина его не
# знает — без ключа в authorized_keys джобы windows/macos падают с
# «Permission denied (publickey)». Скрипт идемпотентен: ключ добавляется,
# только если его ещё нет.
#
# Использование:
#   .forgejo/ci/install-ci-key.sh winpc   # Windows (админский authorized_keys)
#   .forgejo/ci/install-ci-key.sh imac    # macOS (~/.ssh/authorized_keys)
#
# Нужен РАБОЧИЙ доступ к машине (обычным ключом mario; для winpc — вход под
# администратором, у которого есть право писать в administrators_authorized_keys).
set -euo pipefail

HOST="${1:?укажи host: winpc | imac}"
[ -n "${SSH_KEY:-}" ] || { echo "SSH_KEY не задан" >&2; exit 1; }
TMP="$(mktemp)"; trap 'rm -f "$TMP"' EXIT
printf '%s\n' "$SSH_KEY" | tr -d '\r' > "$TMP"
PUB="$(ssh-keygen -y -f "$TMP") ci-smartclip-console"
echo "==> публичный ключ: ${PUB:0:40}…"

case "$HOST" in
winpc)
    # sshd на Windows для админской группы читает ТОЛЬКО
    # C:\ProgramData\ssh\administrators_authorized_keys (игнорирует ~/.ssh).
    # Пишем через PowerShell, ACL файла не меняем (Owner=Administrators + SYSTEM).
    printf '%s\n' "$PUB" | ssh winpc 'powershell -NoProfile -Command "$k=[Console]::In.ReadToEnd().Trim(); $p=\"C:\\ProgramData\\ssh\\administrators_authorized_keys\"; $c=@(); if (Test-Path $p) { $c=@(Get-Content $p) }; if ($c -notcontains $k) { Set-Content -Path $p -Value (@($c) + $k) -Encoding ASCII; Write-Output \"added\" } else { Write-Output \"already\" }"'
    ;;
imac)
    printf '%s\n' "$PUB" | ssh imac 'umask 077; mkdir -p ~/.ssh; k=$(cat); touch ~/.ssh/authorized_keys; chmod 600 ~/.ssh/authorized_keys; if grep -qF "$k" ~/.ssh/authorized_keys; then echo already; else printf "%s\n" "$k" >> ~/.ssh/authorized_keys; echo added; fi'
    ;;
*)
    echo "неизвестный host: $HOST (ожидается winpc|imac)" >&2; exit 1 ;;
esac
