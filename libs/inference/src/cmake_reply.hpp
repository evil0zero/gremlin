#pragma once

#include "gremlin/core/core.hpp"

#include <vector>

namespace gremlin::inference {

Result<std::string> decode_cmake_reply(std::string_view response);
Result<std::vector<std::string>> decode_source_reply(std::string_view response);

}
