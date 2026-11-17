#pragma once

#include "gremlin/build/build.hpp"
#include "gremlin/inference/ollama.hpp"

#include <functional>

namespace gremlin {

struct RepairRequest {
    BuildRequest build;
    std::filesystem::path checker;
    std::filesystem::path runs_dir;
    std::filesystem::path curl;
    std::string prompt;
    std::string model;
    std::vector<std::string> initial_sources;
    unsigned max_repairs{2};
};

struct RepairRun {
    JobId job;
    JobState state{JobState::pending};
    std::filesystem::path directory;
    unsigned attempts = unsigned();
    std::optional<BuildEvidence> evidence{};
    std::filesystem::path executable{};
    std::optional<Error> failure{};
};

using RepairProvider = std::function<Result<std::vector<std::string>>(const SourceRepairRequest&)>;

// Runs an authorized local project with an immutable checker. Model replies
// can select inventoried translation units; they cannot write executable code.
Result<RepairRun> run_repair(const RepairRequest& request, RepairProvider provider = infer_sources);

}
