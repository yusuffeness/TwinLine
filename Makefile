# Thin convenience wrapper around CMake.
# Every target here can also be run with plain cmake commands (see README.md).

BUILD_DIR   ?= build
BUILD_TYPE  ?= Release
CMAKE       ?= cmake
JOBS        ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
SOURCES     := $(shell find src include tests bench -name '*.[ch]' 2>/dev/null)

.DEFAULT_GOAL := build

.PHONY: help build debug release asan tsan run install clean format format-check tidy

help: ## Show this help
	@grep -E '^[a-zA-Z_-]+:.*?## ' $(MAKEFILE_LIST) | \
		awk 'BEGIN {FS = ":.*?## "} {printf "  \033[35m%-14s\033[0m %s\n", $$1, $$2}'

build: ## Configure and build (default: Release)
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)
	$(CMAKE) --build $(BUILD_DIR) -j $(JOBS)

debug: ## Debug build in build-debug/
	$(MAKE) build BUILD_DIR=build-debug BUILD_TYPE=Debug

release: ## Optimised build in build/
	$(MAKE) build BUILD_TYPE=Release

asan: ## Debug build with AddressSanitizer + UBSan in build-asan/
	$(CMAKE) -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
		-DTWINLINE_ENABLE_ASAN=ON -DTWINLINE_ENABLE_UBSAN=ON
	$(CMAKE) --build build-asan -j $(JOBS)

tsan: ## Debug build with ThreadSanitizer in build-tsan/
	$(CMAKE) -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DTWINLINE_ENABLE_TSAN=ON
	$(CMAKE) --build build-tsan -j $(JOBS)

run: build ## Build and run twinline
	./$(BUILD_DIR)/twinline

install: build ## Install into the CMake prefix (default /usr/local)
	$(CMAKE) --install $(BUILD_DIR)

clean: ## Remove all build directories
	rm -rf build build-debug build-asan build-tsan

format: ## Format all C sources in place with clang-format
	clang-format -i $(SOURCES)

format-check: ## Fail if any C source is not formatted
	clang-format --dry-run --Werror $(SOURCES)

tidy: build ## Run clang-tidy using the compile database
	clang-tidy -p $(BUILD_DIR) $(filter %.c,$(SOURCES))
