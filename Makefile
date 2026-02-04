# =========================
# Project configuration
# =========================

PROJECT_NAME := SmartClip
BUILD_DIR    := build
CMAKE        := cmake
BUILD_TYPE   ?= Release

# Можно переопределить при вызове:
# make BUILD_TYPE=Debug
#
# Обязательно указать путь до Qt6:
# make QT_PATH=/path/to/Qt/6.x.x

QT_PATH ?=

# =========================
# Platform detection
# =========================

UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
    PLATFORM := macos
endif
ifeq ($(UNAME_S),Linux)
    PLATFORM := linux
endif
ifeq ($(OS),Windows_NT)
    PLATFORM := windows
endif

# =========================
# CMake arguments
# =========================

CMAKE_ARGS := -S . -B $(BUILD_DIR) \
              -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

ifneq ($(QT_PATH),)
    CMAKE_ARGS += -DCMAKE_PREFIX_PATH=$(QT_PATH)
endif

# =========================
# Targets
# =========================

.PHONY: all configure build clean rebuild run test test-verbose

all: build

configure:
	@echo "Configuring ($(PLATFORM), $(BUILD_TYPE))"
ifeq ($(QT_PATH),)
	@echo "Ошибка: Необходимо указать путь до Qt6"
	@echo "Используйте: make QT_PATH=/path/to/Qt/6.x.x"
	@exit 1
endif
	$(CMAKE) $(CMAKE_ARGS)

build: configure
	@echo "Building $(PROJECT_NAME)"
	$(CMAKE) --build $(BUILD_DIR) --config $(BUILD_TYPE)

clean:
	@echo "Cleaning build directory"
	rm -rf $(BUILD_DIR)

rebuild: clean build

run:
	@echo "Running $(PROJECT_NAME)"
ifeq ($(PLATFORM),windows)
	$(BUILD_DIR)/$(BUILD_TYPE)/$(PROJECT_NAME).exe
else ifeq ($(PLATFORM),macos)
	open $(BUILD_DIR)/$(PROJECT_NAME).app
else
	$(BUILD_DIR)/$(PROJECT_NAME)
endif

test: build
	@echo "Running tests"
	$(CMAKE) --build $(BUILD_DIR) --target test
	cd $(BUILD_DIR) && ctest --output-on-failure

test-verbose: build
	@echo "Running tests with verbose output"
	$(CMAKE) --build $(BUILD_DIR) --target test
	cd $(BUILD_DIR) && ./tests/SettingsDialogTest -v2
