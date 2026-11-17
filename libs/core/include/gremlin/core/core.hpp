#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace gremlin {

enum class ErrorCode {
    input = 2, prerequisite = 3, policy = 4, build = 5, test = 6, internal = 7
};

struct Error {
    ErrorCode code{ErrorCode::internal};
    std::string message;
};

template<typename T>
using Result = std::variant<T, Error>;

struct JobId {
    std::string value;
    static JobId generate();
    static Result<JobId> parse(std::string_view value);
};

enum class JobState { pending, running, verified, failed, cancelled, blocked, uncertain };
std::string_view to_string(JobState state) noexcept;
bool can_transition(JobState from, JobState to) noexcept;

enum class Profile { debug, release, rel_with_deb_info, min_size_rel, custom };
Result<Profile> parse_profile(std::string_view value);

struct Limits {
    unsigned build_workers{1};
    unsigned inference_workers{1};
    unsigned primary_attempts{3};
    std::chrono::seconds timeout{120};
    unsigned token_budget{8192};
    Result<Limits> validate() const;
};

struct Event {
    JobId job;
    std::string component;
    std::string action;
    std::string correlation_id;
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
    std::string task{};
    unsigned attempt = unsigned();
    std::optional<std::chrono::milliseconds> duration{};
    std::optional<ErrorCode> error{};
};

class EventSink {
public:
    virtual ~EventSink() = default;
    virtual Result<std::monostate> emit(const Event&) = 0;
};

}
