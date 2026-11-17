#include "gremlin/logging/jsonl.hpp"

#include <algorithm>
#include <fstream>
#include <utility>

using namespace std;

namespace gremlin {
namespace {

bool is_label(string_view value) {
    return !value.empty() && value.size() <= 64 && all_of(value.begin(), value.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}

}

JsonlSink::JsonlSink(filesystem::path file, uintmax_t rotate_at_bytes)
    : file_(move(file)), rotate_at_bytes_(rotate_at_bytes) {}

Result<monostate> JsonlSink::emit(const Event& event) {
    if (JobId::parse(event.job.value).index() || JobId::parse(event.correlation_id).index() ||
        !is_label(event.component) || !is_label(event.action) ||
        (!event.task.empty() && !is_label(event.task))) {
        return Error{ErrorCode::input, "log events require IDs and trusted event labels"};
    }
    const auto epoch = chrono::duration_cast<chrono::milliseconds>(
        event.timestamp.time_since_epoch()).count();
    string line = "{\"unix_ms\":" + std::to_string(epoch) + ",\"job_id\":\"" + event.job.value
        + "\",\"component\":\"" + event.component + "\",\"action\":\"" + event.action
        + "\",\"correlation_id\":\"" + event.correlation_id + '"';
    if (!event.task.empty()) line += ",\"task\":\"" + event.task + '"';
    if (event.attempt) line += ",\"attempt\":" + std::to_string(event.attempt);
    if (event.duration) line += ",\"duration_ms\":" + std::to_string(event.duration->count());
    if (event.error) line += ",\"error_code\":" + std::to_string(static_cast<int>(*event.error));
    line += "}\n";
    if (line.size() > rotate_at_bytes_) return Error{ErrorCode::input, "event exceeds log size limit"};

    scoped_lock lock(mutex_);
    try {
        if (filesystem::exists(file_) &&
            filesystem::file_size(file_) > rotate_at_bytes_ - line.size()) {
            auto previous = file_;
            previous += ".1";
            filesystem::remove(previous);
            filesystem::rename(file_, previous);
        }
        ofstream output(file_, ios::app | ios::binary);
        output << line;
        output.close();
        if (output) return monostate{};
    } catch (const filesystem::filesystem_error& error) {
        return Error{ErrorCode::internal, error.what()};
    }
    return Error{ErrorCode::internal, "cannot append event log"};
}

}
