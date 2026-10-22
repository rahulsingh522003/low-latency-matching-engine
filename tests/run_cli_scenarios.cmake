if(NOT DEFINED MATCHING_CLI OR NOT DEFINED SCENARIO_DIRECTORY)
    message(FATAL_ERROR "MATCHING_CLI and SCENARIO_DIRECTORY are required")
endif()

foreach(number RANGE 1 15)
    if(number LESS 10)
        set(suffix "0${number}")
    else()
        set(suffix "${number}")
    endif()
    set(scenario "${SCENARIO_DIRECTORY}/scenario-${suffix}.txt")

    execute_process(
        COMMAND "${MATCHING_CLI}"
        INPUT_FILE "${scenario}"
        OUTPUT_VARIABLE scenario_output
        ERROR_VARIABLE scenario_error
        RESULT_VARIABLE scenario_result
    )

    if(NOT scenario_result EQUAL 0)
        message(FATAL_ERROR
                "scenario-${suffix} returned ${scenario_result}: ${scenario_error}")
    endif()
    if(scenario_error MATCHES "ERROR")
        message(FATAL_ERROR "scenario-${suffix} failed: ${scenario_error}")
    endif()
endforeach()
