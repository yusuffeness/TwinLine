# ---------------------------------------------------------------------------
# Unit tests: one executable per module
# ---------------------------------------------------------------------------
set(TWINLINE_UNIT_TESTS
    status
    utf8
    featureset
    document
    fileio
    analysis
    parallel
    job)

foreach(name IN LISTS TWINLINE_UNIT_TESTS)
  add_executable(test_${name} unit/test_${name}.c)
  target_link_libraries(test_${name} PRIVATE twinline_core m)
  add_test(NAME unit.${name} COMMAND test_${name})
  set_tests_properties(unit.${name} PROPERTIES LABELS unit TIMEOUT 120)
endforeach()

# ---------------------------------------------------------------------------
# Integration tests: golden-file scenarios driven by run_case.py
# ---------------------------------------------------------------------------
find_package(Python3 COMPONENTS Interpreter)

if(Python3_Interpreter_FOUND)
  file(
    GLOB case_dirs
    LIST_DIRECTORIES true
    ${CMAKE_CURRENT_SOURCE_DIR}/integration/cases/*)
  foreach(case_dir IN LISTS case_dirs)
    if(IS_DIRECTORY ${case_dir})
      get_filename_component(case_name ${case_dir} NAME)
      add_test(NAME integration.${case_name}
               COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/integration/run_case.py
                       --binary $<TARGET_FILE:twinline> --case ${case_dir})
      set_tests_properties(integration.${case_name} PROPERTIES LABELS integration TIMEOUT 180)
    endif()
  endforeach()
else()
  message(WARNING "Python 3 not found: integration tests are disabled")
endif()

# ---------------------------------------------------------------------------
# Fuzz targets: libFuzzer when TWINLINE_BUILD_FUZZERS is ON, otherwise a
# standalone driver that replays the seed corpus as a regression test.
# ---------------------------------------------------------------------------
set(TWINLINE_FUZZ_TARGETS utf8 featureset command_parse)

foreach(name IN LISTS TWINLINE_FUZZ_TARGETS)
  set(seed_dir ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/corpus/${name})

  if(TWINLINE_BUILD_FUZZERS)
    add_executable(fuzz_${name} fuzz/fuzz_${name}.c)
    target_compile_options(fuzz_${name} PRIVATE -fsanitize=fuzzer)
    target_link_options(fuzz_${name} PRIVATE -fsanitize=fuzzer)
    add_test(NAME fuzz.${name} COMMAND fuzz_${name} -runs=0 ${seed_dir})
  else()
    add_executable(fuzz_${name} fuzz/fuzz_${name}.c fuzz/standalone_main.c)
    file(GLOB seeds ${seed_dir}/*)
    add_test(NAME fuzz.${name} COMMAND fuzz_${name} ${seeds})
  endif()

  target_link_libraries(fuzz_${name} PRIVATE twinline_core)
  set_tests_properties(fuzz.${name} PROPERTIES LABELS fuzz TIMEOUT 120)
endforeach()
