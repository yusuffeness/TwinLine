# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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

[Unreleased]: https://github.com/yusuffeness/TwinLine/compare/v0.2.0...HEAD
[0.2.0]: https://github.com/yusuffeness/TwinLine/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/yusuffeness/TwinLine/releases/tag/v0.1.0
