<div align="center">

# TwinLine

**A concurrent terminal editor that finds the lines that look alike.**

[![CI](https://github.com/yusuffeness/TwinLine/actions/workflows/ci.yml/badge.svg)](https://github.com/yusuffeness/TwinLine/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/yusuffeness/TwinLine/graph/badge.svg)](https://codecov.io/gh/yusuffeness/TwinLine)
![Status](https://img.shields.io/badge/status-work%20in%20progress-orange)
![Version](https://img.shields.io/badge/version-0.4.0-blueviolet)
![Language](https://img.shields.io/badge/language-C17-blue)
![License](https://img.shields.io/badge/license-MIT-green)

</div>

---

> [!NOTE]
> TwinLine is under active development. v0.4.0 ships a line-based editor
> with a parallel, exact near-duplicate analysis; the colourful terminal UI
> arrives in v0.5.0. Follow the [roadmap](docs/ROADMAP.md) to see what is next.

## What is TwinLine?

TwinLine is a line-based text editor for the terminal with a built-in
**near-duplicate detector**. It scans a document for lines that are identical
or almost identical, groups them and helps you clean them up, all while you
keep typing.

It started as a university assignment on POSIX threads and is being rebuilt
from the ground up as a polished, open-source tool.

## Quick start

```sh
make                       # build
make corpus                # generate a 1 MiB test corpus in corpus/
./build/twinline corpus/corpus-1MiB.txt
```

```text
TwinLine 0.4.0 - type 'help' for the list of commands.
Opened 'corpus/corpus-1MiB.txt' (16357 lines).
twinline> analyze
Analysing 16357 lines in the background. Use 'status' or 'cancel'.
twinline> append This line was typed while the analysis was running.
Line 16358 added.
twinline> status
File     : corpus/corpus-1MiB.txt
Lines    : 16358
Modified : yes
Folding  : default
Analysis : running, 41% done
```

### Commands

| Command          | Description                                                  |
| ---------------- | ------------------------------------------------------------ |
| `open[!] FILE`   | Load a file (`open!` discards unsaved changes)               |
| `append TEXT`    | Add a line at the end                                        |
| `show`           | Print the document with line numbers                         |
| `save [FILE]`    | Save atomically to `FILE` or to the current file             |
| `analyze`        | Find near-duplicate lines in the background                  |
| `analyze-sync`   | Same, but wait for the result                                |
| `cancel`         | Stop the running analysis                                    |
| `status`         | File, unsaved changes and analysis progress                  |
| `help`           | List the commands                                            |
| `quit[!]`        | Exit; asks again when there are unsaved changes              |

Command-line options:

| Option | Meaning |
| ------ | ------- |
| `--threads=N` | Worker threads for the analysis (default: one per processor) |
| `--exhaustive` | Compare every pair; slower, but also reports the mean similarity |
| `--fold=turkish` | Fold `I` to dotless `ı` as in Turkish (default folds it to `i`) |

### How the analysis works

Each line becomes a set of word and word-pair (bigram) hashes. Words are
split on Unicode-aware boundaries and case-folded, so `ÇAY` matches `çay` and
curly quotes or dashes do not count as letters. Every pair of lines is then
compared with the Jaccard similarity `|A ∩ B| / |A ∪ B|`; pairs at or above
0.70 are reported as near duplicates.

The comparison is split across all processor cores, and two exact filters
skip pairs that provably cannot matter, so the answer is the same as a full
all-pairs scan, only much faster. The analysis runs on a snapshot of the
document, so you can keep editing while it runs. Details:
[docs/ALGORITHMS.md](docs/ALGORITHMS.md).

## Performance

![Benchmark: analysis time and speed-up by number of threads](docs/assets/benchmark.png)

On a 1 MiB corpus (16,357 lines, 134 million line pairs) on an Intel Core
i7-13620H:

| Mode | 1 thread | 16 threads | Speed-up |
| ---- | -------- | ---------- | -------- |
| Exhaustive (the original algorithm) | 16.5 s | 1.72 s | 9.6x |
| Pruned (default) | 1.91 s | **0.30 s** | 6.5x |

From the original single-threaded algorithm to the default mode on all
cores, the same analysis runs **56 times faster** with identical results.
Reproduce the chart on your machine with `make bench`.

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
| `make test`    | Build and run every test                          |
| `make debug`   | Debug build in `build-debug/`                     |
| `make asan`    | Run the tests with AddressSanitizer and UBSan     |
| `make tsan`    | Run the tests with ThreadSanitizer                |
| `make coverage`| Coverage report with gcovr                        |
| `make bench`   | Benchmark and redraw `docs/assets/benchmark.png`  |
| `make corpus`  | Generate a test corpus (`CORPUS_SIZE=10MiB`)      |
| `make format`  | Format the sources with clang-format              |
| `make help`    | List every target                                 |

## Testing

Every change is checked by unit tests, golden-file integration tests and
fuzz targets, and by GitHub Actions on Linux and macOS with GCC and Clang,
AddressSanitizer, UndefinedBehaviorSanitizer, ThreadSanitizer, Valgrind,
clang-tidy and coverage reporting. See [docs/TESTING.md](docs/TESTING.md).

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

TwinLine is built in ten milestones, from the project skeleton (v0.1.0) to a
packaged v1.0.0 release. See [docs/ROADMAP.md](docs/ROADMAP.md).

## License

TwinLine is released under the [MIT License](LICENSE).
