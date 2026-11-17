find_package(Threads REQUIRED)

add_library(gremlin_scheduler STATIC src/scheduler.cpp)
gremlin_target(gremlin_scheduler)
target_include_directories(gremlin_scheduler PUBLIC include)
target_link_libraries(gremlin_scheduler PUBLIC gremlin_core PRIVATE Threads::Threads)

if(BUILD_TESTING)
    add_executable(scheduler_test tests/scheduler_test.cpp)
    gremlin_target(scheduler_test)
    target_link_libraries(scheduler_test PRIVATE gremlin_scheduler)
    add_test(NAME scheduler.dependencies_and_capacity COMMAND scheduler_test)
    set_tests_properties(scheduler.dependencies_and_capacity PROPERTIES TIMEOUT 15)
endif()
