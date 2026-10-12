#!/usr/bin/env bash
# Сборка (и при желании запуск тестов) SmartClip из исходников под Windows.
#
# Запускать в окружении MinGW-w64: «Qt 6.11.2 (MinGW ...)» из меню «Пуск»
# либо MSYS2/Git Bash с добавленными в PATH Qt- и MinGW-каталогами.
#
# Использование:
#   scripts/build-windows.sh [--test] [--debug] [--jobs N]
#                            [--qt DIR] [--openssl DIR] [--generator NAME]
#     --test           после сборки прогнать ctest (offscreen)
#     --debug          сборка Debug (по умолчанию Release)
#     --jobs N         параллелизм (по умолчанию — число ядер)
#     --qt DIR         префикс Qt (по умолчанию $QT_PATH либо C:/Qt/6.11.2/mingw_64)
#     --openssl DIR    корень OpenSSL (по умолчанию $OPENSSL_ROOT_DIR)
#     --generator NAME CMake-генератор (по умолчанию «MinGW Makefiles»)
#
# Зависимости: Qt 6.11.2 (MinGW) с модулем MQTT, OpenSSL 3.x, собранный тем же
# MinGW (см. docs/BUILD-WINDOWS.md), CMake, MinGW-w64.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

BUILD_DIR="build-windows"
BUILD_TYPE="Release"
DO_TEST=0
JOBS="$(nproc 2>/dev/null || echo 4)"
QT_DIR="${QT_PATH:-C:/Qt/6.11.2/mingw_64}"
OPENSSL_DIR="${OPENSSL_ROOT_DIR:-}"
GENERATOR="MinGW Makefiles"

while [ $# -gt 0 ]; do
    case "$1" in
        --test) DO_TEST=1 ;;
        --debug) BUILD_TYPE="Debug" ;;
        --jobs) shift; JOBS="${1:?--jobs требует число}" ;;
        --qt) shift; QT_DIR="${1:?--qt требует путь}" ;;
        --openssl) shift; OPENSSL_DIR="${1:?--openssl требует путь}" ;;
        --generator) shift; GENERATOR="${1:?--generator требует имя}" ;;
        -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
        *) die "неизвестный аргумент: $1" ;;
    esac
    shift
done

cd "$PROJECT_DIR"

command -v cmake >/dev/null || die "cmake не найден в PATH (поставь CMake или открой Qt MinGW shell)."
[ -d "$QT_DIR" ] || die "каталог Qt не найден: $QT_DIR (укажи --qt DIR или переменную QT_PATH)."
[ -f "$QT_DIR/bin/windeployqt.exe" ] || [ -f "$QT_DIR/bin/qmake6.exe" ] || \
    warn "в $QT_DIR/bin нет windeployqt/qmake — проверь, что путь ведёт на Qt для MinGW 64-bit."

if [ -z "$OPENSSL_DIR" ]; then
    warn "OPENSSL_ROOT_DIR не задан — пробую найти OpenSSL обычным способом."
    warn "  Если CMake упадёт: собери OpenSSL 3.x Qt-шным MinGW и передай --openssl DIR"
    warn "  (см. docs/BUILD-WINDOWS.md, раздел «OpenSSL»)."
fi

log "Конфигурация ($BUILD_TYPE)…"
log "  Qt      : $QT_DIR"
[ -n "$OPENSSL_DIR" ] && log "  OpenSSL : $OPENSSL_DIR"

CMAKE_ARGS=(
    -S . -B "$BUILD_DIR"
    -G "$GENERATOR"
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    -DCMAKE_PREFIX_PATH="$QT_DIR"
)
if [ -n "$OPENSSL_DIR" ]; then
    CMAKE_ARGS+=( -DOPENSSL_ROOT_DIR="$OPENSSL_DIR" )
fi

cmake "${CMAKE_ARGS[@]}"

log "Сборка ($JOBS потоков)…"
cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --parallel "$JOBS"

BIN="$BUILD_DIR/$APP_NAME.exe"
log "Бинарник: $BIN"

if [ "$DO_TEST" -eq 1 ]; then
    log "Тесты (offscreen)…"
    ( cd "$BUILD_DIR" && QT_QPA_PLATFORM=offscreen ctest --output-on-failure )
fi

echo
log "Готово. Запуск: $BIN"
echo "Портативный набор (DLL): scripts/release-windows.sh"
