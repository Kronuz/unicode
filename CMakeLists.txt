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

  # Benchmark against the frozen original (not run by ctest). `unicode_bench --check` is a CTest test:
  # it compares every decoder in the bench with unicode.hh on every code point and on the bench inputs.
  add_executable(unicode_bench bench/bench.cc)
  target_link_libraries(unicode_bench PRIVATE unicode)
  add_test(NAME unicode_bench_check COMMAND unicode_bench --check)

  # Comparators for the benchmark, off by default: utf8proc (Unicode 17, same as the tables here) and
  # utf8cpp, fetched at pinned release tags and linked into unicode_bench only. SOURCE_SUBDIR names a
  # directory that does not exist, so FetchContent downloads the sources without running their CMake
  # projects. utf8proc is a C library: it is compiled here with the compiler driver and the build
  # type's flags of the C++ bench. Bjoern Hoehrmann's DFA decoder is written out in bench/bench.cc
  # and needs nothing.
  option(UNICODE_BENCH_COMPARE "Time unicode_bench against utf8proc and utf8cpp (fetches them)" OFF)
  if(UNICODE_BENCH_COMPARE)
    if(CMAKE_VERSION VERSION_LESS 3.18)
      message(FATAL_ERROR "UNICODE_BENCH_COMPARE needs CMake 3.18 or newer")
    endif()
    include(FetchContent)
    enable_language(C)
    FetchContent_Declare(utf8proc
      GIT_REPOSITORY https://github.com/JuliaStrings/utf8proc.git
      GIT_TAG        v2.11.3
      GIT_SHALLOW    TRUE
      SOURCE_SUBDIR  no-cmake-project)
    FetchContent_Declare(utfcpp
      GIT_REPOSITORY https://github.com/nemtrif/utfcpp.git
      GIT_TAG        v4.2.1
      GIT_SHALLOW    TRUE
      SOURCE_SUBDIR  no-cmake-project)
    FetchContent_MakeAvailable(utf8proc utfcpp)
    add_library(unicode_bench_utf8proc STATIC "${utf8proc_SOURCE_DIR}/utf8proc.c")
    target_include_directories(unicode_bench_utf8proc PUBLIC "${utf8proc_SOURCE_DIR}")
    target_compile_definitions(unicode_bench_utf8proc PUBLIC UTF8PROC_STATIC)
    target_link_libraries(unicode_bench PRIVATE unicode_bench_utf8proc)
    target_include_directories(unicode_bench PRIVATE "${utfcpp_SOURCE_DIR}/source")
    target_compile_definitions(unicode_bench PRIVATE UNICODE_BENCH_COMPARE)
  endif()

  option(UNICODE_FUZZ "Build the libFuzzer target" OFF)
  if(UNICODE_FUZZ)
    add_executable(unicode_fuzz fuzz/fuzz_utf8.cc)
    target_link_libraries(unicode_fuzz PRIVATE unicode)
    target_compile_options(unicode_fuzz PRIVATE -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all)
    target_link_options(unicode_fuzz PRIVATE -fsanitize=fuzzer,address,undefined)
  endif()
endif()
