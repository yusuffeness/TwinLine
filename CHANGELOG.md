# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.4.0] - 2026-11-22

### Added

- Parallel analysis: rows are handed out to worker threads in chunks from an
  atomic cursor; each worker keeps private results that are merged at the
  end, so the comparison loop takes no locks.
- Two exact filters: a size-ratio bound that skips the rest of a row, and an
  early exit from the set intersection once the threshold is out of reach.
  On a 1 MiB corpus the default mode is 8.6x faster than the exhaustive scan
  on one thread and 56x faster with 16 threads (16.5 s to 0.30 s on an
  i7-13620H). All counts and the most similar pair are unchanged.
- `--threads=N` and `--exhaustive` command-line options; `status` shows the
  thread count and the mode.
- The report shows the number of pairs actually compared and the thread
  count.
- `bench/bench_analysis` (median timings, CSV and JSON output),
  `bench/plot.py`, `make bench` and `make bench-large`.
- CI benchmark job that compares each run with the last run on `main`.
- Correctness tests that compare the parallel engine with an independent
  all-pairs implementation for 1-16 threads and several thresholds.
- `docs/ALGORITHMS.md`.

### Changed

- Feature sets are built into one contiguous arena with a reusable builder
  instead of one allocation per line.
- The mean similarity is reported only in exhaustive mode.
- Coverage builds use atomic profile counters, which multithreaded code
  needs for correct coverage data.

### Fixed

- Tests failed to build on macOS because the system headers hide `mkdtemp`
  when `_XOPEN_SOURCE` is defined; `_DARWIN_C_SOURCE` is now set on Apple
  platforms.

## [0.3.0] - 2026-11-21

### Added

- Unit test suite (`tests/unit/`) for every module, built on a small
  header-only harness (`tests/tw_test.h`).
- Golden-file integration tests (`tests/integration/`) that drive the real
  binary through standard input and compare output, exit status and the
  files it writes.
- Fuzz targets for the UTF-8 decoder, the tokenizer and the command parser,
  runnable with libFuzzer or as seed-corpus regression tests with any
  compiler.
- GitHub Actions CI: GCC and Clang on Ubuntu, Clang on macOS, ASan + UBSan,
  TSan, Valgrind, clang-format, clang-tidy, coverage with Codecov upload and
  60-second fuzzing runs.
- CodeQL workflow and Dependabot updates for GitHub Actions.
- `make test` and `make coverage` targets; `make asan` and `make tsan` now
  run the tests too.
- `docs/TESTING.md`.

### Changed

- The command parser is a separate function, `tw_command_parse()`.
- Error messages use the thread-safe `strerror_r`.
- Singular and plural forms in messages ("1 line", "2 lines").
- clang-tidy runs with warnings as errors on a curated set of checks.

### Fixed

- `save` could fail to build with `-Werror` on platforms where `mode_t` is
  16 bits wide (macOS).

## [0.2.0] - 2026-11-21

### Added

- Line-based editor ported from the original course project, rewritten in
  English and split into `core`, `analysis` and `cmd` modules behind a static
  library (`twinline_core`).
- Near-duplicate analysis: word and bigram feature sets compared with the
  Jaccard similarity, in the foreground (`analyze-sync`) or on a worker thread
  (`analyze`).
- Unicode-aware tokenizer: UTF-8 decoding with validation, case folding for
  Latin-1, Latin Extended-A, Greek and Cyrillic, and separation on Unicode
  punctuation, symbols and emoji.
- Turkish case folding mode (`--fold=turkish`).
- `cancel` command and cooperative cancellation of the background analysis.
- `status` command with live analysis progress.
- `open FILE` on the command line, `save` without a file name.
- Unsaved-changes protection: `quit` and `open` ask for confirmation, `quit!`
  and `open!` force the action.
- `scripts/gen_corpus.py` and `make corpus` to generate English test corpora
  with planted exact, near and case-variant duplicates.

### Changed

- Feature hashes are now 64-bit (FNV-1a with a SplitMix64 bigram mixer),
  making hash collisions negligible.
- `save` is atomic: it writes a temporary file, flushes it to disk and renames
  it over the target, keeping the original permissions.
- The background analysis takes its snapshot before the worker starts, which
  removes the condition variable handshake of the original design.
- Quitting during an analysis cancels it instead of waiting for it to finish.
- A leading UTF-8 byte order mark is ignored when loading a file.

## [0.1.0] - 2026-11-21

### Added

- Project skeleton: source, test, benchmark and documentation directories.
- CMake build system (C17) with strict warnings and optional AddressSanitizer,
  UndefinedBehaviorSanitizer and ThreadSanitizer builds.
- `Makefile` wrapper with `build`, `debug`, `asan`, `tsan`, `format` and `tidy`
  targets.
- Minimal `twinline` executable with `--help` and `--version`.
- Generated version header driven by the CMake project version.
- Repository configuration: `.gitignore`, `.gitattributes`, `.editorconfig`,
  `.clang-format` and `.clang-tidy`.
- MIT license, README and roadmap.

[Unreleased]: https://github.com/yusuffeness/TwinLine/compare/v0.4.0...HEAD
[0.4.0]: https://github.com/yusuffeness/TwinLine/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/yusuffeness/TwinLine/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/yusuffeness/TwinLine/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/yusuffeness/TwinLine/releases/tag/v0.1.0
