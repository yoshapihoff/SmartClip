#!/usr/bin/env bash
# Сборка SmartClip.app под macOS + подсказка по зависимостям для запуска.
#
# Использование:
#   scripts/build-macos.sh [--bundle] [--dist] [--debug] [--jobs N]
#     --bundle   упаковать Qt/OpenSSL внутрь .app через macdeployqt
#                (чтобы приложение запускалось без установленного Qt)
#     --dist     скопировать готовый .app в dist/ (артефакт для раздачи)
#     --debug    сборка Debug (по умолчанию Release)
#     --jobs N   параллелизм (по умолчанию: все ядра)
#
# Зависимости (установить один раз):
#   brew install qt@6 cmake openssl@3
# (qt6-mqtt для синхронизации — через Qt Online Installer; см. docs/BUILD-MACOS.md)
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

BUILD_DIR="build-macos"
BUILD_TYPE="Release"
DO_BUNDLE=0
DO_DIST=0
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

while [ $# -gt 0 ]; do
    case "$1" in
        --bundle) DO_BUNDLE=1 ;;
        --dist) DO_DIST=1 ;;
        --debug) BUILD_TYPE="Debug" ;;
        --jobs) shift; JOBS="${1:?--jobs требует число}" ;;
        -h|--help) sed -n '2,18p' "$0"; exit 0 ;;
        *) die "неизвестный аргумент: $1" ;;
    esac
    shift
done

[ "$(uname -s)" = "Darwin" ] || die "Этот скрипт — для macOS. На Linux используй scripts/build-linux.sh / build-appimage.sh."
command -v cmake >/dev/null || die "cmake не найден: brew install cmake"

cd "$PROJECT_DIR"

# ── Поиск Qt6 (с ВАЛИДАЦИЕЙ: в префиксе должен быть lib/cmake/Qt6) ───────
# Просто `qmake6 -query` недостаточно: он может указать на префикс без
# CMake-конфига (напр. /usr/local/opt/qt без библиотеки cmake/Qt6), и CMake
# падает с «Could not find Qt6Config.cmake». Перебираем варианты и берём
# первый, где реально лежит Qt6Config.cmake.
qt6_config_dir() {
    local p="$1" c
    [ -n "$p" ] || return 1
    # pwd -P разрешает симлинки (brew /usr/local/opt/qt → /usr/local/Cellar/qt/…),
    # и работает и на macOS (BSD), в отличие от `readlink -f`.
    c="$(cd "$p" 2>/dev/null && pwd -P)" || return 1
    local f
    for f in "$c/lib/cmake/Qt6/Qt6Config.cmake" "$c/lib64/cmake/Qt6/Qt6Config.cmake"; do
        [ -f "$f" ] && { dirname "$f"; return 0; }
    done
    return 1
}

QT_PREFIX=""
QT6_DIR=""
try_qt() {
    local p="$1" cfg
    [ -n "$p" ] || return 1
    cfg="$(qt6_config_dir "$p")" && { QT_PREFIX="$p"; QT6_DIR="$cfg"; return 0; }
    return 1
}

QT_CANDS=()
[ -n "${QT_PATH:-}" ] && QT_CANDS+=("$QT_PATH")
# Повторная сборка: берём Qt из прошлой конфигурации (там может быть Qt6::Mqtt
# из Online Installer, которого нет в brew).
if [ -f "$BUILD_DIR/CMakeCache.txt" ]; then
    cached="$(sed -n 's/^Qt6_DIR:PATH=//p' "$BUILD_DIR/CMakeCache.txt" | head -1)"
    # Qt6_DIR = <prefix>/lib/cmake/Qt6 → поднимаемся на 3 уровня до prefix
    if [ -n "$cached" ] && [ -f "$cached/Qt6Config.cmake" ]; then
        QT_CANDS+=("$(cd "$(dirname "$(dirname "$(dirname "$cached")")")" && pwd)")
    fi
fi
# Приоритет — brew (по докам ставим qt@6), ЗАТЕМ qmake6: на macOS в PATH может
# оказаться чужой qmake6 (напр. из conda) и увести на префикс без cmake/Qt6.
if command -v brew >/dev/null 2>&1; then
    QT_CANDS+=("$(brew --prefix qt@6 2>/dev/null)" "$(brew --prefix qt 2>/dev/null)" \
               "$(brew --prefix 2>/dev/null)/opt/qt" "$(brew --prefix 2>/dev/null)/opt/qt@6")
fi
command -v qmake6   >/dev/null 2>&1 && QT_CANDS+=("$(qmake6 -query QT_INSTALL_PREFIX 2>/dev/null)")
command -v qtpaths6 >/dev/null 2>&1 && QT_CANDS+=("$(qtpaths6 --install-prefix 2>/dev/null)")
QT_CANDS+=(/usr/local/opt/qt@6 /opt/homebrew/opt/qt@6 /usr/local/opt/qt /opt/homebrew/opt/qt)
# Qt Online Installer (типовые локации; у Лёши — /Volumes/HDD/qt)
for d in /Volumes/HDD/qt /opt/Qt "$HOME/Qt"; do
    [ -d "$d" ] || continue
    hit="$(find "$d" -maxdepth 4 -path '*/macos/lib/cmake/Qt6/Qt6Config.cmake' -not -path '*/Examples/*' 2>/dev/null | head -1)"
    [ -z "$hit" ] && hit="$(find "$d" -maxdepth 6 -name Qt6Config.cmake -path '*/cmake/*' \
        -not -path '*/android*' -not -path '*/ios*' -not -path '*/wasm*' -not -path '*/qnx*' -not -path '*/Examples/*' 2>/dev/null | head -1)"
    [ -n "$hit" ] && QT_CANDS+=("$(cd "$(dirname "$(dirname "$(dirname "$(dirname "$hit")")")")" 2>/dev/null && pwd)")
done

for c in ${QT_CANDS[@]+"${QT_CANDS[@]}"}; do try_qt "$c" && break; done
if [ -z "$QT6_DIR" ]; then
    warn "Qt6 с lib/cmake/Qt6 не найден. Проверены префиксы:"
    for c in ${QT_CANDS[@]+"${QT_CANDS[@]}"}; do [ -n "$c" ] && warn "  - $c"; done
    die "Установи 'brew install qt@6' или задай QT_PATH=/путь/к/Qt/6.x (напр. \$HOME/Qt/6.11.0/macos).\n     Найти реальный префикс: find /usr/local /opt/homebrew \\$HOME/Qt -name Qt6Config.cmake 2>/dev/null"
fi

# ── Поиск OpenSSL (обязателен: AES-256-GCM для шифрования истории) ────────
OPENSSL_PREFIX=""
for c in "$(brew --prefix openssl@3 2>/dev/null)" "$(brew --prefix openssl 2>/dev/null)" \
         /usr/local/opt/openssl@3 /opt/homebrew/opt/openssl@3 /usr/local/opt/openssl /opt/homebrew/opt/openssl; do
    [ -n "$c" ] && { [ -f "$c/lib/libcrypto.dylib" ] || [ -f "$c/lib/libcrypto.a" ]; } && { OPENSSL_PREFIX="$c"; break; }
done
[ -n "$OPENSSL_PREFIX" ] || die "OpenSSL не найден (обязателен для шифрования). Установи: brew install openssl@3"

log "Qt6:     $QT_PREFIX"
log "Qt6_DIR: $QT6_DIR"
log "OpenSSL: $OPENSSL_PREFIX"

log "Конфигурация ($BUILD_TYPE)…"
cmake -S . -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_PREFIX_PATH="$QT_PREFIX;$OPENSSL_PREFIX" \
    -DQt6_DIR="$QT6_DIR" \
    -DOPENSSL_ROOT_DIR="$OPENSSL_PREFIX"

log "Сборка ($JOBS потоков)…"
# Для релиза собираем только цель приложения (без тестов): тесты при упаковке
# не нужны, а их сборочные сюрпризы не должны ломать релизный артефакт.
cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --target "$APP_NAME" --parallel "$JOBS"

APP="$BUILD_DIR/$APP_NAME.app"
[ -d "$APP" ] || die "не найден бандл $APP"

if [ "$DO_BUNDLE" -eq 1 ]; then
    # macOS-приложение: без Qt внутри бандла запускается только там, где есть
    # Qt в PATH. macdeployqt НЕ всегда чинит rpath → после него САНИРУЕМ.
    MACDEPLOYQT="$QT_PREFIX/bin/macdeployqt"
    [ -x "$MACDEPLOYQT" ] || die "macdeployqt не найден в $QT_PREFIX/bin (нужен для --bundle)"
    log "Упаковка Qt/OpenSSL внутрь .app (macdeployqt)…"
    # Homebrew делит Qt на отдельные keg'и (qtbase, qtsvg, qtpdf, …), и
    # macdeployqt не находит по @rpath фреймворки вида QtPdf/QtVirtualKeyboardQml,
    # которые тянут плагины. Отдаём ему пути всех keg'ов как -libpath.
    MACDEPLOYQT_ARGS=()
    if command -v brew >/dev/null 2>&1; then
        for d in "$(brew --prefix)"/opt/*/lib "$(brew --prefix)"/lib; do
            [ -d "$d" ] && MACDEPLOYQT_ARGS+=("-libpath=$d")
        done
    fi
    [ -n "$QT_PREFIX" ] && MACDEPLOYQT_ARGS+=("-libpath=$QT_PREFIX/lib")
    "$MACDEPLOYQT" "$APP" -always-overwrite \
        -executable="$APP/Contents/MacOS/$APP_NAME" \
        ${MACDEPLOYQT_ARGS[@]+"${MACDEPLOYQT_ARGS[@]}"}

    # ── Санитария rpath ───────────────────────────────────────────────
    # Главный бинарник мог получить rpath на ИСХОДНЫЙ Qt — тогда при запуске
    # грузятся ДВЕ копии QtCore и плагин cocoa падает. Прибиваем внешние пути,
    # гарантируем @executable_path/../Frameworks.
    EXE="$APP/Contents/MacOS/$APP_NAME"
    INT="$(command -v install_name_tool || echo /usr/bin/install_name_tool)"
    [ -x "$INT" ] || die "install_name_tool не найден (нужен Xcode CLT)"
    log "Санитария rpath (убираю ссылки на исходный Qt)…"
    # У главного бинарника должен остаться ТОЛЬКО бандловый rpath. Удаляем
    # все прочие (в т.ч. путь к исходному Qt, в т.ч. через симлинки).
    while IFS= read -r rp; do
        if [ "$rp" != "@executable_path/../Frameworks" ]; then
            "$INT" -delete_rpath "$rp" "$EXE" 2>/dev/null \
                && warn "удалён внешний rpath: $rp" || true
        fi
    done < <(otool -l "$EXE" | awk '/LC_RPATH/{f=1} f&&/path /{print $2; f=0}')
    # Гарантируем встроенный rpath
    otool -l "$EXE" | grep -q '@executable_path/../Frameworks' \
        || "$INT" -add_rpath "@executable_path/../Frameworks" "$EXE"
    # ── Вложить OpenSSL (libcrypto/libssl) ──────────────────────────────
    # macdeployqt копирует ТОЛЬКО Qt. OpenSSL приложение использует напрямую
    # (шифрование истории), поэтому без него .app на чужой машине не стартует.
    # Копируем dylib(ы) в бандл и переписываем ссылки на @rpath.
    FW="$APP/Contents/Frameworks"
    mkdir -p "$FW"
    bundle_dylib() {
        local src="$1" base dest m
        [ -f "$src" ] || return 0
        base="$(basename "$src")"
        dest="$FW/$base"
        if [ ! -f "$dest" ]; then
            cp -f "$src" "$dest"; chmod u+w "$dest"
        fi
        "$INT" -id "@rpath/$base" "$dest" 2>/dev/null || true
        while IFS= read -r m; do
            otool -L "$m" 2>/dev/null | grep -Fq "$src" || continue
            "$INT" -change "$src" "@rpath/$base" "$m" 2>/dev/null || true
        done < <(find "$APP/Contents/MacOS" "$APP/Contents/Frameworks" "$APP/Contents/PlugIns" -type f 2>/dev/null)
    }
    for lib in libcrypto libssl; do
        src="$(ls "$OPENSSL_PREFIX"/lib/$lib.[0-9]*.dylib 2>/dev/null | head -1)"
        [ -n "$src" ] || src="$(ls "$OPENSSL_PREFIX"/lib/$lib.dylib 2>/dev/null | head -1)"
        [ -n "$src" ] && { log "Вкладываю $(basename "$src")…"; bundle_dylib "$src"; }
    done
    # ── Полная санитария install_name: абсолютные пути ВНУТРИ бандла → @rpath ──
    # macdeployqt (особенно с Qt из Online Installer) нередко прописывает
    # фреймворкам и плагинам зависимости/install-name АБСОЛЮТНЫМ путём внутрь
    # самого .app (…/SmartClip.app/Contents/Frameworks/…). На машине без этого
    # пути запуск падает (dyld не находит библиотеку). Переписываем ВСЕ такие
    # ссылки на @rpath и гарантируем rpath на бандловые Frameworks у каждого
    # Mach-O файла в бандле.
    BUNDLE_MARK="/$(basename "$APP")/Contents/Frameworks/"
    log "Санитария install_name (абсолютные пути бандла → @rpath)…"
    find "$APP/Contents" -type f -print0 2>/dev/null | while IFS= read -r -d '' m; do
        otool -L "$m" >/dev/null 2>&1 || continue   # только Mach-O
        chmod u+w "$m" 2>/dev/null || true
        # 1) install-name (LC_ID_DYLIB) самого файла. Берём первую строку,
        # начинающуюся с '/', без заголовка «файл:» — так надёжнее, чем
        # предполагать наличие строки-заголовка.
        idn="$(otool -D "$m" 2>/dev/null | grep -E '^/' | grep -v ':$' | head -1)"
        case "$idn" in
            *"$BUNDLE_MARK"*)
                "$INT" -id "@rpath/${idn#*$BUNDLE_MARK}" "$m" 2>/dev/null || true ;;
        esac
        # 2) зависимости (LC_LOAD_DYLIB)
        while IFS= read -r dep; do
            case "$dep" in
                *"$BUNDLE_MARK"*)
                    "$INT" -change "$dep" "@rpath/${dep#*$BUNDLE_MARK}" "$m" 2>/dev/null || true ;;
            esac
        done < <(otool -L "$m" 2>/dev/null | tail -n +2 | awk '{print $1}')
        # 3) rpath на бандловые Frameworks (нужен всем: exe, плагинам, фреймворкам)
        otool -l "$m" 2>/dev/null | grep -q '@executable_path/../Frameworks' || \
            "$INT" -add_rpath "@executable_path/../Frameworks" "$m" 2>/dev/null || true
    done

    # Переподписываем ad-hoc (arm64 иначе откажется запускать изменённый
    # бинарник; install_name_tool ломает подпись).
    if command -v codesign >/dev/null 2>&1; then
        codesign --force --deep --sign - "$APP" 2>/dev/null \
            || codesign --force --sign - "$EXE" 2>/dev/null || true
    fi
fi

# ── Проверка самодостаточности ──────────────────────────────────────────
if [ "$DO_BUNDLE" -eq 1 ]; then
    log "Проверка бандла (external refs)…"
    bad=0
    for f in "$APP/Contents/MacOS/$APP_NAME" "$APP"/Contents/Frameworks/*.dylib "$APP"/Contents/PlugIns/platforms/*.dylib; do
        [ -f "$f" ] || continue
        # Внешняя зависимость = абсолютный путь ВНЕ самого бандла и вне
        # /usr/lib, /System (Homebrew/opt, /usr/local, /Volumes/Qt/… — должны
        # быть вложены). Ссылки на файлы ВНУТРИ .app допустимы (мы их перевели
        # в @rpath, но на всякий случай не считаем ошибкой).
        ext="$(otool -L "$f" | tail -n +2 | awk '{print $1}' \
               | grep -E '^/' | grep -vE '^/usr/lib/|^/System/' \
               | grep -vF "$APP/" || true)"
        if [ -n "$ext" ]; then
            warn "$(basename "$f") линкует внешние пути:"
            echo "$ext" | sed 's/^/    /' >&2
            bad=1
        fi
    done
    if [ "$bad" -eq 0 ]; then
        log "OK: бандл самодостаточен (внешних ссылок нет)."
    else
        warn "Бандл НЕ самодостаточен — на машине без этих библиотек запуск упадёт."
    fi
fi

echo
log "Готово: $APP"
if [ "$DO_DIST" -eq 1 ]; then
    ensure_dist
    rm -rf "$DIST_DIR/$APP_NAME.app"
    cp -R "$APP" "$DIST_DIR/$APP_NAME.app"
    log "Артефакт: $DIST_DIR/$APP_NAME.app"
fi
echo "Запуск:  open \"$APP\"   (или: $APP/Contents/MacOS/$APP_NAME)"
cat <<'DEPS'

────────────────────────────────────────────────────────────────────────
Зависимости для ЗАПУСКА готового приложения:
  • macOS 12+ (шрифты/тема — системные).
  • Qt6 (Core/Gui/Widgets/Svg) — либо системный, либо внутри .app (--bundle).
  • OpenSSL 3 (libcrypto) — нужен для шифрования истории.
  • Qt6 MQTT — ТОЛЬКО если пользуешься сетевой синхронизацией.
  • Keychain — встроен в macOS (хранение ключа шифрования).
Если собирал без --bundle, на машине без Qt запусти:
  brew install qt@6 openssl@3
────────────────────────────────────────────────────────────────────────
DEPS
