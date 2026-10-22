add_executable(itch_replay itch_replay.cpp)
target_link_libraries(
    itch_replay
    PRIVATE
        nasdaq::itch_market_adapter
)
matching_enable_warnings(itch_replay)
matching_enable_sanitizers(itch_replay)

add_executable(matching_cli matching_cli.cpp)
target_link_libraries(matching_cli PRIVATE matching::engine)
matching_enable_warnings(matching_cli)
matching_enable_sanitizers(matching_cli)
