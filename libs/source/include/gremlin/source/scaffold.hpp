#pragma once

#include "gremlin/core/core.hpp"

#include <filesystem>

namespace gremlin {

// Creates a starter app and its CMake/CTest files in a new caller-owned directory.
Result<std::filesystem::path> create_starter_project(
    const std::filesystem::path& source_dir, std::string_view executable_name);

}
