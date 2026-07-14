cmake_minimum_required(VERSION 3.14)
project(NanoMatch VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags for ultra-low latency & zero overhead
if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4 /O2")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O3 -march=native -fno-exceptions -fno-rtti")
endif()

include_directories(${CMAKE_SOURCE_DIR}/include)

# Google Test & Benchmark integration
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)
FetchContent_MakeAvailable(googletest)
FetchContent_Declare(
  googlebenchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG v1.8.3
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "Disable benchmark tests" FORCE)
set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "Disable Werror in benchmark" FORCE)
FetchContent_MakeAvailable(googlebenchmark)

# Directories
add_subdirectory(src)
add_subdirectory(tests)
add_subdirectory(benchmarks)
