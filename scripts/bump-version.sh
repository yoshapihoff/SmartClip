#!/usr/bin/env bash
# Бамп версии SmartClip в файле VERSION.
#
# Схема: MAJOR.MINOR.PATCH. Обычный коммит инкрементит МЛАДШЕЕ число (PATCH,
# третья цифра). MINOR (средняя) и MAJOR (старшая) меняются только по явному
# указанию — вручную или через SMARTCLIP_BUMP в pre-commit.
#
# Использование:
#   scripts/bump-version.sh                # patch: 1.0.0 -> 1.0.1
#   scripts/bump-version.sh patch          # то же
#   scripts/bump-version.sh minor          # 1.0.0 -> 1.1.0
#   scripts/bump-version.sh major          # 1.0.0 -> 2.0.0
#   scripts/bump-version.sh --set 1.2.3    # выставить точное значение
#   scripts/bump-version.sh -q [part]      # тихий режим (для git-хука)
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

VERSION_FILE="$PROJECT_DIR/VERSION"
[ -f "$VERSION_FILE" ] || die "не найден файл версии: $VERSION_FILE"

PART="patch"
QUIET=0
SET_VER=""

while [ $# -gt 0 ]; do
    case "$1" in
        patch|minor|major) PART="$1" ;;
        --set) shift; SET_VER="${1:?--set требует значение X.Y.Z}" ;;
        --quiet|-q) QUIET=1 ;;
        -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
        *) die "неизвестный аргумент: $1" ;;
    esac
    shift
done

cur="$(tr -d '[:space:]' < "$VERSION_FILE")"

if [ -n "$SET_VER" ]; then
    new="$SET_VER"
else
    IFS=. read -r v_major v_minor v_patch <<<"$cur"
    if [[ ! "$v_major" =~ ^[0-9]+$ || ! "$v_minor" =~ ^[0-9]+$ || ! "$v_patch" =~ ^[0-9]+$ ]]; then
        die "текущая версия не в форме MAJOR.MINOR.PATCH: '$cur'"
    fi
    case "$PART" in
        major) v_major=$((v_major + 1)); v_minor=0; v_patch=0 ;;
        minor) v_minor=$((v_minor + 1)); v_patch=0 ;;
        patch) v_patch=$((v_patch + 1)) ;;
    esac
    new="$v_major.$v_minor.$v_patch"
fi

if [[ ! "$new" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    die "некорректная версия: '$new'"
fi

if [ "$new" = "$cur" ]; then
    [ "$QUIET" -eq 0 ] && echo "Версия без изменений: $cur"
    exit 0
fi

printf '%s\n' "$new" > "$VERSION_FILE"
[ "$QUIET" -eq 0 ] && log "SmartClip: версия $cur -> $new ($PART)"
exit 0
