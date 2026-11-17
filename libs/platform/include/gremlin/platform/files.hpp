#pragma once

#include "gremlin/core/core.hpp"

#include <filesystem>
#include <string_view>

namespace gremlin {

// Windows converts UTF-8 to its native path type. POSIX preserves path bytes.
std::filesystem::path path_from_utf8(std::string_view value);

// Publishes a fully written sibling-volume file without replacing a destination.
// The caller owns cleanup of the staged path if it survives the operation.
Result<std::monostate> publish_new_file(
    const std::filesystem::path& staged, const std::filesystem::path& destination);

}
