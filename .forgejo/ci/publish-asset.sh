#!/usr/bin/env bash
# Заливка ОДНОГО артефакта в релиз Forgejo (используется CI-джобами
# windows/macos, чтобы не дублировать curl-логику в YAML).
#
# Использование: publish-asset.sh <файл> <версия>
#   Токен — из $TOKEN (секрет RELEASE_TOKEN) или ~/.openclaw/forgejo.token.
#
# Релиз v<версия> к этому моменту уже создан джобой linux; если его нет —
# создаём сами (на случай ручного запуска только win/mac-джобы).
set -euo pipefail

FILE="${1:?нужен путь к файлу}"
VERSION="${2:?нужна версия}"
TOKEN="${TOKEN:-$(cat "$HOME/.openclaw/forgejo.token" 2>/dev/null || true)}"
API="https://git.halfpi.ru/api/v1/repos/mario/smartclip"

[ -f "$FILE" ] || { echo "::error::нет файла $FILE"; exit 1; }
[ -n "$TOKEN" ] || { echo "::error::нет токена (TOKEN/RELEASE_TOKEN)"; exit 1; }

TAG="v$VERSION"
NAME="$(basename "$FILE")"

RELEASE_ID="$(curl -sSf -H "Authorization: token $TOKEN" \
    "$API/releases/tags/$TAG" \
  | python3 -c "import sys,json;print(json.load(sys.stdin).get('id',''))" 2>/dev/null || true)"

if [ -z "$RELEASE_ID" ]; then
    echo "релиза $TAG нет — создаю"
    RELEASE_ID="$(curl -sSf -H "Authorization: token $TOKEN" \
        -H 'Content-Type: application/json' -X POST "$API/releases" \
        -d "{\"tag_name\":\"$TAG\",\"name\":\"SmartClip $VERSION\",\"draft\":false,\"prerelease\":false,\"target_commitish\":\"main\"}" \
      | python3 -c "import sys,json;print(json.load(sys.stdin)['id'])")"
fi

# Грабля 21.10.2026: ?name= в Forgejo НЕ фильтр — отдаёт весь список ассетов
# (python видел dict -> id всегда пустой, и дубли копились). Скачиваем список
# и фильтруем по имени сами; удаляем ВСЕ совпадения (лечим и старые дубли).
OLD_IDS="$(curl -s -H "Authorization: token $TOKEN" "$API/releases/$RELEASE_ID/assets" \
  | python3 -c "
import sys, json
name = sys.argv[1]
try:
    data = json.load(sys.stdin)
except Exception:
    data = []
if isinstance(data, dict):
    data = data.get('assets', [])
print(' '.join(str(a['id']) for a in data if a.get('name') == name))
" "$NAME" 2>/dev/null || true)"
for oid in $OLD_IDS; do
    curl -sS -o /dev/null -X DELETE -H "Authorization: token $TOKEN" "$API/releases/$RELEASE_ID/assets/$oid"
    echo "удалён прежний ассет $NAME (id=$oid)"
done

CODE="$(curl -sS -o /dev/null -w '%{http_code}' -H "Authorization: token $TOKEN" \
    -F "attachment=@$FILE" "$API/releases/$RELEASE_ID/assets?name=$NAME")"
echo "asset $NAME: HTTP $CODE"
case "$CODE" in 2*) ;; *) echo "::error::не удалось залить $NAME (HTTP $CODE)"; exit 1;; esac
