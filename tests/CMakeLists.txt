add_executable(
    matching_tests
    foundation_smoke_test.cpp
    hash_map_test.cpp
    itch_decoder_test.cpp
    itch_fixture_test.cpp
    itch_frame_decoder_test.cpp
    itch_market_adapter_test.cpp
    intrusive_avl_tree_test.cpp
    level_test.cpp
    event_handler_test.cpp
    market_manager_test.cpp
    matching_engine_test.cpp
    object_pool_test.cpp
    order_test.cpp
    order_book_test.cpp
    order_lifecycle_test.cpp
)

target_link_libraries(
    matching_tests
    PRIVATE
        nasdaq::itch_market_adapter
        Catch2::Catch2
)
matching_enable_warnings(matching_tests)
matching_enable_sanitizers(matching_tests)

add_test(NAME matching.foundation COMMAND matching_tests "[foundation]")
add_test(NAME matching.hash_map COMMAND matching_tests "[hash_map]")
add_test(NAME matching.itch_decoder COMMAND matching_tests "[itch_decoder]")
add_test(NAME matching.itch_fixture COMMAND matching_tests "[itch_fixture]")
add_test(NAME matching.itch_frame COMMAND matching_tests "[itch_frame]")
add_test(NAME matching.itch_adapter COMMAND matching_tests "[itch_adapter]")
add_test(NAME matching.object_pool COMMAND matching_tests "[object_pool]")
add_test(NAME matching.order COMMAND matching_tests "[order]")
add_test(NAME matching.intrusive_avl_tree COMMAND matching_tests "[avl]")
add_test(NAME matching.level COMMAND matching_tests "[level]")
add_test(NAME matching.event_handler COMMAND matching_tests "[event_handler]")
add_test(NAME matching.market_manager COMMAND matching_tests "[market_manager]")
add_test(NAME matching.engine COMMAND matching_tests "[matching]")
add_test(NAME matching.order_book COMMAND matching_tests "[order_book]")
add_test(NAME matching.order_lifecycle COMMAND matching_tests "[order_lifecycle]")

if(TARGET matching_cli)
    add_test(
        NAME matching.example_cli
        COMMAND
            "${CMAKE_COMMAND}"
            "-DMATCHING_CLI=$<TARGET_FILE:matching_cli>"
            "-DCLI_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/data/cli_smoke.txt"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/run_cli_smoke.cmake"
    )
    add_test(
        NAME matching.example_scenarios
        COMMAND
            "${CMAKE_COMMAND}"
            "-DMATCHING_CLI=$<TARGET_FILE:matching_cli>"
            "-DSCENARIO_DIRECTORY=${PROJECT_SOURCE_DIR}/examples/scenarios"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/run_cli_scenarios.cmake"
    )
endif()

if(TARGET itch_replay AND MATCHING_ITCH_SAMPLE_FILE)
    if(NOT EXISTS "${MATCHING_ITCH_SAMPLE_FILE}")
        message(FATAL_ERROR
                "MATCHING_ITCH_SAMPLE_FILE does not exist: ${MATCHING_ITCH_SAMPLE_FILE}")
    endif()
    add_test(
        NAME matching.itch_sample_replay
        COMMAND
            "${CMAKE_COMMAND}"
            "-DITCH_REPLAY=$<TARGET_FILE:itch_replay>"
            "-DITCH_SAMPLE=${MATCHING_ITCH_SAMPLE_FILE}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/run_itch_sample_replay.cmake"
    )
endif()
