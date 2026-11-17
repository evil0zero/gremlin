#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "gremlin/platform/files.hpp"

using namespace std;

namespace gremlin {

filesystem::path path_from_utf8(string_view value) {
    return filesystem::path(u8string(value.begin(), value.end()));
}

Result<monostate> publish_new_file(
        const filesystem::path& staged, const filesystem::path& destination) {
    if (MoveFileW(staged.c_str(), destination.c_str())) return monostate{};
    const auto error = GetLastError();
    if (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS) {
        return Error{ErrorCode::input, "destination already exists"};
    }
    return Error{ErrorCode::internal, "publishing the staged file failed (Windows error " +
        std::to_string(error) + ')'};
}

}
