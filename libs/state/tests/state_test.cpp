#include "gremlin/state/jobs.hpp"

using namespace std;

int main() {
    using namespace gremlin;
    const auto id = JobId::generate();
    const auto path = filesystem::path("job-test-" + id.value + ".sqlite");
    {
        auto first = JobStore::open(path);
        auto second = JobStore::open(path);
        if (!holds_alternative<JobStore::Handle>(first) ||
            !holds_alternative<JobStore::Handle>(second)) return 1;
        auto& db = *get<JobStore::Handle>(first);
        auto& other = *get<JobStore::Handle>(second);
        if (db.submit(id, "request").index() || other.submit(id, "request").index()) return 2;
        if (!other.submit(id, "different").index()) return 3;
        if (db.transition(id, JobState::pending, JobState::running).index()) return 4;
        if (!other.transition(id, JobState::pending, JobState::cancelled).index()) return 5;
    }
    {
        auto reopened = JobStore::open(path);
        if (reopened.index()) return 6;
        auto& db = *get<JobStore::Handle>(reopened);
        if (get<JobState>(db.state(id)) != JobState::running) return 7;
        if (get<int>(db.recover_interrupted()) != 1) return 8;
        if (get<int>(db.recover_interrupted()) != 0) return 9;
        if (get<JobState>(db.state(id)) != JobState::uncertain) return 10;
    }
    return filesystem::remove(path) ? 0 : 11;
}
