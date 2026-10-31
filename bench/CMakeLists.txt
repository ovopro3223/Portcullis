# Throughput benchmark runner. Configure with -DPORTCULLIS_BUILD_BENCH=ON.
#
#   cmake --build --preset <p> --target portcullis_bench
#   build/<p>/bench/portcullis_bench            # JSON lines on stdout
#   python3 scripts/gen-benchmarks.py           # runs it, writes BENCHMARKS.md
#
# A benchmark needs optimisation whatever CMAKE_BUILD_TYPE says (the default
# presets build Debug), so -O2 is forced unless the config already optimises.
add_executable(portcullis_bench bench.cpp)
target_link_libraries(portcullis_bench PRIVATE portcullis::portcullis)
target_include_directories(portcullis_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(portcullis_bench PRIVATE
  -Wall -Wextra -Werror
  $<$<NOT:$<CONFIG:Release,RelWithDebInfo,MinSizeRel>>:-O2>)

# Smoke: the runner builds, every parser accepts its sample, output is emitted.
# 20 repetitions instead of 2000; the numbers are not meaningful, the run is.
add_test(NAME bench_smoke COMMAND portcullis_bench --iterations 20)
