#pragma once

#include "gremlin/core/core.hpp"

#include <filesystem>
#include <mutex>

namespace gremlin {

// One sink per file. Component and action are trusted event labels.
// Keep user data, command output, and secrets out of this event stream.
class JsonlSink final : public EventSink {
public:
    JsonlSink(std::filesystem::path file, std::uintmax_t rotate_at_bytes);
    Result<std::monostate> emit(const Event& event) override;

private:
    std::filesystem::path file_;
    std::uintmax_t rotate_at_bytes_;
    std::mutex mutex_;
};

}
