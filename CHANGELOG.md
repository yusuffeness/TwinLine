# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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

[Unreleased]: https://github.com/yusuffeness/TwinLine/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/yusuffeness/TwinLine/releases/tag/v0.1.0
