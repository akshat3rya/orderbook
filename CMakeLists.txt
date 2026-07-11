cmake_minimum_required(VERSION 3.16)
project(OrderBook LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Equivalent to:
# CXXFLAGS := -std=c++17 -O2 -Wall -Wextra
if(MSVC)
    add_compile_options(/O2 /W4)
else()
    add_compile_options(-O2 -Wall -Wextra)
endif()
find_package(Threads REQUIRED)
# ------------------------------------------------------------------------------
# Source lists
# ------------------------------------------------------------------------------
set(CLI_SRCS main.cpp order_book.cpp matching_engine.cpp)
set(TEST_SRCS tests.cpp order_book.cpp matching_engine.cpp)
set(SERVER_SRCS server.cpp order_book.cpp matching_engine.cpp net_io.cpp)
set(CLIENT_SRCS client.cpp net_io.cpp)
set(SUBSCRIBER_SRCS subscriber.cpp net_io.cpp)
set(PROTO_TEST_SRCS test_protocol.cpp net_io.cpp)
set(QUEUE_TEST_SRCS test_command_queue.cpp net_io.cpp)
# ------------------------------------------------------------------------------
# Executables
# ------------------------------------------------------------------------------
add_executable(orderbook ${CLI_SRCS})
add_executable(run_tests ${TEST_SRCS})
add_executable(server ${SERVER_SRCS})
target_link_libraries(server PRIVATE Threads::Threads)
add_executable(client ${CLIENT_SRCS})
add_executable(subscriber ${SUBSCRIBER_SRCS})
add_executable(run_protocol_tests ${PROTO_TEST_SRCS})
target_link_libraries(run_protocol_tests PRIVATE Threads::Threads)
add_executable(run_queue_tests ${QUEUE_TEST_SRCS})
target_link_libraries(run_queue_tests PRIVATE Threads::Threads)
# ------------------------------------------------------------------------------
# Tests (equivalent to `make test`)
# ------------------------------------------------------------------------------
enable_testing()
add_test(NAME unit_tests COMMAND run_tests)
add_test(NAME protocol_tests COMMAND run_protocol_tests)
add_test(NAME queue_tests COMMAND run_queue_tests)
# Convenience target matching `make test`
add_custom_target(test_all COMMAND run_tests COMMAND run_protocol_tests COMMAND run_queue_tests DEPENDS run_tests run_protocol_tests run_queue_tests USES_TERMINAL)