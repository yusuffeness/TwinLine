# Thin convenience wrapper around CMake.
# Every target here can also be run with plain cmake commands (see README.md).

BUILD_DIR   ?= build
BUILD_TYPE  ?= Release
CMAKE       ?= cmake
PYTHON      ?= python3
CORPUS_SIZE ?= 1MiB
JOBS        ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
SOURCES     := $(shell find src include tests bench -name '*.[ch]' 2>/dev/null)

.DEFAULT_GOAL := build

.PHONY: help build debug release test asan tsan coverage bench bench-large bench-accuracy package run install corpus clean format format-check tidy

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

test: build ## Build and run every test with CTest
	ctest --test-dir $(BUILD_DIR) --output-on-failure --parallel $(JOBS)

asan: ## AddressSanitizer + UBSan build in build-asan/, then run the tests
	$(CMAKE) -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
		-DTWINLINE_ENABLE_ASAN=ON -DTWINLINE_ENABLE_UBSAN=ON
	$(CMAKE) --build build-asan -j $(JOBS)
	ctest --test-dir build-asan --output-on-failure --parallel $(JOBS)

tsan: ## ThreadSanitizer build in build-tsan/, then run the tests
	$(CMAKE) -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DTWINLINE_ENABLE_TSAN=ON
	$(CMAKE) --build build-tsan -j $(JOBS)
	ctest --test-dir build-tsan --output-on-failure --parallel $(JOBS)

coverage: ## Coverage build, run the tests and print a gcovr summary
	$(CMAKE) -S . -B build-coverage -DCMAKE_BUILD_TYPE=Debug -DTWINLINE_ENABLE_COVERAGE=ON
	$(CMAKE) --build build-coverage -j $(JOBS)
	find build-coverage -name '*.gcda' -delete
	ctest --test-dir build-coverage --output-on-failure --parallel $(JOBS)
	gcovr --root . --object-directory build-coverage --filter src/ --print-summary

bench: release ## Benchmark 1 MiB (both modes, 1..N threads) and draw docs/assets/benchmark.png
	$(MAKE) corpus CORPUS_SIZE=1MiB
	@mkdir -p bench/results
	./$(BUILD_DIR)/bench/bench_analysis --repeat=3 --csv=bench/results/benchmark.csv \
		corpus/corpus-1MiB.txt
	$(PYTHON) bench/plot.py bench/results/benchmark.csv -o docs/assets/benchmark.png

bench-large: release ## Benchmark a 10 MiB corpus (pruned mode only; takes a few minutes)
	$(MAKE) corpus CORPUS_SIZE=10MiB
	@mkdir -p bench/results
	./$(BUILD_DIR)/bench/bench_analysis --repeat=1 --modes=pruned \
		--csv=bench/results/benchmark-10MiB.csv corpus/corpus-10MiB.txt

bench-accuracy: release ## MinHash recall and speed against exact Jaccard (1 MiB, three thresholds)
	$(MAKE) corpus
	@mkdir -p bench/results
	@for t in 0.5 0.7 0.9; do \
		./$(BUILD_DIR)/bench/bench_accuracy --threshold=$$t --csv=bench/results/accuracy-$$t.csv \
			corpus/corpus-1MiB.txt || exit 1; \
	done

run: build ## Build and run twinline
	./$(BUILD_DIR)/twinline

install: build ## Install into the CMake prefix (default /usr/local)
	$(CMAKE) --install $(BUILD_DIR)

package: release ## Build the .deb and .tar.gz packages in build/ (cpack)
	cd $(BUILD_DIR) && cpack -G DEB && cpack -G TGZ

corpus: ## Generate a test corpus in corpus/ (CORPUS_SIZE=1MiB)
	$(PYTHON) scripts/gen_corpus.py --size $(CORPUS_SIZE) --output corpus/corpus-$(CORPUS_SIZE).txt

clean: ## Remove all build directories
	rm -rf build build-debug build-asan build-tsan build-coverage

format: ## Format all C sources in place with clang-format
	clang-format -i $(SOURCES)

format-check: ## Fail if any C source is not formatted
	clang-format --dry-run --Werror $(SOURCES)

tidy: build ## Run clang-tidy on the sources using the compile database
	clang-tidy -p $(BUILD_DIR) --quiet $(wildcard src/*.c src/*/*.c)
