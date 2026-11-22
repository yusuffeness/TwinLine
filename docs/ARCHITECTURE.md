# Architecture

TwinLine is about 10,000 lines of C17 in four layers. Each layer only uses
the ones below it.

```text
 main.c         options, configuration, interactive or scripting mode
   │
 tui/           the terminal interface: raw mode, canvas, widgets, render thread
   │
 cmd/           commands, scripting subcommands, settings, configuration,
   │            reports and exports, background services
   │
 analysis/      UTF-8, feature sets, the feature index, the three strategies,
   │            clusters, the background job
   │
 core/          documents, files, sessions and buffers, undo, recovery files
```

`include/twinline/` holds the headers of `core/` and `analysis/`; the other
layers keep theirs next to their sources. Everything except `main.c` is
built into the static library `twinline_core`, which the tests, the fuzz
targets and the benchmarks link against.

## Modules

| Module | Files | Responsibility |
| ------ | ----- | -------------- |
| Documents | `core/document.c` | Lines in an array; `tw_document_splice()` is the one way to change them; memory-mapped lines in large-file mode |
| Files | `core/fileio.c` | Loading (stdio or `mmap`), atomic saving through a temporary file and `rename` |
| Sessions | `core/session.c` | The active buffer and its lock, dirty tracking against the saved undo state, what the file on disk looked like |
| Undo | `core/undo.c` | Every splice recorded with both sides; state ids unique across buffers |
| Recovery | `core/recovery.c` | Recovery files next to the original, written atomically with mode 0600 |
| UTF-8 | `analysis/utf8.c` | Decoding, case folding (default and Turkish), word characters, display width |
| Features | `analysis/featureset.c` | A line as a sorted set of word and word n-gram hashes |
| Index | `analysis/index.c` | All feature sets of a document in one arena, built by several threads |
| Jaccard search | `analysis/analysis.c` | Every pair, exact pruning, worker threads; the summary of `analyze` |
| Similarity | `analysis/similarity.c` | Identical groups, MinHash and LSH, pairs and union-find clusters |
| Job | `analysis/job.c` | One background analysis on a snapshot, with progress and cancellation |
| Commands | `cmd/commands.c` | Every editor command, writing to `app->out` and `app->err` |
| Scripting | `cmd/cli.c` | `analyze`, `clusters`, `dedupe`, `export`, `config`; exit codes |
| Settings | `cmd/settings.c`, `cmd/config.c` | Settings by name; the `config.toml` reader |
| Reports | `cmd/report.c`, `cmd/export.c` | Text reports; JSON, CSV and HTML |
| Services | `cmd/services.c` | Autosave and file watching on their own thread |
| Terminal | `tui/term.c`, `tui/keys.c` | Raw mode, signals, resizes; decoding keys and escape sequences |
| Drawing | `tui/canvas.c`, `tui/widgets.c` | A grid of styled cells; every widget draws into it |
| Interface | `tui/ui.c` | The live region, the render thread, menus, history, completion |

## Threads

| Thread | Started by | Does |
| ------ | ---------- | ---- |
| Main | `main()` | Reads keys or lines, runs commands, draws on input |
| Render | `tw_ui_run()` | Draws animation frames at 30 FPS while something moves; sleeps on a condition variable otherwise |
| Analysis | `tw_job_start()` | Runs one analysis on a snapshot and its worker threads; reports through `app->emit` or `app->on_result` |
| Workers | the analysis | Score rows or LSH bands from a shared atomic cursor; each keeps its own results, merged at the end |
| Index builders | the analysis | Build the feature sets of a share of the lines each |
| Services | `tw_app_start_services()` | Once a second, or when inotify reports a change: checks the open files and writes recovery files |

## Locks

There are three locks, always taken in this order:

1. `ui->lock`: the interface state (the input line, menus, the live region);
2. `app->session.lock`: the active buffer and the list of buffers;
3. `app->services.lock`: only the services thread's stop flag.

Nothing holds the session lock while it calls back into the interface:
background work copies what it needs under the lock, releases it, and only
then calls `app->emit` or `app->notify`, which take the interface lock.
Commands run with the interface lock released, so a long command never
freezes the screen. ThreadSanitizer runs over the whole test suite in CI.

## Data flow of an analysis

1. `analyze` takes a snapshot of the active document under the session lock;
   editing continues on the original.
2. The job builds the feature index of the snapshot (in parallel for large
   documents), then runs the chosen strategy on worker threads.
3. Progress is a set of atomic counters that the render thread reads for the
   progress bar and the per-thread panel.
4. The result goes to `app->on_result` (the interface's result card) or is
   printed by `app->emit` (the plain interface).

## Output and front ends

Commands never write to the terminal directly. They write to `app->out` and
`app->err`, which are `stdout` and `stderr` in the plain interface and memory
streams in the terminal interface; the interface then styles each line
(success, warning, error) above the input box. Messages from other threads
use `app->emit`, short notices `app->notify`, and `edit` uses
`app->prefill`. The same command code therefore serves the plain REPL, the
terminal interface and the tests.

## Design decisions

The reasons behind the larger choices are recorded in [docs/adr](adr/):

- [0001](adr/0001-raw-ansi-instead-of-ncurses.md): raw ANSI output instead of ncurses
- [0002](adr/0002-inline-live-region.md): an inline live region instead of the alternate screen
- [0003](adr/0003-analysis-on-a-snapshot.md): analysis on a snapshot of the document
- [0004](adr/0004-exact-first-approximate-on-request.md): exact results first, approximation on request
- [0005](adr/0005-recovery-files-next-to-the-original.md): recovery files next to the original
