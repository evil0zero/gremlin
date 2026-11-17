#include "gremlin/scheduler/scheduler.hpp"

#include <algorithm>
#include <future>
#include <set>

using namespace std;

namespace gremlin {
namespace {

TaskResult validate_graph(span<const ScheduledTask> tasks, unsigned slots) {
    if (tasks.empty() || tasks.size() > 256 || slots == 0 || slots > 32) {
        return Error{ErrorCode::input, "scheduler requires 1-256 tasks and 1-32 slots"};
    }
    set<string_view> names;
    vector<vector<size_t>> dependents(tasks.size());
    vector<size_t> remaining(tasks.size());
    vector<size_t> ready;
    for (size_t i = size_t(); i < tasks.size(); ++i) {
        const auto& task = tasks[i];
        if (!task.run || task.name.empty() || !names.insert(task.name).second) {
            return Error{ErrorCode::input, "scheduler task names and worker functions must be valid and unique"};
        }
        set<size_t> unique;
        for (const auto dependency : task.dependencies) {
            if (dependency >= tasks.size() || dependency == i || !unique.insert(dependency).second) {
                return Error{ErrorCode::input, "scheduler graph has an invalid dependency"};
            }
            dependents[dependency].push_back(i);
        }
        remaining[i] = task.dependencies.size();
        if (!remaining[i]) ready.push_back(i);
    }
    for (size_t i = size_t(); i < ready.size(); ++i) {
        for (const auto child : dependents[ready[i]]) {
            if (--remaining[child] == 0) ready.push_back(child);
        }
    }
    if (ready.size() != tasks.size()) return Error{ErrorCode::input, "scheduler graph contains a cycle"};
    return monostate{};
}

TaskResult run_worker(const ScheduledTask& task) {
    try {
        return task.run();
    } catch (const exception& error) {
        return Error{ErrorCode::internal, string("worker failed: ") + error.what()};
    } catch (...) {
        return Error{ErrorCode::internal, "worker failed with an unknown exception"};
    }
}

struct ActiveTask {
    size_t index = size_t();
    chrono::steady_clock::time_point start;
    future<TaskResult> result;
};

}

Result<vector<TaskOutcome>> schedule_tasks(span<const ScheduledTask> tasks,
        unsigned slots, const JobId& job, unsigned attempt, EventSink& events) {
    const auto valid = validate_graph(tasks, slots);
    if (const auto* error = get_if<Error>(&valid)) return *error;

    vector<TaskOutcome> outcomes(tasks.size());
    vector<string> correlations;
    for (size_t i = size_t(); i < tasks.size(); ++i) correlations.push_back(JobId::generate().value);
    auto record = [&](size_t index, const char* action,
                      optional<chrono::milliseconds> elapsed = {}) {
        Event event{job, "scheduler", action, correlations[index]};
        event.task = tasks[index].name;
        event.attempt = attempt;
        event.duration = elapsed;
        if (outcomes[index].error) event.error = outcomes[index].error->code;
        return events.emit(event);
    };
    for (size_t i = size_t(); i < tasks.size(); ++i) {
        const auto logged = record(i, "queued");
        if (const auto* error = get_if<Error>(&logged)) return *error;
    }

    vector<ActiveTask> active;
    active.reserve(slots);
    size_t terminal = size_t();
    while (terminal < tasks.size()) {
        bool advanced = false;
        for (auto it = active.begin(); it != active.end();) {
            if (it->result.wait_for(chrono::seconds(0)) != future_status::ready) {
                ++it;
                continue;
            }
            const auto result = it->result.get();
            const auto index = it->index;
            const auto elapsed = chrono::duration_cast<chrono::milliseconds>(
                chrono::steady_clock::now() - it->start);
            auto& outcome = outcomes[index];
            outcome.state = TaskState::passed;
            if (const auto* error = get_if<Error>(&result)) {
                outcome.state = TaskState::failed;
                outcome.error = *error;
            }
            it = active.erase(it);
            ++terminal;
            advanced = true;
            const auto logged = record(index, outcome.error ? "failed" : "passed", elapsed);
            if (const auto* error = get_if<Error>(&logged)) return *error;
        }
        for (size_t i = size_t(); i < tasks.size(); ++i) {
            auto& outcome = outcomes[i];
            if (outcome.state != TaskState::pending) continue;
            const auto& deps = tasks[i].dependencies;
            if (any_of(deps.begin(), deps.end(), [&](size_t index) {
                    return outcomes[index].state == TaskState::failed || outcomes[index].state == TaskState::skipped;
                })) {
                outcome.state = TaskState::skipped;
                ++terminal;
                advanced = true;
                const auto logged = record(i, "skipped");
                if (const auto* error = get_if<Error>(&logged)) return *error;
            } else if (active.size() < slots && all_of(deps.begin(), deps.end(), [&](size_t index) {
                    return outcomes[index].state == TaskState::passed;
                })) {
                const auto logged = record(i, "started");
                if (const auto* error = get_if<Error>(&logged)) return *error;
                outcome.state = TaskState::running;
                try {
                    active.push_back({i, chrono::steady_clock::now(),
                        async(launch::async, [task = &tasks[i]] { return run_worker(*task); })});
                } catch (const system_error& error) {
                    return Error{ErrorCode::internal, string("cannot start worker: ") + error.what()};
                }
                advanced = true;
            }
        }
        if (!advanced && !active.empty()) active.front().result.wait_for(chrono::milliseconds(5));
    }
    return outcomes;
}

}
