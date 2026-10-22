if(NOT DEFINED MATCHING_CLI OR NOT DEFINED CLI_INPUT)
    message(FATAL_ERROR "MATCHING_CLI and CLI_INPUT are required")
endif()

execute_process(
    COMMAND "${MATCHING_CLI}"
    INPUT_FILE "${CLI_INPUT}"
    OUTPUT_VARIABLE cli_output
    ERROR_VARIABLE cli_error
    RESULT_VARIABLE cli_result
)

if(NOT cli_result EQUAL 0)
    message(FATAL_ERROR "matching_cli returned ${cli_result}: ${cli_error}")
endif()

if(cli_error MATCHES "ERROR")
    message(FATAL_ERROR "matching_cli reported an error: ${cli_error}")
endif()

if(NOT cli_output MATCHES
   "event execute-order id=1 symbol=7 price=18530 quantity=6 leaves=4")
    message(FATAL_ERROR "Expected passive execution was not reported")
endif()

if(NOT cli_output MATCHES "AskLevels=1")
    message(FATAL_ERROR "Expected final ask level was not displayed")
endif()

if(NOT cli_output MATCHES
   "id=1 symbol=7 type=1 side=sell tif=0 price=18530 stop=0 quantity=4")
    message(FATAL_ERROR "Expected four-share resting order was not displayed")
endif()
