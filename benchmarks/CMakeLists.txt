add_executable(matching_latency_benchmark matching_latency_benchmark.cpp)
target_link_libraries(matching_latency_benchmark PRIVATE matching::engine)
matching_enable_warnings(matching_latency_benchmark)
matching_enable_sanitizers(matching_latency_benchmark)

add_executable(itch_pipeline_benchmark itch_pipeline_benchmark.cpp)
target_link_libraries(
    itch_pipeline_benchmark
    PRIVATE
        nasdaq::itch_market_adapter
)
matching_enable_warnings(itch_pipeline_benchmark)
matching_enable_sanitizers(itch_pipeline_benchmark)

add_executable(structure_benchmark structure_benchmark.cpp)
target_link_libraries(structure_benchmark PRIVATE matching::engine)
matching_enable_warnings(structure_benchmark)
matching_enable_sanitizers(structure_benchmark)

add_executable(layout_report layout_report.cpp)
target_link_libraries(layout_report PRIVATE matching::engine)
matching_enable_warnings(layout_report)
matching_enable_sanitizers(layout_report)

if(BUILD_TESTING)
    add_test(
        NAME matching.benchmark_latency_smoke
        COMMAND
            matching_latency_benchmark
            --samples 32
            --warmup 8
            --batch-size 4
            --repetitions 1
            --output "${CMAKE_CURRENT_BINARY_DIR}/latency-smoke"
    )
    add_test(
        NAME matching.benchmark_structure_smoke
        COMMAND
            structure_benchmark
            --operations 128
            --elements 32
            --repetitions 1
            --output "${CMAKE_CURRENT_BINARY_DIR}/structure-smoke"
    )
endif()
