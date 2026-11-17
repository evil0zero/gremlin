#include "gremlin/build/build.hpp"
#include "gremlin/platform/process.hpp"

#include <array>
#include <fstream>
#include <iomanip>

using namespace std;

namespace gremlin {
namespace {

namespace fs = filesystem;

string utf8(const fs::path& path) {
    const auto value = path.generic_u8string();
    return string(value.begin(), value.end());
}

bool is_within(const fs::path& candidate, const fs::path& existing_root) {
    for (auto ancestor = candidate; !ancestor.empty(); ancestor = ancestor.parent_path()) {
        if (fs::exists(ancestor) && fs::equivalent(ancestor, existing_root)) return true;
        if (ancestor == ancestor.root_path()) break;
    }
    return false;
}

Result<monostate> run_step(const char* stage, ErrorCode failure, const fs::path& log,
        const fs::path& executable, vector<string> arguments) {
    {
        ofstream file(log, ios::binary);
        file << "argv: " << quoted(utf8(executable));
        for (const auto& arg : arguments) file << ' ' << quoted(arg);
        file << '\n';
        file.close();
        if (!file) return Error{ErrorCode::internal, "cannot write " + utf8(log)};
    }

    const auto start = chrono::steady_clock::now();
    const auto outcome = run_process({executable, move(arguments), log, chrono::minutes(5)});
    const auto elapsed = chrono::duration_cast<chrono::milliseconds>(
        chrono::steady_clock::now() - start).count();

    ofstream file(log, ios::app | ios::binary);
    file << "\nelapsed_ms=" << elapsed << '\n';
    const auto* error = get_if<Error>(&outcome);
    const auto* exit = get_if<ProcessExit>(&outcome);
    if (error) file << "process_error=" << error->message << '\n';
    if (exit) file << "exit=" << exit->code << "\ntimed_out=" << exit->timed_out << '\n';
    file.close();
    if (!file) return Error{ErrorCode::internal, "cannot finish " + utf8(log)};
    const string evidence = ". See " + utf8(log);
    if (error) return Error{error->code, error->message + evidence};
    if (exit->timed_out) return Error{failure, string(stage) + " timed out" + evidence};
    if (exit->code != 0) {
        return Error{failure, string(stage) + " exited " + std::to_string(exit->code) + evidence};
    }
    return monostate{};
}

}

BuildSession::BuildSession(BuildRequest request, BuildEvidence evidence, const char* config)
    : request_(move(request)), evidence_(move(evidence)), config_(config) {}

Result<BuildSession> prepare_build(const BuildRequest& request) {
    if (!request.trusted_source) return Error{ErrorCode::policy, "build requires explicit source trust"};
    const char* config{};
    switch (request.profile) {
        case Profile::debug: config = "Debug"; break;
        case Profile::release: config = "Release"; break;
        case Profile::rel_with_deb_info: config = "RelWithDebInfo"; break;
        case Profile::min_size_rel: config = "MinSizeRel"; break;
        default: return Error{ErrorCode::input, "unsupported Ninja build profile"};
    }

    try {
        if (!fs::is_directory(request.source_dir) ||
            !fs::is_regular_file(request.source_dir / "CMakeLists.txt") || request.build_dir.empty()) {
            return Error{ErrorCode::input, "expected a CMake source directory and a separate build directory"};
        }
        for (const auto& [tool, label] : array{pair{&request.cmake, "CMake"},
                pair{&request.ctest, "CTest"}, pair{&request.ninja, "Ninja"},
                pair{&request.compiler, "C++ compiler"}}) {
            if (!fs::is_regular_file(*tool)) {
                return Error{ErrorCode::prerequisite, string(label) + " executable path is missing"};
            }
        }

        const auto source = fs::canonical(request.source_dir);
        const auto build = fs::weakly_canonical(fs::absolute(request.build_dir));
        if (is_within(build, source) || (fs::is_directory(build) && is_within(source, build))) {
            return Error{ErrorCode::input, "build directory must be outside the source tree"};
        }
        if (fs::exists(fs::symlink_status(request.build_dir))) {
            return Error{ErrorCode::input, "build directory already exists. Choose a fresh directory"};
        }
        fs::create_directories(build.parent_path());
        if (!fs::create_directory(build)) {
            return Error{ErrorCode::input, "build directory already exists. Choose a fresh directory"};
        }
        BuildEvidence evidence{build, build / "gremlin-logs", build / "gremlin-logs" / "junit.xml"};
        fs::create_directory(evidence.log_dir);
        auto prepared = request;
        prepared.source_dir = source;
        prepared.cmake = fs::absolute(request.cmake);
        prepared.ctest = fs::absolute(request.ctest);
        prepared.ninja = fs::absolute(request.ninja);
        prepared.compiler = fs::absolute(request.compiler);
        return BuildSession(move(prepared), move(evidence), config);
    } catch (const fs::filesystem_error& error) {
        return Error{ErrorCode::internal, error.what()};
    }
}

Result<monostate> BuildSession::configure() const {
    return run_step("configure", ErrorCode::build, evidence_.log_dir / "configure.log", request_.cmake,
        {"-S", utf8(request_.source_dir), "-B", utf8(evidence_.build_dir), "-G", "Ninja",
         string("-DCMAKE_BUILD_TYPE=") + config_, "-DCMAKE_MAKE_PROGRAM=" + utf8(request_.ninja),
         "-DCMAKE_CXX_COMPILER=" + utf8(request_.compiler), "-DBUILD_TESTING=ON"});
}

Result<monostate> BuildSession::compile() const {
    return run_step("build", ErrorCode::build, evidence_.log_dir / "build.log", request_.cmake,
        {"--build", utf8(evidence_.build_dir), "--parallel"});
}

Result<monostate> BuildSession::test() const {
    const auto outcome = run_step("tests", ErrorCode::test, evidence_.log_dir / "test.log", request_.ctest,
        {"--test-dir", utf8(evidence_.build_dir), "--no-tests=error", "--output-on-failure",
         "--output-junit", utf8(evidence_.junit)});
    if (const auto* error = get_if<Error>(&outcome)) return *error;
    error_code error;
    const auto size = fs::file_size(evidence_.junit, error);
    if (error || size == 0) {
        return Error{ErrorCode::test, "CTest did not produce evidence at " + utf8(evidence_.junit)};
    }
    return monostate{};
}

Result<BuildEvidence> build_and_test(const BuildRequest& request) {
    auto prepared = prepare_build(request);
    if (const auto* error = get_if<Error>(&prepared)) return *error;
    const auto& build = get<BuildSession>(prepared);
    for (auto step : {&BuildSession::configure, &BuildSession::compile, &BuildSession::test}) {
        const auto result = (build.*step)();
        if (const auto* error = get_if<Error>(&result)) return *error;
    }
    return build.evidence();
}

}
