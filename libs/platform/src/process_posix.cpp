#include "gremlin/platform/process.hpp"

#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <system_error>
#include <thread>
#include <unistd.h>

using namespace std;

extern char** environ;

namespace gremlin {
namespace {

Error platform_error(const char* operation, int code) {
    return {ErrorCode::internal, string(operation) + " failed: " + generic_category().message(code)};
}

class FileActions {
public:
    FileActions() : error_(posix_spawn_file_actions_init(&actions_)) {}
    ~FileActions() { if (!error_) posix_spawn_file_actions_destroy(&actions_); }
    int error() const { return error_; }
    posix_spawn_file_actions_t* get() { return &actions_; }
private:
    posix_spawn_file_actions_t actions_{};
    int error_;
};

class SpawnAttributes {
public:
    SpawnAttributes() : error_(posix_spawnattr_init(&attrs_)) {}
    ~SpawnAttributes() { if (!error_) posix_spawnattr_destroy(&attrs_); }
    int error() const { return error_; }
    posix_spawnattr_t* get() { return &attrs_; }
private:
    posix_spawnattr_t attrs_{};
    int error_;
};

}

Result<ProcessExit> run_process(const ProcessRequest& request) {
    if (!request.executable.is_absolute() || request.timeout.count() <= 0 ||
        request.timeout > chrono::hours(24)) {
        return Error{ErrorCode::input, "invalid executable path or process timeout"};
    }
    string executable = request.executable.native();
    auto arguments = request.arguments;
    vector<char*> argv{executable.data()};
    for (auto& argument : arguments) {
        if (argument.find('\0') != string::npos) {
            return Error{ErrorCode::input, "process argument contains NUL"};
        }
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);

    FileActions actions;
    SpawnAttributes attrs;
    if (actions.error()) return platform_error("posix_spawn_file_actions_init", actions.error());
    if (attrs.error()) return platform_error("posix_spawnattr_init", attrs.error());
    if (const int rc = posix_spawn_file_actions_addopen(actions.get(), STDIN_FILENO, "/dev/null", O_RDONLY, 0)) {
        return platform_error("redirect stdin", rc);
    }
    if (const int rc = posix_spawn_file_actions_addopen(actions.get(), STDOUT_FILENO, request.log.c_str(),
            O_WRONLY | O_APPEND, 0)) {
        return platform_error("redirect stdout", rc);
    }
    if (const int rc = posix_spawn_file_actions_adddup2(actions.get(), STDOUT_FILENO, STDERR_FILENO)) {
        return platform_error("redirect stderr", rc);
    }
    if (const int rc = posix_spawnattr_setpgroup(attrs.get(), 0)) return platform_error("set process group", rc);
    if (const int rc = posix_spawnattr_setflags(attrs.get(), POSIX_SPAWN_SETPGROUP)) {
        return platform_error("set spawn flags", rc);
    }
    pid_t child = pid_t();
    if (const int rc = posix_spawn(&child, executable.c_str(), actions.get(), attrs.get(), argv.data(), environ)) {
        return platform_error("posix_spawn", rc);
    }

    const auto deadline = chrono::steady_clock::now() + request.timeout;
    int status = int();
    for (;;) {
        const auto result = waitpid(child, &status, WNOHANG);
        if (result == child) {
            return ProcessExit{static_cast<uint32_t>(WIFEXITED(status) ? WEXITSTATUS(status)
                : 128 + WTERMSIG(status)), false};
        }
        if (result < 0 && errno != EINTR) return platform_error("waitpid", errno);
        if (chrono::steady_clock::now() >= deadline) {
            if (kill(-child, SIGKILL) < 0 && errno != ESRCH) return platform_error("signal process group", errno);
            while (waitpid(child, &status, 0) < 0) {
                if (errno != EINTR) return platform_error("reap process", errno);
            }
            return ProcessExit{124, true};
        }
        this_thread::sleep_for(chrono::milliseconds(20));
    }
}

}
