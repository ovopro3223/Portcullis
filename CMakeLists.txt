cmake_minimum_required(VERSION 3.25)
project(portcullis VERSION 0.1.0 LANGUAGES CXX)

add_library(portcullis INTERFACE)
add_library(portcullis::portcullis ALIAS portcullis)
target_include_directories(portcullis INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(portcullis INTERFACE cxx_std_20)

option(PORTCULLIS_BUILD_TESTS "Build the test suite" ON)
option(PORTCULLIS_BUILD_BENCH "Build the benchmarks" OFF)

if(PORTCULLIS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(PORTCULLIS_BUILD_BENCH)
  add_subdirectory(bench)
endif()
