# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.7.0] - 2026-11-22

### Added

- Editing commands: `insert N TEXT`, `delete N[-M]`, `replace N TEXT` and
  `edit N`, which puts the line into the terminal UI's input box (the plain
  interface prints it).
- `undo` and `redo` for every change, up to 1,000 per buffer. Undoing back
  to the last save clears the "modified" mark.
- Paged viewing: `show` prints 40 lines at a time and continues where it
  stopped, `show N` starts a page at line `N`, `show N-M` prints a range and
  `show!` everything. Documents of up to 40 lines are printed whole.
- `goto N` shows a line in context and moves the next page there.
- `find TEXT`: case-insensitive search using the same Unicode case folding
  as the analysis (including `--fold=turkish`); matches are highlighted in
  the terminal UI.
- Several open buffers: `new [FILE]`, `buffers`, `switch N` and `close[!]`.
  The status bar shows the buffer number when more than one is open, and
  `quit` warns about unsaved changes in any buffer.
- Large-file mode: files of 8 MiB and more are memory-mapped and split in
  place, about 1.7x faster to load and without an allocation per line.
  `status` reports it.
- `bench_load`, which compares the two ways of loading a file.
- Unit tests for splicing documents, undo and redo, the saved state and
  memory-mapped loading; integration tests for editing, viewing and buffers.

### Changed

- All document changes go through one splice operation, recorded by the
  undo history.
- `help` groups the commands into files and buffers, editing, viewing and
  analysis.

## [0.6.0] - 2026-11-22

### Added

- Render thread that draws at 30 frames per second while something moves and
  sleeps on a condition variable otherwise (no CPU use when idle). Only the
  rows of the live region that changed are redrawn.
- Live analysis view: spinner, shimmering "Analyzing…" label, elapsed time,
  percentage, pairs done / total, thread count and "esc to cancel".
- Gradient progress bar drawn to an eighth of a cell.
- `threads` command (`/threads` in the UI) that toggles a per-thread progress
  panel; the plain interface prints each thread's progress.
- Animated welcome banner with a light sweeping over the logo; a key press
  skips it.
- Result card whose numbers count up, with a `J 0.86` similarity badge and a
  word diff of the most similar pair (removed words in red, added in green).
- Toasts for finished, cancelled and failed analyses.
- Input box border that breathes while an analysis runs.
- `--no-animation` option and `TWINLINE_REDUCE_MOTION` environment variable
  for still frames.
- Unit tests for the progress bar, spinner, number formatting, word diff,
  canvas blitting and row comparison, the progress view, toasts and the
  result card.

### Changed

- The snippets of the most similar pair start just before the first word
  where the two lines differ, so the difference is visible in reports.
- Analysis progress tracks elapsed time and each worker thread.
- "Analysing" is now spelled "Analyzing" everywhere.

### Fixed

- A finished result card keeps no spinner glyph in the scrollback.

## [0.5.0] - 2026-11-22

### Added

- Interactive terminal UI, used automatically when standard input and output
  are terminals:
  - welcome banner with a gradient ASCII logo;
  - rounded, coloured input box that wraps long lines and handles UTF-8,
    wide characters and emoji;
  - slash-command menu with fuzzy filtering and highlighted matches;
  - `Tab` completion of commands and of file names after `/open` and
    `/save`, with a menu when several files match;
  - command history with `↑`/`↓` and reverse search with `Ctrl+R`;
  - Emacs-style editing keys, word movement and bracketed paste;
  - command results as indented blocks with ✔, ▲ and ✖ markers;
  - live analysis progress, `Esc` to cancel, and background results shown
    above the input without tearing it;
  - status bar with the file, unsaved changes, line count, threads and mode;
  - `?` for the keyboard shortcuts, `Ctrl+L` to clear, `Ctrl+C` twice to exit.
- Colour support in 24-bit, 256-colour and no-colour modes, chosen from
  `COLORTERM`, `TERM` and `NO_COLOR`.
- Four themes: `dark`, `light`, `dracula` and `nord`.
- Options `--theme=NAME`, `--no-color` and `--plain`.
- The terminal is restored on exit and on SIGTERM, SIGHUP, SIGINT and
  SIGQUIT; window resizes redraw the UI.
- A cell-based canvas (virtual screen) that all widgets draw into, so the UI
  is unit-tested as plain text without a terminal.
- Unit tests for key decoding, line editing, history, fuzzy matching, file
  completion, colours, the canvas and the widgets, and an end-to-end test that
  drives the UI in a pseudo-terminal.
- Dependabot groups all GitHub Actions updates into one pull request.

### Changed

- Commands write to the application's output streams instead of directly to
  standard output, so the UI can capture and format their results.
- The unsaved-changes notice on `quit` is a warning on standard error
  (`warning: you have unsaved changes...`).

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

[Unreleased]: https://github.com/yusuffeness/TwinLine/compare/v0.7.0...HEAD
[0.7.0]: https://github.com/yusuffeness/TwinLine/compare/v0.6.0...v0.7.0
[0.6.0]: https://github.com/yusuffeness/TwinLine/compare/v0.5.0...v0.6.0
[0.5.0]: https://github.com/yusuffeness/TwinLine/compare/v0.4.0...v0.5.0
[0.4.0]: https://github.com/yusuffeness/TwinLine/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/yusuffeness/TwinLine/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/yusuffeness/TwinLine/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/yusuffeness/TwinLine/releases/tag/v0.1.0
