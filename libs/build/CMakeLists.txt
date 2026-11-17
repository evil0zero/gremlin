add_library(gremlin_build STATIC src/build.cpp)
gremlin_target(gremlin_build)
target_include_directories(gremlin_build PUBLIC include)
target_link_libraries(gremlin_build PUBLIC gremlin_core PRIVATE gremlin_platform)
