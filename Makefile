# =========================
# Project configuration
# =========================

PROJECT_NAME := SmartClip
CMAKE        := cmake
BUILD_TYPE   ?= Release

# Можно переопределить при вызове:
#   make BUILD_TYPE=Debug
#   make QT_PATH=/path/to/Qt/6.x.x
#   make PREFIX=/usr/local          (Linux install prefix)
#   make PREFIX=$(HOME)/.local     (user install, no sudo)

# =========================
# Platform detection & build directory
# =========================

UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
    PLATFORM := macos
    BUILD_DIR := build-macos
endif
ifeq ($(UNAME_S),Linux)
    PLATFORM := linux
    BUILD_DIR := build-linux
endif
ifeq ($(OS),Windows_NT)
    PLATFORM := windows
    BUILD_DIR := build-windows
endif

# ---- parallel jobs (Linux/macOS) ----
NPROC := $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 1)

# ---- install prefix (Linux only; macOS uses .app bundle) ----
ifeq ($(PLATFORM),linux)
    PREFIX ?= /usr/local
endif

# =========================
# CMake auto-detection (evaluated once at parse time)
# =========================

CMAKE ?= cmake

# Приоритет: PATH → Qt SDK Tools → Homebrew → системные пути
CMAKE := $(shell \
    { command -v cmake 2>/dev/null; \
      for d in /Volumes/HDD/qt /opt/Qt $(HOME)/Qt; do \
        test -x "$$d/Tools/CMake/CMake.app/Contents/bin/cmake" && echo "$$d/Tools/CMake/CMake.app/Contents/bin/cmake" && break; \
      done; \
      for d in /usr/local/bin /opt/homebrew/bin /usr/bin; do \
        test -x "$$d/cmake" && echo "$$d/cmake" && break; \
      done; \
    } | head -1)

# =========================
# Qt6 auto-detection
# =========================

# Приоритет: QT_PATH (явный) → qmake6 → qtpaths6 → поиск Qt6Config.cmake в
# нестандартных локациях (Qt Installer) → pkg-config → поиск по ФС
ifeq ($(QT_PATH),)
    # 1. qmake6 -query QT_INSTALL_PREFIX
    QMAKE6 := $(shell which qmake6 2>/dev/null)
    ifneq ($(QMAKE6),)
        QT_PATH := $(shell $(QMAKE6) -query QT_INSTALL_PREFIX 2>/dev/null)
    endif

    # 2. qtpaths6 (Qt6.2+) — более надёжный способ
    ifeq ($(QT_PATH),)
        QTPATHS6 := $(shell which qtpaths6 2>/dev/null)
        ifneq ($(QTPATHS6),)
            QT_PATH := $(shell $(QTPATHS6) --install-prefix 2>/dev/null)
        endif
    endif

    # 3. Поиск Qt6Config.cmake в нестандартных локациях (Qt Installer)
    #    Ищем в /Volumes/HDD/qt, /opt/Qt, $(HOME)/Qt.
    #    Исключаем android/ios/wasm/qnx — это кросс-билды.
    #    Приоритет: сначала платформно-специфичные поддиректории.
    ifeq ($(QT_PATH),)
        # Qt6Config.cmake лежит в: $PREFIX/lib/cmake/Qt6/Qt6Config.cmake
        # Нужно 4 dirname чтобы подняться до $PREFIX
        ifeq ($(UNAME_S),Darwin)
            # macOS: ищем */macos/lib/cmake/Qt6/Qt6Config.cmake
            QT6_BASE := $(shell find /Volumes/HDD/qt /opt/Qt $(HOME)/Qt \
                -path '*/macos/lib/cmake/Qt6/Qt6Config.cmake' \
                -not -path '*/Examples/*' \
                2>/dev/null | head -1)
            ifeq ($(QT6_BASE),)
                # fallback: любой вариант кроме android/ios/wasm/qnx
                QT6_BASE := $(shell find /Volumes/HDD/qt /opt/Qt $(HOME)/Qt \
                    -name Qt6Config.cmake -path '*/cmake/*' \
                    -not -path '*/android*' -not -path '*/ios*' \
                    -not -path '*/wasm*' -not -path '*/qnx*' \
                    -not -path '*/Examples/*' \
                    2>/dev/null | head -1)
            endif
        else
            # Linux: ищем */gcc_64/lib/cmake/Qt6/Qt6Config.cmake
            QT6_BASE := $(shell find /opt/Qt $(HOME)/Qt \
                -path '*/gcc_64/lib/cmake/Qt6/Qt6Config.cmake' \
                -not -path '*/Examples/*' \
                2>/dev/null | head -1)
            ifeq ($(QT6_BASE),)
                QT6_BASE := $(shell find /opt/Qt $(HOME)/Qt \
                    -name Qt6Config.cmake -path '*/cmake/*' \
                    -not -path '*/android*' -not -path '*/ios*' \
                    -not -path '*/wasm*' -not -path '*/qnx*' \
                    -not -path '*/Examples/*' \
                    2>/dev/null | head -1)
            endif
        endif
        ifneq ($(QT6_BASE),)
            # $PREFIX/lib/cmake/Qt6/Qt6Config.cmake → 4 dirname → $PREFIX
            QT_PATH := $(shell echo "$(QT6_BASE)" | xargs dirname | xargs dirname | xargs dirname | xargs dirname)
        endif
    endif

    # 4. pkg-config
    ifeq ($(QT_PATH),)
        QT_PATH := $(shell pkg-config --variable=prefix Qt6Core 2>/dev/null)
    endif

    # 5. Стандартные локации: Homebrew (macOS) / системные (Linux)
    ifeq ($(QT_PATH),)
        ifeq ($(UNAME_S),Darwin)
            QT_PATH := $(shell ls -d /usr/local/Cellar/qt@6/*/lib/cmake/Qt6 2>/dev/null | head -1 | xargs dirname | xargs dirname | xargs dirname)
            ifeq ($(QT_PATH),)
                QT_PATH := $(shell ls -d /opt/homebrew/Cellar/qt@6/*/lib/cmake/Qt6 2>/dev/null | head -1 | xargs dirname | xargs dirname | xargs dirname)
            endif
        else ifeq ($(UNAME_S),Linux)
            QT6_CMAKE := $(shell find /usr/lib /usr/share -name Qt6Config.cmake -path '*/cmake/*' 2>/dev/null | head -1)
            ifneq ($(QT6_CMAKE),)
                QT_PATH := $(shell echo "$(QT6_CMAKE)" | xargs dirname | xargs dirname | xargs dirname)
            endif
        endif
    endif
endif

# =========================
# CMake arguments
# =========================

CMAKE_ARGS := -S . -B $(BUILD_DIR) \
              -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

ifneq ($(QT_PATH),)
    CMAKE_ARGS += -DCMAKE_PREFIX_PATH=$(QT_PATH)
endif

ifeq ($(PLATFORM),linux)
    CMAKE_ARGS += -DCMAKE_INSTALL_PREFIX=$(PREFIX)
endif

# =========================
# Top-level targets (platform-native)
# =========================

.PHONY: all configure build clean rebuild run install uninstall test test-verbose

all: build

configure:
	@echo "=== Configuring $(PROJECT_NAME) ==="
	@echo "  Platform : $(PLATFORM)"
	@echo "  Build dir: $(BUILD_DIR)"
	@echo "  Type     : $(BUILD_TYPE)"
	@echo "  Parallel : $(NPROC) jobs"
	@echo "  CMake    : $(CMAKE)"
ifneq ($(QT_PATH),)
	@echo "  Qt6      : $(QT_PATH)"
else
	@echo "  Qt6      : not found (set QT_PATH=... if CMake fails)"
endif
ifeq ($(PLATFORM),linux)
	@echo "  Prefix   : $(PREFIX)"
endif
	$(CMAKE) $(CMAKE_ARGS)

build: configure
	@echo "=== Building $(PROJECT_NAME) ==="
	$(CMAKE) --build $(BUILD_DIR) --config $(BUILD_TYPE) --parallel $(NPROC)

clean:
	@echo "=== Cleaning $(BUILD_DIR) ==="
	rm -rf $(BUILD_DIR)

rebuild: clean build

install: build
	@echo "=== Installing $(PROJECT_NAME) ==="
ifeq ($(PLATFORM),linux)
	@if [ -w "$(dir $(PREFIX))" ] && [ -w "$(PREFIX)" ]; then \
	    $(CMAKE) --install $(BUILD_DIR); \
	else \
	    echo "Need root to install to $(PREFIX). Running: sudo $(CMAKE) --install $(BUILD_DIR)"; \
	    sudo $(CMAKE) --install $(BUILD_DIR); \
	fi
else
	$(CMAKE) --install $(BUILD_DIR)
endif

uninstall:
	@echo "=== Uninstalling $(PROJECT_NAME) ==="
ifeq ($(PLATFORM),linux)
	@if [ -f "$(BUILD_DIR)/install_manifest.txt" ]; then \
	    xargs -a $(BUILD_DIR)/install_manifest.txt rm -vf; \
	else \
	    echo "No install_manifest.txt found — run 'make install' first."; \
	fi
	rm -f $(HOME)/.config/autostart/smartclip.desktop
else
	@echo "Uninstall not supported on $(PLATFORM). Remove $(PROJECT_NAME).app manually."
endif

run:
	@echo "=== Running $(PROJECT_NAME) ==="
ifeq ($(PLATFORM),windows)
	$(BUILD_DIR)/$(BUILD_TYPE)/$(PROJECT_NAME).exe
else ifeq ($(PLATFORM),macos)
	open $(BUILD_DIR)/$(PROJECT_NAME).app
else
	$(BUILD_DIR)/$(PROJECT_NAME)
endif

test: build
	@echo "=== Running tests ==="
	$(CMAKE) --build $(BUILD_DIR) --target test
	cd $(BUILD_DIR) && ctest --output-on-failure

test-verbose: build
	@echo "=== Running tests (verbose) ==="
	$(CMAKE) --build $(BUILD_DIR) --target test
	cd $(BUILD_DIR) && ctest --output-on-failure -V

# =========================
# Convenience: linux-specific targets
# =========================

.PHONY: linux linux-build linux-install linux-run linux-test linux-uninstall

linux: linux-build

linux-build:
	@$(MAKE) build BUILD_DIR=build-linux

linux-install:
	@$(MAKE) install BUILD_DIR=build-linux

linux-run:
	@$(MAKE) run BUILD_DIR=build-linux

linux-test:
	@$(MAKE) test BUILD_DIR=build-linux

linux-uninstall:
	@$(MAKE) uninstall BUILD_DIR=build-linux

# =========================
# Convenience: macos-specific targets
# =========================

.PHONY: macos macos-build macos-run macos-test

macos: macos-build

macos-build:
	@$(MAKE) build BUILD_DIR=build-macos

macos-run:
	@$(MAKE) run BUILD_DIR=build-macos

macos-test:
	@$(MAKE) test BUILD_DIR=build-macos
