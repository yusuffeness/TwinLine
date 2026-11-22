add_executable(bench_analysis bench_analysis.c)
target_link_libraries(bench_analysis PRIVATE twinline_core)

add_executable(bench_load bench_load.c)
target_link_libraries(bench_load PRIVATE twinline_core)
