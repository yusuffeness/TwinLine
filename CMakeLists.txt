cmake_minimum_required(VERSION 3.16)

project(
  TwinLine
  VERSION 1.0.0
  DESCRIPTION "A concurrent terminal editor that finds the lines that look alike"
  HOMEPAGE_URL "https://github.com/yusuffeness/TwinLine"
  LANGUAGES C)

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(TWINLINE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(TWINLINE_ENABLE_ASAN "Build with AddressSanitizer" OFF)
option(TWINLINE_ENABLE_UBSAN "Build with UndefinedBehaviorSanitizer" OFF)
option(TWINLINE_ENABLE_TSAN "Build with ThreadSanitizer" OFF)
option(TWINLINE_ENABLE_COVERAGE "Instrument the build for code coverage" OFF)
option(TWINLINE_BUILD_TESTS "Build the unit, integration and fuzz regression tests" ON)
option(TWINLINE_BUILD_BENCHMARKS "Build the benchmark programs" ON)
option(TWINLINE_BUILD_FUZZERS "Build the fuzz targets with libFuzzer (Clang only)" OFF)

if(TWINLINE_ENABLE_TSAN AND TWINLINE_ENABLE_ASAN)
  message(FATAL_ERROR "ThreadSanitizer and AddressSanitizer cannot be enabled together")
endif()

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# Shared compile options (applied through an interface target)
# ---------------------------------------------------------------------------
add_library(twinline_options INTERFACE)

target_compile_definitions(twinline_options INTERFACE _XOPEN_SOURCE=700)

# macOS headers hide some POSIX 2008 functions (such as mkdtemp) when
# _XOPEN_SOURCE is defined; _DARWIN_C_SOURCE makes them visible again.
if(APPLE)
  target_compile_definitions(twinline_options INTERFACE _DARWIN_C_SOURCE)
endif()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(
    twinline_options
    INTERFACE -Wall
              -Wextra
              -Wpedantic
              -Wshadow
              -Wconversion
              -Wsign-conversion
              -Wstrict-prototypes
              -Wmissing-prototypes
              -Wformat=2
              -Wundef
              $<$<BOOL:${TWINLINE_WARNINGS_AS_ERRORS}>:-Werror>)

  set(_tw_sanitizers "")
  if(TWINLINE_ENABLE_ASAN)
    list(APPEND _tw_sanitizers address)
  endif()
  if(TWINLINE_ENABLE_UBSAN)
    list(APPEND _tw_sanitizers undefined)
  endif()
  if(TWINLINE_ENABLE_TSAN)
    list(APPEND _tw_sanitizers thread)
  endif()

  if(_tw_sanitizers)
    list(JOIN _tw_sanitizers "," _tw_sanitizer_list)
    target_compile_options(twinline_options INTERFACE -fsanitize=${_tw_sanitizer_list}
                                                      -fno-omit-frame-pointer -g)
    target_link_options(twinline_options INTERFACE -fsanitize=${_tw_sanitizer_list})
    message(STATUS "TwinLine: sanitizers enabled: ${_tw_sanitizer_list}")
  endif()

  if(TWINLINE_ENABLE_COVERAGE)
    # Atomic counters: the analysis runs on several threads, and racy counter
    # updates would corrupt the coverage data.
    target_compile_options(twinline_options INTERFACE --coverage -fprofile-update=atomic -O0 -g)
    target_link_options(twinline_options INTERFACE --coverage)
    message(STATUS "TwinLine: coverage instrumentation enabled")
  endif()
endif()

if(TWINLINE_BUILD_FUZZERS)
  if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "TWINLINE_BUILD_FUZZERS requires Clang (libFuzzer)")
  endif()
  # Instrument the library too, so the fuzzer gets coverage feedback from it.
  target_compile_options(twinline_options INTERFACE -fsanitize=fuzzer-no-link)
endif()

# ---------------------------------------------------------------------------
# Generated version header
# ---------------------------------------------------------------------------
configure_file(${PROJECT_SOURCE_DIR}/cmake/version.h.in
               ${PROJECT_BINARY_DIR}/generated/twinline/version.h @ONLY)

# ---------------------------------------------------------------------------
# Library: everything except main(), so tests and benchmarks can link to it
# ---------------------------------------------------------------------------
add_library(
  twinline_core STATIC
  src/core/status.c
  src/core/document.c
  src/core/fileio.c
  src/core/session.c
  src/core/sysinfo.c
  src/core/undo.c
  src/core/recovery.c
  src/analysis/utf8.c
  src/analysis/featureset.c
  src/analysis/index.c
  src/analysis/similarity.c
  src/analysis/analysis.c
  src/analysis/job.c
  src/cmd/app.c
  src/cmd/commands.c
  src/cmd/report.c
  src/cmd/settings.c
  src/cmd/config.c
  src/cmd/cli.c
  src/cmd/services.c
  src/cmd/export.c
  src/cmd/repl.c
  src/tui/strbuf.c
  src/tui/color.c
  src/tui/theme.c
  src/tui/canvas.c
  src/tui/widgets.c
  src/tui/keys.c
  src/tui/lineedit.c
  src/tui/complete.c
  src/tui/term.c
  src/tui/ui.c)

target_include_directories(
  twinline_core PUBLIC ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
                       ${PROJECT_BINARY_DIR}/generated)
target_link_libraries(twinline_core PUBLIC twinline_options Threads::Threads m)

# ---------------------------------------------------------------------------
# Executable
# ---------------------------------------------------------------------------
add_executable(twinline src/main.c)
target_link_libraries(twinline PRIVATE twinline_core)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(TWINLINE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(TWINLINE_BUILD_BENCHMARKS)
  add_subdirectory(bench)
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
set(CMAKE_INSTALL_DOCDIR "share/doc/twinline" CACHE PATH "Documentation directory")
include(GNUInstallDirs)
install(TARGETS twinline RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES man/twinline.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
install(FILES README.md CHANGELOG.md LICENSE docs/CONFIGURATION.md
        DESTINATION ${CMAKE_INSTALL_DOCDIR})

# ---------------------------------------------------------------------------
# Packages: `cpack -G DEB` (Debian/Ubuntu) and `cpack -G TGZ` from a build dir
# ---------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME "twinline")
set(CPACK_PACKAGE_VENDOR "Yusuf Enes Kutuk")
set(CPACK_PACKAGE_CONTACT "Yusuf Enes Kutuk <https://github.com/yusuffeness>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
string(CONCAT CPACK_PACKAGE_DESCRIPTION
       "TwinLine is a line-based terminal editor with a built-in near-duplicate detector. "
       "It finds identical and nearly identical lines with an exact parallel Jaccard search "
       "or MinHash for large files, groups them into clusters and removes the copies, "
       "interactively or from scripts.")
set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_FILE_NAME "twinline-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_STRIP_FILES ON)
set(CPACK_DEBIAN_PACKAGE_SECTION "editors")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
include(CPack)
