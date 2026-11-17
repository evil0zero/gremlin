#pragma once

#include "gremlin/core/core.hpp"

#include <filesystem>

namespace gremlin {

struct BuildRequest {
    std::filesystem::path source_dir;
    std::filesystem::path build_dir;
    std::filesystem::path cmake;
    std::filesystem::path ctest;
    std::filesystem::path ninja;
    std::filesystem::path compiler;
    Profile profile{Profile::debug};
    bool trusted_source{false};
};

struct BuildEvidence {
    std::filesystem::path build_dir;
    std::filesystem::path log_dir;
    std::filesystem::path junit;
};

class BuildSession {
public:
    const BuildEvidence& evidence() const noexcept { return evidence_; }
    Result<std::monostate> configure() const;
    Result<std::monostate> compile() const;
    Result<std::monostate> test() const;

private:
    friend Result<BuildSession> prepare_build(const BuildRequest&);
    BuildSession(BuildRequest request, BuildEvidence evidence, const char* config);

    BuildRequest request_;
    BuildEvidence evidence_;
    const char* config_;
};

// Reserves a fresh build tree and prepares stages for an external scheduler.
Result<BuildSession> prepare_build(const BuildRequest& request);

// Configures, builds and tests an authorized local CMake project in a fresh
// Ninja tree. The source and build directories must be disjoint.
Result<BuildEvidence> build_and_test(const BuildRequest& request);

}
