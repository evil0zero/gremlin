#pragma once
#include "gremlin/core/core.hpp"
#include "gremlin/core/project_file.hpp"
#include <filesystem>
#include <span>

namespace gremlin {
Result<std::filesystem::path> publish_project(const std::filesystem::path& destination,
                                             std::span<const ProjectFile> files);
}
