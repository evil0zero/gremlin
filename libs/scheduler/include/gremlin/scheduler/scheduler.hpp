#pragma once

#include "gremlin/core/core.hpp"

#include <functional>
#include <span>
#include <vector>

namespace gremlin {

using TaskResult = Result<std::monostate>;

struct ScheduledTask {
    std::string name;
    std::vector<std::size_t> dependencies;
    std::function<TaskResult()> run;
};

enum class TaskState { pending, running, passed, failed, skipped };

struct TaskOutcome {
    TaskState state{TaskState::pending};
    std::optional<Error> error{};
};

// Dependencies are task indices. Independent tasks may overlap up to slots.
// Only the calling scheduler thread publishes transitions to the event sink.
Result<std::vector<TaskOutcome>> schedule_tasks(std::span<const ScheduledTask> tasks,
    unsigned slots, const JobId& job, unsigned attempt, EventSink& events);

}
