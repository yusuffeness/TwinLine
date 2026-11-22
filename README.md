<div align="center">

# TwinLine

**A concurrent terminal editor that finds the lines that look alike.**

[![CI](https://github.com/yusuffeness/TwinLine/actions/workflows/ci.yml/badge.svg)](https://github.com/yusuffeness/TwinLine/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/yusuffeness/TwinLine/graph/badge.svg)](https://codecov.io/gh/yusuffeness/TwinLine)
![Status](https://img.shields.io/badge/status-work%20in%20progress-orange)
![Version](https://img.shields.io/badge/version-0.6.0-blueviolet)
![Language](https://img.shields.io/badge/language-C17-blue)
![License](https://img.shields.io/badge/license-MIT-green)

</div>

---

> [!NOTE]
> TwinLine is under active development. v0.6.0 brings the terminal UI to
> life: a live progress view with per-thread bars, animated result cards with
> a word diff, and toasts, all drawn by a render thread that sleeps when
> nothing moves. Editing features arrive in v0.7.0. Follow the
> [roadmap](docs/ROADMAP.md) to see what is next.

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

In a terminal, TwinLine opens its interactive UI:

```text
╭─────────────────────────────────────────────────────────────────╮
│  ████████╗██╗    ██╗██╗███╗   ██╗██╗     ██╗███╗   ██╗███████╗  │
│  ╚══██╔══╝██║    ██║██║████╗  ██║██║     ██║████╗  ██║██╔════╝  │
│     ██║   ██║ █╗ ██║██║██╔██╗ ██║██║     ██║██╔██╗ ██║█████╗    │
│     ██║   ██║███╗██║██║██║╚██╗██║██║     ██║██║╚██╗██║██╔══╝    │
│     ██║   ╚███╔███╔╝██║██║ ╚████║███████╗██║██║ ╚████║███████╗  │
│     ╚═╝    ╚══╝╚══╝ ╚═╝╚═╝  ╚═══╝╚══════╝╚═╝╚═╝  ╚═══╝╚══════╝  │
│ ✻ Welcome to TwinLine v0.6.0                                    │
│   /help for commands · ? for shortcuts · Tab to complete        │
╰─────────────────────────────────────────────────────────────────╯

> /threads
  ⎿ ✔ Thread panel on: each thread's progress is shown during analysis.
> /analyze
  ⎿ ✔ Analyzing 4662 lines in the background. Use 'status' or 'cancel'.
 ✺ Analyzing… 0.4 s · 44% · 4.8M / 11M pairs · 2 threads · esc to cancel
   █████████████████████▍░░░░░░░░░░░░░░░░░░░░░░░░░░  44%
     T1  █████████████████▍░░░░░░░░░░░░░░░░░░░░  46%
     T2  ████████████████▍░░░░░░░░░░░░░░░░░░░░░  43%
╭────────────────────────────────────────────────────────────────────╮
│ > Type a command, or / for the menu                                │
╰────────────────────────────────────────────────────────────────────╯
 ▸ med.txt · 4,662 lines · 2 threads · exhaustive
```

When the analysis is done, its result card counts up into place and a toast
confirms it:

```text
✔ Analysis finished in 1.04 s with 2 threads
  ⎿ 4,662 lines · 10,864,791 line pairs · mean J 0.0242
    ● 36 identical pairs    ● 40 near duplicates (J ≥ 0.70)
    Most similar pair  J 0.86
      917  ...disk started carefully!
     1327  ...disk started task
```

Output scrolls like any terminal program; only the input box, the command
menu and the status bar at the bottom are redrawn in place. Results of a
background analysis appear above the input box without disturbing what you
are typing, and the words that differ in the most similar pair are
highlighted (removed in red, added in green).

A render thread draws at 30 frames per second while something moves and
redraws only the rows that changed. When nothing moves it sleeps, so an idle
TwinLine uses no CPU.

### Commands

Commands are typed with a leading `/` in the terminal UI (`/open notes.txt`);
the `/` is optional in the plain interface.

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
| `threads`        | Toggle the per-thread progress panel                         |
| `help`           | List the commands                                            |
| `quit[!]`        | Exit; asks again when there are unsaved changes              |

Command-line options:

| Option | Meaning |
| ------ | ------- |
| `--threads=N` | Worker threads for the analysis (default: one per processor) |
| `--exhaustive` | Compare every pair; slower, but also reports the mean similarity |
| `--fold=turkish` | Fold `I` to dotless `ı` as in Turkish (default folds it to `i`) |
| `--theme=NAME` | Colour theme: `dark` (default), `light`, `dracula`, `nord` |
| `--no-color` | No colours (the `NO_COLOR` environment variable works too) |
| `--no-animation` | No animations: still frames, progress updated 4 times a second (`TWINLINE_REDUCE_MOTION=1` works too) |
| `--plain` | The plain line interface, as used when input is piped |

### Keyboard

| Key | Action |
| --- | ------ |
| `/` | Open the command menu; keep typing to filter it (fuzzy) |
| `Tab` | Complete the selected command, or a file name after `/open` and `/save` |
| `↑` `↓` | Move in the menu, or browse the command history |
| `Ctrl+R` | Search the history |
| `Esc` | Close the menu, or cancel a running analysis |
| `Ctrl+A` `Ctrl+E` | Start / end of the line |
| `Ctrl+←` `Ctrl+→` | Previous / next word |
| `Ctrl+U` `Ctrl+K` `Ctrl+W` | Delete to the start, to the end, the previous word |
| `Ctrl+L` | Clear the screen |
| `?` | Show the shortcuts |
| `Ctrl+C` twice | Exit |

The UI uses 24-bit colour when the terminal announces it (`COLORTERM`), the
256-colour palette otherwise, and no colour with `NO_COLOR` or `--no-color`.
When input or output is not a terminal (pipes, scripts, CI), TwinLine falls
back to the plain interface automatically.

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

- **Background analysis** that never blocks the editor: analysis runs on a
  snapshot of the document while you continue to edit.
- **Parallel engine**: all-pairs Jaccard similarity split across worker
  threads, plus MinHash + LSH for very large files.
- **Unicode-aware** tokenisation, including Turkish case folding.
- **Side-by-side view** of all near-duplicate pairs, clustering and
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
src/                Sources: core, analysis, cmd (commands), tui (terminal UI)
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
