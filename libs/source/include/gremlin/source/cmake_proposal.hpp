#pragma once

#include "gremlin/core/core.hpp"

#include <filesystem>
#include <vector>

namespace gremlin {

// Inventories top-level C++ implementation names, without reading their contents.
Result<std::vector<std::string>> cmake_inputs(const std::filesystem::path& source_dir);

// Publishes the proposal only if CMakeLists.txt is still absent.
Result<std::filesystem::path> publish_cmake_proposal(
    const std::filesystem::path& source_dir, std::string_view contents);

}
