# Structure-aware fuzz targets — Linux + clang only (libFuzzer and the
# sanitizers ship with clang on Linux). This is a standalone CMake project so it
# does not depend on the top-level build: configure it directly, e.g.
#
#   cmake -S fuzz -B build/fuzz -G Ninja -DCMAKE_CXX_COMPILER=clang++
#   cmake --build build/fuzz
#   ./build/fuzz/fuzz_heartbeat -max_total_time=60
#
# Each target links the fuzzer runtime with ASan and UBSan; parse<S> over the
# fuzzer's bytes must never read out of bounds, and LLVMFuzzerCustomMutator
# drives the schema-aware mutate<S>.
cmake_minimum_required(VERSION 3.25)
project(portcullis_fuzz LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(FATAL_ERROR "portcullis fuzz targets require clang (libFuzzer). "
                      "Configure with -DCMAKE_CXX_COMPILER=clang++.")
endif()
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "portcullis fuzz targets build on Linux only "
                      "(libFuzzer + sanitizers). Use WSL or the CI job.")
endif()

set(PORTCULLIS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(PORTCULLIS_FUZZ_FLAGS -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer -g -O1)

foreach(target heartbeat png_chunks tiff_ifd)
  add_executable(fuzz_${target} targets/${target}.cpp)
  target_include_directories(fuzz_${target} PRIVATE
    ${PORTCULLIS_ROOT}/include
    ${PORTCULLIS_ROOT}/tests)  # for "schemas/<name>.hpp"
  target_compile_options(fuzz_${target} PRIVATE -Wall -Wextra -Werror ${PORTCULLIS_FUZZ_FLAGS})
  target_link_options(fuzz_${target} PRIVATE ${PORTCULLIS_FUZZ_FLAGS})
endforeach()
