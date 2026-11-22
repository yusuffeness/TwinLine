# Roadmap

TwinLine is developed in ten milestones. Each one ends with a tagged release
and leaves the repository in a working, presentable state.

| Milestone | Version | Theme |
| --------- | ------- | ----- |
| 0 | v0.1.0 | Project skeleton and build system |
| 1 | v0.2.0 | Modular core, Unicode handling and robustness fixes |
| 2 | v0.3.0 | Tests and continuous integration |
| 3 | v0.4.0 | Parallel analysis engine and benchmarks |
| 4 | v0.5.0 | Terminal UI foundations |
| 5 | v0.6.0 | Animations and live views |
| 6 | v0.7.0 | Editing features |
| 7 | v0.8.0 | Advanced analysis: MinHash/LSH, clustering, export |
| 8 | v0.9.0 | Services, configuration and scripting mode |
| 9 | v1.0.0 | Packaging, documentation and launch |

## Milestone 0 - Project skeleton (v0.1.0)

- [x] Directory layout
- [x] CMake build with strict warnings and sanitizer options
- [x] Makefile wrapper
- [x] Minimal executable with `--help` and `--version`
- [x] Repository configuration (gitignore, gitattributes, editorconfig, clang-format, clang-tidy)
- [x] License, README, changelog and roadmap

## Milestone 1 - Modular core (v0.2.0)

- [x] Split the original single-file program into `core`, `analysis` and `cmd` modules
- [x] UTF-8 aware case folding (including Turkish I/i and dotless i) and punctuation handling
- [x] 64-bit hashing
- [x] Atomic save (write to a temporary file, then rename)
- [x] Unsaved-changes tracking and exit warning
- [x] Cancellable analysis
- [x] Test corpus generator (`make corpus`)

## Milestone 2 - Tests and CI (v0.3.0)

- [x] Unit tests for the tokenizer, UTF-8, Jaccard, document and file I/O
- [x] Integration tests with golden files
- [x] GitHub Actions: GCC and Clang on Ubuntu and macOS
- [x] AddressSanitizer, UBSan, ThreadSanitizer and Valgrind jobs
- [x] clang-format and clang-tidy checks
- [x] Coverage reporting
- [x] Fuzz targets for the command parser and UTF-8 decoder
- [x] CodeQL

## Milestone 3 - Parallel analysis (v0.4.0)

- [x] Worker thread pool
- [x] Balanced split of the pair matrix
- [x] Lock-free per-thread results merged at the end
- [x] Size-based pruning
- [x] Arena allocation
- [x] Atomic progress counter
- [x] Benchmarks and speed-up chart

## Milestone 4 - Terminal UI foundations (v0.5.0)

- [x] Raw mode, resize handling and terminal restore
- [x] Key decoder
- [x] Truecolor / 256-colour / no-colour fallback
- [x] Themes
- [x] Widgets: box, input, output block, status bar
- [x] Welcome screen with gradient logo
- [x] Slash-command menu with fuzzy search and completion
- [x] Command history and reverse search
- [x] Virtual screen buffer for UI tests

## Milestone 5 - Animations and live views (v0.6.0)

- [x] Render thread at 30 FPS with damage tracking
- [x] Spinner and shimmer
- [x] Live analysis line with cancel on Esc
- [x] Gradient progress bar and per-thread panel
- [x] Breathing border, logo shine, counting results, toasts
- [x] Coloured diff view
- [x] `--no-animation` and reduced motion

## Milestone 6 - Editing features (v0.7.0)

- [x] Insert, delete, replace and inline edit
- [x] Paged show, goto and find
- [x] Undo and redo
- [x] Multiple buffers
- [x] Large-file mode with `mmap`

## Milestone 7 - Advanced analysis (v0.8.0)

- [x] Exact strategy
- [x] MinHash + LSH
- [x] Union-find clustering
- [x] Interactive de-duplication
- [x] Runtime settings (`/set`)
- [x] JSON, CSV and HTML export
- [x] Accuracy benchmark

## Milestone 8 - Services and scripting (v0.9.0)

- [x] Autosave and crash recovery
- [x] File watching
- [x] TOML configuration
- [x] Subcommands and flags
- [x] JSON output and exit codes

## Milestone 9 - Launch (v1.0.0)

- [ ] Release binaries for Linux and macOS
- [ ] Homebrew tap, Debian package and Docker image
- [ ] Man page
- [ ] Architecture, algorithm and configuration docs
- [ ] Logo and demo GIF
- [ ] Contributing guide, code of conduct, security policy and templates
- [ ] v1.0.0 release
