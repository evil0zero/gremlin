#pragma once

#include "gremlin/core/core.hpp"

#include <filesystem>
#include <memory>

struct sqlite3;

namespace gremlin {

class JobStore {
public:
    using Status = Result<std::monostate>;
    using Handle = std::unique_ptr<JobStore>;
    ~JobStore();
    JobStore(const JobStore&) = delete;
    JobStore& operator=(const JobStore&) = delete;

    static Result<Handle> open(const std::filesystem::path& path);
    Status submit(const JobId& id, std::string_view original);
    Result<JobState> state(const JobId& id);
    Status transition(const JobId& id, JobState expected, JobState next);

    // Caller must already hold exclusive master ownership.
    Result<int> recover_interrupted();

private:
    explicit JobStore(sqlite3* db) : db_(db) {}
    sqlite3* db_;
};

}
