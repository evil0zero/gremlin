#pragma once

#include "gremlin/core/core.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace gremlin {

struct ProcessRequest {
    // An absolute native binary path. Arguments exclude argv[0].
    // Windows arguments use UTF-8. POSIX arguments use native bytes.
    std::filesystem::path executable;
    std::vector<std::string> arguments;
    // An existing file opened for append. Receives combined stdout/stderr.
    std::filesystem::path log;
    std::chrono::seconds timeout{};
};

struct ProcessExit {
    std::uint32_t code = std::uint32_t();
    bool timed_out = false;
};

// Executes a trusted program with stdin disconnected and no intermediary shell.
Result<ProcessExit> run_process(const ProcessRequest& request);

}
