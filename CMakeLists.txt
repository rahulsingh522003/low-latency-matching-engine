cmake_minimum_required(VERSION 3.16)

project(limit_order_book LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(order_book
    src/order_book.cpp
)

target_include_directories(order_book
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(order_book
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

add_executable(test_order_book
    tests/test_order_book.cpp
)

target_compile_options(test_order_book
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

target_link_libraries(test_order_book
    PRIVATE
        order_book
)

add_executable(test_order_book_verbose
    tests/test_order_book_verbose.cpp
)

target_compile_options(test_order_book_verbose
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

target_link_libraries(test_order_book_verbose
    PRIVATE
        order_book
)

add_executable(order_book_demo
    examples/demo.cpp
)

target_compile_options(order_book_demo
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

target_link_libraries(order_book_demo
    PRIVATE
        order_book
)

add_executable(order_book_interactive
    examples/interactive_demo.cpp
)

target_compile_options(order_book_interactive
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

target_link_libraries(order_book_interactive
    PRIVATE
        order_book
)

add_executable(order_book_benchmark
    benchmarks/benchmark.cpp
)

target_compile_options(order_book_benchmark
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

target_link_libraries(order_book_benchmark
    PRIVATE
        order_book
)

add_executable(random
    examples/random.cpp
)

target_compile_options(random
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

target_link_libraries(random
    PRIVATE
        order_book
)