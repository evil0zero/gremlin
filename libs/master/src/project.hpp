#pragma once

#include "gremlin/master/master.hpp"

namespace gremlin::master {

struct ProjectSnapshot {
    std::filesystem::path directory;
    std::vector<std::string> sources;
};

Result<ProjectSnapshot> snapshot_project(const RepairRequest& request, const std::filesystem::path& directory);
Result<std::monostate> stage_project(const ProjectSnapshot& snapshot, const std::vector<std::string>& selection,
    const std::filesystem::path& destination);
bool valid_selection(const std::vector<std::string>& sorted_selection, const ProjectSnapshot& snapshot);

}
