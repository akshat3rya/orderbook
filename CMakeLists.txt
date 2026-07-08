cmake_minimum_required(VERSION 3.16)

project(OrderBook LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Common compiler warnings
add_compile_options(
    -Wall
    -Wextra
    -O2
)

# Source files
set(COMMON_SOURCES
    order_book.cpp
    matching_engine.cpp
)

# CLI executable
add_executable(orderbook
    main.cpp
    ${COMMON_SOURCES}
)

# Test executable
add_executable(run_tests
    tests.cpp
    ${COMMON_SOURCES}
)

# Optional: enable testing with CTest
enable_testing()

add_test(
    NAME OrderBookTests
    COMMAND run_tests
)