# Testing

TwinLine is tested at four levels. All of them run with a single command:

```sh
make test        # or: ctest --test-dir build --output-on-failure
```

| Level | Where | What it checks |
| ----- | ----- | -------------- |
| Unit | `tests/unit/` | Each module in isolation: UTF-8 decoding and folding, feature sets, documents, file I/O, the analysis, the background job, and the terminal UI (keys, line editing, completion, canvas, widgets) |
| Integration | `tests/integration/cases/` | The real `twinline` binary, driven through standard input and compared with golden files |
| Terminal UI | `tests/integration/tui_smoke.py` | The interactive UI in a pseudo-terminal: keys are typed and the screen is checked |
| Fuzz | `tests/fuzz/` | Invariants of the UTF-8 decoder, the tokenizer and the command parser under random input |
| Dynamic analysis | CI | AddressSanitizer, UndefinedBehaviorSanitizer, ThreadSanitizer and Valgrind over the whole suite |

Tests carry CTest labels, so a subset can be run on its own:

```sh
ctest --test-dir build -L unit
ctest --test-dir build -L integration
ctest --test-dir build -R fileio
```

## Unit tests

Unit tests use `tests/tw_test.h`, a small header-only harness with no
dependencies. A test is a function declared with `TW_TEST` and registered with
`TW_RUN` in the file's `main`:

```c
TW_TEST(word_order_matters)
{
    TW_CHECK_NEAR(similarity("a b", "b a", TW_FOLD_DEFAULT), 0.5, 1e-12);
}
```

Available checks: `TW_CHECK`, `TW_CHECK_EQ_INT`, `TW_CHECK_EQ_SIZE`,
`TW_CHECK_EQ_STR` and `TW_CHECK_NEAR`. A failing check prints its location and
the values involved, then moves on to the next test.

To add a test file, create `tests/unit/test_<name>.c` and add `<name>` to
`TWINLINE_UNIT_TESTS` in `tests/CMakeLists.txt`.

## Integration tests

Each directory in `tests/integration/cases/` is one scenario:

```
cases/edit_and_save/
├── stdin.txt              commands typed into twinline
├── args.txt               command-line arguments, one per line (optional)
├── files/                 fixtures copied into the working directory (optional)
└── expected/
    ├── stdout.txt         expected standard output
    ├── stderr.txt         expected standard error (omitted when empty)
    ├── exit.txt           expected exit status (omitted when 0)
    └── files/             files that must exist afterwards, byte for byte
```

Every scenario runs in a fresh temporary directory. Timings such as
`Finished in 0.012 s` and the version number are normalised before comparison. New cases are picked up
automatically when CMake is re-run.

To create or refresh the golden files after an intentional change, run the
case with `--update` and review the diff before committing:

```sh
python3 tests/integration/run_case.py --binary build/twinline \
    --case tests/integration/cases/my_case --update
git diff tests/integration/cases/my_case
```

Set `TWINLINE_TEST_WRAPPER` to run the binary under another tool, for example
`TWINLINE_TEST_WRAPPER="valgrind --error-exitcode=99 -q"`.

## Terminal UI tests

The UI is drawn into a canvas, a grid of styled cells, before it is turned
into escape sequences. Unit tests render widgets into a canvas and compare
rows as plain text, so layout, wrapping and clipping are tested without a
terminal:

```c
TW_CHECK_EQ_STR(row(&canvas, 0), "╭──────────╮");
```

Animated widgets take their clock as a parameter (`view.time`,
`card.progress`, `welcome.shine`), so any frame of an animation can be
rendered and checked in a test.

`tests/integration/tui_smoke.py` starts the real binary in a pseudo-terminal,
types keys (menus, Tab completion, history search, paste, Ctrl+C) and waits
for the expected text on the screen. The first session runs with animations
and the second with `--no-animation`, so both drawing paths are covered. It needs a POSIX system and runs in CI
on Linux and macOS, also under the sanitizers and Valgrind
(`TWINLINE_TUI_TIMEOUT` raises its time limit for slow runs).

## Fuzzing

The fuzz targets in `tests/fuzz/` follow the libFuzzer interface. They are
built in one of two ways:

- **Regression mode** (default, any compiler): each target is linked with
  `standalone_main.c` and replays the seed inputs in `tests/fuzz/corpus/` as a
  normal CTest test.
- **Fuzzing mode** (Clang only): libFuzzer explores new inputs.

```sh
cmake -S . -B build-fuzz -DCMAKE_C_COMPILER=clang -DTWINLINE_BUILD_FUZZERS=ON \
      -DTWINLINE_ENABLE_ASAN=ON -DTWINLINE_ENABLE_UBSAN=ON
cmake --build build-fuzz --target fuzz_utf8
./build-fuzz/tests/fuzz_utf8 -max_total_time=60 new-corpus/ tests/fuzz/corpus/utf8/
```

When a fuzzer finds a crash, add the input to the target's seed directory so it
becomes a permanent regression test.

## Sanitizers and coverage

```sh
make asan        # AddressSanitizer + UndefinedBehaviorSanitizer
make tsan        # ThreadSanitizer
make coverage    # line, function and branch coverage with gcovr
```

On some Linux kernels ThreadSanitizer stops with "unexpected memory mapping".
Run it with address-space randomisation disabled:
`setarch "$(uname -m)" -R ctest --test-dir build-tsan`.

## Continuous integration

`.github/workflows/ci.yml` runs on every push and pull request:

| Job | Details |
| --- | ------- |
| Build and test | GCC and Clang on Ubuntu, Clang on macOS, warnings as errors |
| Sanitizers | ASan + UBSan, and TSan, with Clang |
| Valgrind | Unit and integration tests under memcheck |
| Format and static analysis | clang-format and clang-tidy, pinned versions |
| Coverage | gcovr report, fails below 80 % line coverage, uploaded to Codecov |
| Fuzz | Each target fuzzed for 60 seconds with ASan and UBSan |

`.github/workflows/codeql.yml` runs GitHub CodeQL weekly and on every change
to `main` once the repository is public.
