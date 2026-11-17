#include "gremlin/scheduler/scheduler.hpp"

#include <atomic>
#include <thread>

using namespace std;

namespace {

struct RecordingSink : gremlin::EventSink {
    vector<gremlin::Event> records;
    const thread::id master = this_thread::get_id();
    gremlin::TaskResult emit(const gremlin::Event& event) override {
        if (this_thread::get_id() != master) {
            return gremlin::Error{gremlin::ErrorCode::internal, "worker attempted to emit an event"};
        }
        records.push_back(event);
        return monostate{};
    }
};

}

int main() {
    using namespace gremlin;
    using namespace chrono_literals;
    const auto id = JobId::generate();
    RecordingSink log;
    atomic<unsigned> arrived{};
    bool ran_dependent = false;
    auto parallel = [&]() -> TaskResult {
        ++arrived;
        const auto deadline = chrono::steady_clock::now() + 2s;
        while (arrived.load() != 2 && chrono::steady_clock::now() < deadline) {
            this_thread::sleep_for(1ms);
        }
        if (arrived.load() != 2) return Error{ErrorCode::internal, "independent tasks did not overlap"};
        return monostate{};
    };
    const vector<ScheduledTask> batch{
        {"check", {1, 2}, [&]() -> TaskResult {
            ran_dependent = true;
            return Error{ErrorCode::test, "deliberate test failure"};
        }},
        {"first", {}, parallel}, {"second", {}, parallel},
        {"publish", {0}, []() -> TaskResult { return Error{ErrorCode::internal, "must be skipped"}; }}
    };
    const auto result = schedule_tasks(batch, 2, id, 1, log);
    if (result.index() || !ran_dependent) return 1;
    const auto& outcomes = get<vector<TaskOutcome>>(result);
    if (outcomes[0].state != TaskState::failed || outcomes[1].state != TaskState::passed ||
        outcomes[2].state != TaskState::passed || outcomes[3].state != TaskState::skipped) return 2;
    if (log.records.size() != 11 || log.records.back().action != "skipped" ||
        log.records.back().task != "publish" || log.records.back().attempt != 1) return 3;

    auto cycle = batch;
    cycle[1].dependencies = {3};
    if (!schedule_tasks(cycle, 2, id, 1, log).index() || log.records.size() != 11) return 4;
}
