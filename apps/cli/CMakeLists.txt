add_executable(gremlin main.cpp)
gremlin_target(gremlin)
target_link_libraries(gremlin PRIVATE gremlin_state gremlin_build gremlin_source
    gremlin_inference gremlin_master gremlin_platform)
target_compile_definitions(gremlin PRIVATE GREMLIN_VERSION="${PROJECT_VERSION}")
find_program(GREMLIN_CURL_EXECUTABLE NAMES curl)
if(GREMLIN_CURL_EXECUTABLE)
    target_compile_definitions(gremlin PRIVATE GREMLIN_DEFAULT_CURL="${GREMLIN_CURL_EXECUTABLE}")
endif()
if(WIN32 AND MINGW)
    target_link_options(gremlin PRIVATE -municode)
endif()
if(WIN32)
    target_compile_definitions(gremlin PRIVATE _WIN32_WINNT=0x0600)
endif()

if(BUILD_TESTING)
    add_test(NAME cli.generate_project_arguments COMMAND ${CMAKE_COMMAND}
        "-DEXE=$<TARGET_FILE:gremlin>" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/generate_project.cmake")
    find_package(nlohmann_json 3.11 CONFIG REQUIRED)
    add_executable(cli_project_fake_curl tests/fake_project_curl.cpp)
    gremlin_target(cli_project_fake_curl)
    target_link_libraries(cli_project_fake_curl PRIVATE nlohmann_json::nlohmann_json)
    add_test(NAME cli.generate_project_from_spec COMMAND ${CMAKE_COMMAND}
        "-DEXE=$<TARGET_FILE:gremlin>" "-DCURL=$<TARGET_FILE:cli_project_fake_curl>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/generate_project_from_spec.cmake")
    add_test(NAME cli.renamed_invocation COMMAND ${CMAKE_COMMAND}
        "-DEXE=$<TARGET_FILE:gremlin>" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/renamed.cmake")
    add_test(NAME cli.submit_and_query COMMAND ${CMAKE_COMMAND}
        "-DEXE=$<TARGET_FILE:gremlin>" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/submit.cmake")
    if(CMAKE_GENERATOR STREQUAL "Ninja")
        foreach(mode IN ITEMS out_of_source no_tests scaffold)
            add_test(NAME smoke.gremlin_${mode} COMMAND ${CMAKE_COMMAND}
                "-DEXE=$<TARGET_FILE:gremlin>"
                "-DCMAKE_TOOL=${CMAKE_COMMAND}" "-DCTEST_TOOL=${CMAKE_CTEST_COMMAND}"
                "-DNINJA_TOOL=${CMAKE_MAKE_PROGRAM}" "-DCOMPILER=${CMAKE_CXX_COMPILER}"
                "-DPROFILE=$<IF:$<BOOL:$<CONFIG>>,$<CONFIG>,Debug>"
                "-DMODE=${mode}" "-DFIXTURE=${PROJECT_SOURCE_DIR}/examples/sweet-nothings"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/build_project.cmake")
        endforeach()
    endif()
    if(WIN32)
        add_executable(cli_argv0_test tests/windows_argv0.cpp)
        gremlin_target(cli_argv0_test)
        if(MINGW)
            target_link_options(cli_argv0_test PRIVATE -municode)
        endif()
        add_test(NAME cli.windows_alternate_argv0 COMMAND cli_argv0_test "$<TARGET_FILE:gremlin>")
    endif()
endif()
