#include "gremlin/core/core.hpp"

#include <algorithm>
#include <random>

using namespace std;

namespace gremlin {

JobId JobId::generate() {
    random_device random;
    uniform_int_distribution<unsigned> hex(0, 15);
    JobId id{string(32, '0')};
    for (char& digit : id.value) {
        digit = "0123456789abcdef"[hex(random)];
    }
    return id;
}

Result<JobId> JobId::parse(string_view value) {
    if (value.size() != 32 || !all_of(value.begin(), value.end(), [](char c) {
        return string_view("0123456789abcdef").find(c) != string_view::npos;
    })) {
        return Error{ErrorCode::input, "expected a 32-digit lowercase hexadecimal job ID"};
    }
    return JobId{string(value)};
}

string_view to_string(JobState state) noexcept {
    switch (state) {
        case JobState::pending: return "PENDING";
        case JobState::running: return "RUNNING";
        case JobState::verified: return "VERIFIED";
        case JobState::failed: return "FAILED";
        case JobState::cancelled: return "CANCELLED";
        case JobState::blocked: return "BLOCKED";
        case JobState::uncertain: return "UNCERTAIN";
    }
    return {};
}

bool can_transition(JobState from, JobState to) noexcept {
    switch (from) {
        case JobState::pending:
            return to == JobState::running || to == JobState::blocked || to == JobState::cancelled;
        case JobState::running:
            return to == JobState::verified || to == JobState::failed || to == JobState::cancelled
                || to == JobState::blocked || to == JobState::uncertain;
        default:
            return false;
    }
}

Result<Profile> parse_profile(string_view value) {
    if (value == "Debug") return Profile::debug;
    if (value == "Release") return Profile::release;
    if (value == "RelWithDebInfo") return Profile::rel_with_deb_info;
    if (value == "MinSizeRel") return Profile::min_size_rel;
    if (value == "Custom") return Profile::custom;
    return Error{ErrorCode::input, "unknown build profile"};
}

Result<Limits> Limits::validate() const {
    if (!build_workers || inference_workers != 1 || !primary_attempts ||
        timeout <= chrono::seconds::zero() || !token_budget) {
        return Error{ErrorCode::input, "limits require positive values and exactly one inference slot"};
    }
    return *this;
}

}
