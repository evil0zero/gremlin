cmake_minimum_required(VERSION 3.21)
project(SweetNothings LANGUAGES CXX)

add_executable(sweet_nothings main.cpp)
target_compile_features(sweet_nothings PRIVATE cxx_std_20)

include(CTest)
if(BUILD_TESTING)
    add_test(NAME sweet_nothings.output COMMAND ${CMAKE_COMMAND}
        "-DAPP=$<TARGET_FILE:sweet_nothings>" -P "${CMAKE_CURRENT_SOURCE_DIR}/check.cmake")
endif()
