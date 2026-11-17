string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef suffix)
set(dest "cli-project-${suffix}")
execute_process(COMMAND "${EXE}" generate-project --source "${dest}" --model sample:model --prompt x --spec y
    RESULT_VARIABLE code ERROR_VARIABLE error)
if(NOT code EQUAL 2 OR EXISTS "${dest}")
    message(FATAL_ERROR "Conflicting prompts accepted: ${code} ${error}")
endif()
file(MAKE_DIRECTORY "${dest}")
file(WRITE "${dest}/keep.txt" "original")
execute_process(COMMAND "${EXE}" generate-project --source "${dest}" --model sample:model --prompt x
    RESULT_VARIABLE code ERROR_VARIABLE error)
file(READ "${dest}/keep.txt" contents)
if(NOT code EQUAL 2 OR NOT error MATCHES "destination already exists" OR NOT contents STREQUAL "original")
    message(FATAL_ERROR "Expected destination collision: ${code} ${error}")
endif()

set(new_dest "${dest}-absent")
set(spec "${dest}-spec.md")
foreach(kind IN ITEMS missing empty oversized)
    if(kind STREQUAL "empty")
        file(WRITE "${spec}" "")
    elseif(kind STREQUAL "oversized")
        string(REPEAT "x" 32769 large_prompt)
        file(WRITE "${spec}" "${large_prompt}")
    endif()
    execute_process(COMMAND "${EXE}" generate-project --source "${new_dest}"
        --spec "${spec}" --model sample:model --curl "missing-provider"
        RESULT_VARIABLE code ERROR_VARIABLE error)
    if(NOT code EQUAL 2 OR EXISTS "${new_dest}" OR NOT error MATCHES "${spec}")
        message(FATAL_ERROR "Expected ${kind} specification rejection: ${code} ${error}")
    endif()
    if(kind STREQUAL "oversized" AND (NOT error MATCHES "32769" OR NOT error MATCHES "32768"))
        message(FATAL_ERROR "Oversized diagnostic omitted actual size or limit: ${error}")
    endif()
endforeach()

foreach(tokens IN ITEMS 0 127 65537 -1 17s 1.5 +2 999999999999999999999999999999)
    execute_process(COMMAND "${EXE}" generate-project --source "${new_dest}"
        --prompt "A specification" --model sample:model --curl missing-provider
        --max-output-tokens "${tokens}" RESULT_VARIABLE code ERROR_VARIABLE error)
    if(NOT code EQUAL 2 OR EXISTS "${new_dest}" OR NOT error MATCHES "--max-output-tokens")
        message(FATAL_ERROR "Invalid output token budget ${tokens} returned ${code}: ${error}")
    endif()
endforeach()

foreach(command IN ITEMS generate-project generate-cmake)
    foreach(timeout IN ITEMS 0 -1 7201 17s 1.5 +2 999999999999999999999999999999)
        execute_process(COMMAND "${EXE}" "${command}" --source "${new_dest}"
            --prompt "A specification" --model sample:model --curl missing-provider
            --timeout-seconds "${timeout}" RESULT_VARIABLE code ERROR_VARIABLE error)
        if(NOT code EQUAL 2 OR EXISTS "${new_dest}" OR NOT error MATCHES "--timeout-seconds")
            message(FATAL_ERROR "Invalid ${command} timeout ${timeout} returned ${code}: ${error}")
        endif()
    endforeach()
endforeach()
