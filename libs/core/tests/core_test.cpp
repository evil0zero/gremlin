#include "gremlin/core/core.hpp"

using namespace std;

int main() {
    using namespace gremlin;
    const auto id = JobId::generate();
    if (!holds_alternative<JobId>(JobId::parse(id.value))) return 1;
    if (!holds_alternative<Error>(JobId::parse("../job"))) return 2;
    if (!can_transition(JobState::running, JobState::uncertain)) return 3;
    if (can_transition(JobState::uncertain, JobState::running)) return 4;
    if (can_transition(JobState::pending, JobState::verified)) return 5;
    if (to_string(JobState::verified) != "VERIFIED") return 6;
    if (get<Profile>(parse_profile("RelWithDebInfo")) != Profile::rel_with_deb_info) return 7;
    if (!holds_alternative<Error>(parse_profile("release"))) return 8;
    Limits limits;
    if (!holds_alternative<Limits>(limits.validate())) return 9;
    limits.inference_workers = 2;
    if (!holds_alternative<Error>(limits.validate())) return 10;
}
