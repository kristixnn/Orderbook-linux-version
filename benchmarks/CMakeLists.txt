cmake_minimum_required(VERSION 3.20)
include(FetchContent)

# 1. Fetch Google Benchmark from GitHub
FetchContent_Declare(
    googlebenchmark
    URL https://github.com/google/benchmark/archive/refs/tags/v1.8.3.zip
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googlebenchmark)

# 2. Define the benchmark executable
add_executable(OrderbookBenchmarks
    BenchMarkOrderbook.cpp
    ../src/Orderbook.cpp
)

# 3. Include directories and link libraries
target_include_directories(OrderbookBenchmarks PRIVATE ../src)
target_link_libraries(OrderbookBenchmarks
    PRIVATE
        benchmark::benchmark_main
)
