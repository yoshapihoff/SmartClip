#!/usr/bin/env bash
# Готовит SSH для CI-джоб windows/macos (вариант 2, решение Лёши 20.10.2026):
# приватный deploy-ключ приходит из секрета репозитория SSH_DEPLOY_KEY, а не
# из домашней папки mario (у пользователя `runner` нет туда доступа).
#
# Создаёт рабочий ssh-config с алиасами winpc/imac и экспортирует
# GIT_SSH_COMMAND, чтобы последующие шаги видели настройку.
set -euo pipefail

DEST="${RUNNER_TEMP:-/tmp}/smartclip-ssh"
mkdir -p "$DEST"
chmod 700 "$DEST"

if [ -z "${SSH_KEY:-}" ]; then
    echo "::error::секрет SSH_DEPLOY_KEY пуст — SSH к winpc/iMac невозможен"
    exit 1
fi

# Ключ из секрета: переводы строк могут прийти с \r, чистим.
printf '%s\n' "$SSH_KEY" | tr -d '\r' > "$DEST/deploy_key"
chmod 600 "$DEST/deploy_key"
ssh-keygen -y -f "$DEST/deploy_key" >/dev/null \
    || { echo "::error::SSH_DEPLOY_KEY не похож на приватный ключ"; exit 1; }

cat > "$DEST/config" <<EOF
Host winpc
    HostName 192.168.0.18
    User mario
    IdentityFile $DEST/deploy_key
    IdentitiesOnly yes
    StrictHostKeyChecking accept-new

Host imac
    HostName 192.168.0.144
    User mario
    IdentityFile $DEST/deploy_key
    IdentitiesOnly yes
    StrictHostKeyChecking accept-new
EOF
chmod 600 "$DEST/config"

if [ -n "${GITHUB_ENV:-}" ]; then
    {
        echo "SMARTCLIP_SSH_CONFIG=$DEST/config"
        echo "GIT_SSH_COMMAND=ssh -F $DEST/config"
    } >> "$GITHUB_ENV"
fi
echo "SSH готов: $DEST/config (ключ из секрета SSH_DEPLOY_KEY)"
