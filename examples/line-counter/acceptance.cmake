file(WRITE "${WORK_DIR}/mixed-lines.txt" "first\r\nsecond\nlast")
file(WRITE "${WORK_DIR}/empty.txt" "")

function(check_count input expected)
    execute_process(COMMAND "${APP}" "${WORK_DIR}/${input}"
        RESULT_VARIABLE code OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT code STREQUAL "0" OR NOT output STREQUAL "${expected}\n" OR NOT error STREQUAL "")
        message(FATAL_ERROR "Unexpected result for ${input} (${code}): ${output}${error}")
    endif()
endfunction()

check_count("mixed-lines.txt" 3)
check_count("empty.txt" 0)
execute_process(COMMAND "${APP}" "${WORK_DIR}/intentionally-missing.txt"
    RESULT_VARIABLE code OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT code STREQUAL "3" OR NOT output STREQUAL "" OR NOT error STREQUAL "Could not open input\n")
    message(FATAL_ERROR "Unexpected missing-file result (${code}): ${output}${error}")
endif()
