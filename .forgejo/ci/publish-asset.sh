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

OLD_ID="$(curl -s -H "Authorization: token $TOKEN" "$API/releases/$RELEASE_ID/assets?name=$NAME" \
  | python3 -c "import sys,json;d=json.load(sys.stdin);print(d.get('id','') if isinstance(d,dict) else '')" 2>/dev/null || true)"
if [ -n "$OLD_ID" ]; then
    curl -sS -o /dev/null -X DELETE -H "Authorization: token $TOKEN" "$API/releases/$RELEASE_ID/assets/$OLD_ID"
    echo "удалён прежний ассет $NAME (id=$OLD_ID)"
fi

CODE="$(curl -sS -o /dev/null -w '%{http_code}' -H "Authorization: token $TOKEN" \
    -F "attachment=@$FILE" "$API/releases/$RELEASE_ID/assets?name=$NAME")"
echo "asset $NAME: HTTP $CODE"
case "$CODE" in 2*) ;; *) echo "::error::не удалось залить $NAME (HTTP $CODE)"; exit 1;; esac
