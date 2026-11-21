<div align="center">

# TwinLine

**A concurrent terminal editor that finds the lines that look alike.**

![Status](https://img.shields.io/badge/status-work%20in%20progress-orange)
![Version](https://img.shields.io/badge/version-0.1.0-blueviolet)
![Language](https://img.shields.io/badge/language-C17-blue)
![License](https://img.shields.io/badge/license-MIT-green)

</div>

---

> [!NOTE]
> TwinLine is under active development. This release (v0.1.0) sets up the
> project skeleton and build system. Follow the [roadmap](docs/ROADMAP.md)
> to see what is coming next.

## What is TwinLine?

TwinLine is a line-based text editor for the terminal with a built-in
**near-duplicate detector**. It scans a document for lines that are identical
or almost identical, groups them and helps you clean them up, all while you
keep typing.

It started as a university assignment on POSIX threads and is being rebuilt
from the ground up as a polished, open-source tool.

## Planned features

- **Colourful, animated terminal UI** inspired by modern CLI tools: bordered
  boxes, a slash-command menu with fuzzy search, a live spinner and progress bar.
- **Background analysis** that never blocks the editor: analysis runs on a
  snapshot of the document while you continue to edit.
- **Parallel engine**: all-pairs Jaccard similarity split across worker
  threads, plus MinHash + LSH for very large files.
- **Unicode-aware** tokenisation, including Turkish case folding.
- **Side-by-side diff view** of near-duplicate lines, clustering and
  interactive de-duplication.
- **Scriptable mode** with JSON output and meaningful exit codes.
- Autosave with crash recovery, file watching, themes and a TOML config file.

## Building from source

Requirements: a C17 compiler (GCC or Clang), CMake 3.16+ and a POSIX system
(Linux, macOS, or WSL on Windows).

```sh
git clone https://github.com/yusuffeness/TwinLine.git
cd TwinLine
make            # or: cmake -S . -B build && cmake --build build
./build/twinline --help
```

Useful targets:

| Command        | What it does                                      |
| -------------- | ------------------------------------------------- |
| `make`         | Release build in `build/`                         |
| `make debug`   | Debug build in `build-debug/`                     |
| `make asan`    | Build with AddressSanitizer and UBSan             |
| `make tsan`    | Build with ThreadSanitizer                        |
| `make format`  | Format the sources with clang-format              |
| `make help`    | List every target                                 |

## Project layout

```
include/twinline/   Public headers
src/                Sources: core, analysis, tui, cmd, services, export
tests/              Unit, integration and fuzz tests
bench/              Benchmarks
scripts/            Helper scripts (test corpus generator, demo recording)
docs/               Roadmap, architecture notes and design decisions
```

## Roadmap

TwinLine is built in ten milestones, from this skeleton (v0.1.0) to a
packaged v1.0.0 release. See [docs/ROADMAP.md](docs/ROADMAP.md).

## License

TwinLine is released under the [MIT License](LICENSE).
