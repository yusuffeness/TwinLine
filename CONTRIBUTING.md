# Contributing to TwinLine

Thank you for helping. Bug reports, ideas, documentation fixes and code are
all welcome.

## Reporting a bug or asking for a feature

Open an [issue](https://github.com/yusuffeness/TwinLine/issues/new/choose)
with the matching template. For a bug, `twinline --version`, your system and
the smallest input that shows the problem help the most. Security problems
go through [SECURITY.md](SECURITY.md) instead.

## Building and testing

You need a C17 compiler (GCC or Clang), CMake 3.16 or newer, Make and
Python 3.

```sh
make            # release build in build/ (make debug for build-debug/)
make test       # every test: unit, integration, terminal UI, fuzz seeds
make asan       # AddressSanitizer + UBSan
make tsan       # ThreadSanitizer
make format     # clang-format
make tidy       # clang-tidy
make help       # everything else
```

[docs/TESTING.md](docs/TESTING.md) explains the test suite, and
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) how the code fits together.

## Making a change

1. Open an issue first for anything larger than a small fix, so we can agree
   on the approach before you spend time on it.
2. Create a branch from `main`.
3. Keep the change focused. Add or update tests: unit tests for logic, a
   golden-file case in `tests/integration/cases/` for behaviour you can see
   from the command line.
4. Run `make test` and `make format`. CI also runs the sanitizers,
   clang-tidy, coverage and fuzzing on every pull request.
5. Update `README.md`, the man page (`man/twinline.1`) or `docs/` if users
   will notice the change, and add a line under "Unreleased" in
   `CHANGELOG.md`.
6. Open a pull request and fill in the template.

## Code style

- C17 with the warnings in `CMakeLists.txt`; the code must build without
  warnings on GCC and Clang.
- `clang-format` decides the layout; `clang-tidy` must be clean.
- Names are `snake_case` with a `tw_` prefix for anything outside one file.
- Functions report errors through `tw_status`; nothing calls `exit()` except
  `main()`.
- Comments explain why, not what. Every public function has a comment in its
  header.
- Messages to users are short, plain English sentences that say what to do
  next.

## Commit messages

The history uses [Conventional Commits](https://www.conventionalcommits.org):
`feat:`, `fix:`, `docs:`, `test:`, `perf:`, `refactor:`, `ci:`, with an
optional scope such as `fix(undo):`. The subject is in the imperative ("add",
not "added") and under 72 characters; the body explains why.

## License

By contributing, you agree that your contribution is licensed under the
[MIT License](LICENSE) of the project.
