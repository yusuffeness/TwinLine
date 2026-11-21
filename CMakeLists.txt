cmake_minimum_required(VERSION 3.16)

project(
  TwinLine
  VERSION 0.2.0
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
  src/analysis/utf8.c
  src/analysis/featureset.c
  src/analysis/analysis.c
  src/analysis/job.c
  src/cmd/app.c
  src/cmd/commands.c
  src/cmd/report.c
  src/cmd/repl.c)

target_include_directories(
  twinline_core PUBLIC ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src
                       ${PROJECT_BINARY_DIR}/generated)
target_link_libraries(twinline_core PUBLIC twinline_options Threads::Threads)

# ---------------------------------------------------------------------------
# Executable
# ---------------------------------------------------------------------------
add_executable(twinline src/main.c)
target_link_libraries(twinline PRIVATE twinline_core)

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS twinline RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
