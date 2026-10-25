cmake_minimum_required(VERSION 3.20)
project(OrderBookEngine CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Wpedantic)
add_executable(orderbookengine
    src/main.cpp
    src/Orderbook.cpp
)
target_include_directories(orderbookengine PUBLIC src/)

enable_testing()
add_subdirectory(tests)
add_subdirectory(benchmarks)
