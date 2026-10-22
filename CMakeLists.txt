cmake_minimum_required(VERSION 3.20)

project(MatchingEngine VERSION 0.1.0 LANGUAGES CXX)

option(MATCHING_WARNINGS_AS_ERRORS "Treat project warnings as errors" ON)
option(MATCHING_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(MATCHING_BUILD_FUZZERS "Build sanitizer-backed fuzz targets" OFF)
option(MATCHING_BUILD_EXAMPLES "Build runnable project examples" ON)
option(MATCHING_BUILD_BENCHMARKS "Build reproducible performance benchmarks" ON)
set(MATCHING_ITCH_SAMPLE_FILE "" CACHE FILEPATH
    "Optional external ITCH sample used by the replay integration test")

set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Sanitizers)

add_library(
    matching_engine
    STATIC
        source/matching/order.cpp
        source/matching/order_book.cpp
        source/matching/market_manager.cpp
)
add_library(matching::engine ALIAS matching_engine)
target_compile_features(matching_engine PUBLIC cxx_std_20)
target_include_directories(
    matching_engine
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
)
matching_enable_warnings(matching_engine)
matching_enable_sanitizers(matching_engine)

add_library(
    nasdaq_itch
    STATIC
        source/providers/nasdaq/itch/decoder.cpp
)
add_library(nasdaq::itch ALIAS nasdaq_itch)
target_compile_features(nasdaq_itch PUBLIC cxx_std_20)
target_include_directories(
    nasdaq_itch
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
)
matching_enable_warnings(nasdaq_itch)
matching_enable_sanitizers(nasdaq_itch)

add_library(
    nasdaq_itch_market_adapter
    STATIC
        source/providers/nasdaq/itch/market_adapter.cpp
)
add_library(nasdaq::itch_market_adapter ALIAS nasdaq_itch_market_adapter)
target_compile_features(nasdaq_itch_market_adapter PUBLIC cxx_std_20)
target_include_directories(
    nasdaq_itch_market_adapter
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
)
target_link_libraries(
    nasdaq_itch_market_adapter
    PUBLIC
        matching::engine
        nasdaq::itch
)
matching_enable_warnings(nasdaq_itch_market_adapter)
matching_enable_sanitizers(nasdaq_itch_market_adapter)

include(CTest)

if(MATCHING_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(MATCHING_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

if(MATCHING_BUILD_FUZZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "^(AppleClang|Clang|GNU)$")
        message(FATAL_ERROR "MATCHING_BUILD_FUZZERS requires Clang or GCC")
    endif()

    target_compile_options(
        nasdaq_itch
        PRIVATE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
            -fno-sanitize-recover=all
    )
    target_link_options(
        nasdaq_itch
        INTERFACE
            -fsanitize=address,undefined
    )

    add_executable(
        itch_decoder_fuzz
        fuzz/itch_decoder_fuzz.cpp
        fuzz/itch_decoder_fuzz_main.cpp
    )
    target_compile_features(itch_decoder_fuzz PRIVATE cxx_std_20)
    target_link_libraries(itch_decoder_fuzz PRIVATE nasdaq::itch)
    target_compile_options(
        itch_decoder_fuzz
        PRIVATE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
            -fno-sanitize-recover=all
    )
    target_link_options(
        itch_decoder_fuzz
        PRIVATE
            -fsanitize=address,undefined
    )
    matching_enable_warnings(itch_decoder_fuzz)
endif()

if(BUILD_TESTING)
    add_subdirectory(third_party/Catch2)
    add_subdirectory(tests)
endif()
