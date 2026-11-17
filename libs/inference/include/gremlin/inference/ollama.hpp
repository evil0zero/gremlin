#pragma once

#include "gremlin/core/core.hpp"
#include "gremlin/core/project_file.hpp"

#include <filesystem>
#include <vector>

namespace gremlin {

struct CMakeInferenceRequest {
    std::string model;
    std::string prompt;
    std::vector<std::string> source_files;
    std::filesystem::path curl;
    std::filesystem::path log_dir;
};

inline constexpr std::size_t cmake_prompt_limit = 8192;

// Requests a file proposal from Ollama at 127.0.0.1:11434.
// The result is untrusted text and carries no permission to execute it.
Result<std::string> infer_cmake(const CMakeInferenceRequest& request);
Result<std::vector<ProjectFile>> infer_project(const CMakeInferenceRequest& request);

struct SourceRepairRequest {
    std::string model;
    std::string prompt;
    std::vector<std::string> available_sources;
    std::vector<std::string> attempted_sources;
    std::string diagnostic;
    std::filesystem::path curl;
    std::filesystem::path log_dir;
};

// Proposes the complete replacement list of translation units.
Result<std::vector<std::string>> infer_sources(const SourceRepairRequest& request);

}
