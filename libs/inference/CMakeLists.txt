find_package(nlohmann_json 3.11 CONFIG REQUIRED)

add_library(gremlin_inference STATIC src/ollama.cpp src/cmake_reply.cpp)
gremlin_target(gremlin_inference)
target_include_directories(gremlin_inference PUBLIC include)
target_link_libraries(gremlin_inference PUBLIC gremlin_core
    PRIVATE gremlin_platform nlohmann_json::nlohmann_json)

if(BUILD_TESTING)
    add_executable(inference_test tests/cmake_reply_test.cpp)
    gremlin_target(inference_test)
    target_include_directories(inference_test PRIVATE src)
    target_link_libraries(inference_test PRIVATE gremlin_inference)
    add_test(NAME inference.cmake_reply_validation COMMAND inference_test)
endif()
