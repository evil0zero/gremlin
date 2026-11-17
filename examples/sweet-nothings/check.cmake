execute_process(COMMAND "${APP}" RESULT_VARIABLE code OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT code STREQUAL "0" OR NOT output STREQUAL "sweet nothings\n" OR NOT error STREQUAL "")
    message(FATAL_ERROR "Unexpected exit or output (${code}): ${output}${error}")
endif()
