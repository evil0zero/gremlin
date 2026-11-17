#include "gremlin/platform/files.hpp"

using namespace std;

namespace gremlin {

filesystem::path path_from_utf8(string_view value) {
    return filesystem::path(value);
}

Result<monostate> publish_new_file(
        const filesystem::path& staged, const filesystem::path& destination) {
    error_code error;
    filesystem::create_hard_link(staged, destination, error);
    if (!error) return monostate{};
    if (error == errc::file_exists) return Error{ErrorCode::input, "destination already exists"};
    return Error{ErrorCode::internal, "cannot publish the staged file with a local hard link: " + error.message()};
}

}
