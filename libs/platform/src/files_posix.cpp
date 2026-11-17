#include "gremlin/platform/files.hpp"
#include <cerrno>
#include <cstdio>
#ifdef __linux__
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

using namespace std;

namespace gremlin {

Result<monostate> publish_new_directory(const filesystem::path& staged, const filesystem::path& destination) {
#if defined(__linux__) && defined(SYS_renameat2)
    if (syscall(SYS_renameat2, AT_FDCWD, staged.c_str(), AT_FDCWD, destination.c_str(), 1U) == 0) return monostate{};
#elif defined(__APPLE__)
    if (renamex_np(staged.c_str(), destination.c_str(), RENAME_EXCL) == 0) return monostate{};
#else
    (void)staged;
    (void)destination;
    return Error{ErrorCode::prerequisite, "no exclusive directory rename on this platform"};
#endif
    const int code = errno;
    return Error{code == EEXIST ? ErrorCode::input : ErrorCode::internal,
        "could not publish project: " + generic_category().message(code)};
}

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
