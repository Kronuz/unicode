cmake_minimum_required(VERSION 3.14)
project(unicode LANGUAGES CXX)

# Header-only: unicode.hh plus the generated unicode_tables.hh.
add_library(unicode INTERFACE)
target_include_directories(unicode INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(unicode INTERFACE cxx_std_20)
add_library(unicode::unicode ALIAS unicode)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  enable_testing()
  add_executable(unicode_test test/test.cc)
  target_link_libraries(unicode_test PRIVATE unicode)
  add_test(NAME unicode COMMAND unicode_test)

  # Differential test: decoding, encoding and the tables against a frozen copy of the original
  # header (test/unicode_old.hh).
  add_executable(unicode_diff_test test/diff_test.cc)
  target_link_libraries(unicode_diff_test PRIVATE unicode)
  add_test(NAME unicode_diff COMMAND unicode_diff_test)

  # Benchmark against the frozen original (not run by ctest).
  add_executable(unicode_bench bench/bench.cc)
  target_link_libraries(unicode_bench PRIVATE unicode)
  option(UNICODE_FUZZ "Build the libFuzzer target" OFF)
  if(UNICODE_FUZZ)
    add_executable(unicode_fuzz fuzz/fuzz_utf8.cc)
    target_link_libraries(unicode_fuzz PRIVATE unicode)
    target_compile_options(unicode_fuzz PRIVATE -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all)
    target_link_options(unicode_fuzz PRIVATE -fsanitize=fuzzer,address,undefined)
  endif()
endif()
