if(NOT DEFINED ITCH_REPLAY OR NOT DEFINED ITCH_SAMPLE)
    message(FATAL_ERROR "ITCH_REPLAY and ITCH_SAMPLE are required")
endif()

execute_process(
    COMMAND "${ITCH_REPLAY}" "${ITCH_SAMPLE}" --round-cent --chunk-size 8192
    OUTPUT_VARIABLE replay_output
    ERROR_VARIABLE replay_error
    RESULT_VARIABLE replay_result
)

if(NOT replay_result EQUAL 0)
    message(FATAL_ERROR
            "Sample replay returned ${replay_result}: ${replay_error}")
endif()

foreach(expected
        "Bytes: 73200452"
        "Messages: 1563071"
        "Unknown messages: 0"
        "Maximum symbols: 8352"
        "Maximum books: 8352"
        "Maximum active orders: 56245"
        "Maximum displayed levels/side: 562"
        "58915/27/58915/2435"
        "Final symbols/books/levels/orders: 8352/8352/0/0"
        "Event hash: 0x517a49e4d0db4d1d"
        "State hash: 0xb49d1f3fa4ad25ab")
    if(NOT replay_output MATCHES "${expected}")
        message(FATAL_ERROR "Missing expected replay result: ${expected}")
    endif()
endforeach()
