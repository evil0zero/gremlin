add_library(gremlin_master STATIC src/master.cpp src/project.cpp)
gremlin_target(gremlin_master)
target_include_directories(gremlin_master PUBLIC include)
target_link_libraries(gremlin_master PUBLIC gremlin_build gremlin_inference
    PRIVATE gremlin_logging gremlin_state gremlin_scheduler)

if(BUILD_TESTING AND CMAKE_GENERATOR STREQUAL "Ninja")
    add_executable(master_test tests/master_test.cpp)
    gremlin_target(master_test)
    target_link_libraries(master_test PRIVATE gremlin_master gremlin_state gremlin_platform)
    foreach(mode IN ITEMS repair repeat reject test_failure)
        add_test(NAME master.${mode} COMMAND master_test "${mode}" "${PROJECT_SOURCE_DIR}/examples/line-counter"
            "${CMAKE_COMMAND}" "${CMAKE_CTEST_COMMAND}" "${CMAKE_MAKE_PROGRAM}" "${CMAKE_CXX_COMPILER}"
            "$<IF:$<BOOL:$<CONFIG>>,$<CONFIG>,Debug>")
        set_tests_properties(master.${mode} PROPERTIES TIMEOUT 60)
    endforeach()
endif()
