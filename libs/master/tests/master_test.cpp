#include "gremlin/master/master.hpp"
#include "gremlin/platform/files.hpp"
#include "gremlin/state/jobs.hpp"

#include <fstream>
#include <iostream>
#include <iterator>

using namespace std;

namespace {

string read(const filesystem::path& path) {
    ifstream file(path, ios::binary);
    return {istreambuf_iterator<char>(file), {}};
}

}

int main(int argc, char** argv) {
    using namespace gremlin;
    namespace fs = filesystem;
    if (argc != 8) return 1;
    const string_view mode = argv[1];
    RepairRequest request;
    request.build.source_dir = path_from_utf8(argv[2]);
    request.build.cmake = path_from_utf8(argv[3]);
    request.build.ctest = path_from_utf8(argv[4]);
    request.build.ninja = path_from_utf8(argv[5]);
    request.build.compiler = path_from_utf8(argv[6]);
    request.build.profile = get<Profile>(parse_profile(argv[7]));
    request.build.trusted_source = true;
    request.checker = request.build.source_dir / "acceptance.cmake";
    request.runs_dir = "test-runs";
    request.initial_sources = {"main.cpp"};
    request.prompt = read(request.build.source_dir / "ProjectPrompt.md");
    if (mode == "test_failure") {
        auto checker = read(request.checker);
        const auto expected = checker.find("mixed-lines.txt\" 3");
        if (expected == string::npos) return 8;
        checker.replace(expected, string_view("mixed-lines.txt\" 3").size(), "mixed-lines.txt\" 4");
        request.checker = "failing-check-" + JobId::generate().value + ".cmake";
        ofstream(request.checker, ios::binary) << checker;
        request.initial_sources.clear();
        request.max_repairs = 0;
    }
    unsigned requests = unsigned();
    const auto result = run_repair(request, [&](const SourceRepairRequest& proposal) -> Result<vector<string>> {
        ++requests;
        if (proposal.prompt != request.prompt || proposal.diagnostic.find("count_lines") == string::npos ||
            proposal.available_sources != vector<string>{"line_count.cpp", "main.cpp"} ||
            proposal.attempted_sources != request.initial_sources) {
            return Error{ErrorCode::internal, "repair worker received wrong build evidence"};
        }
        if (mode == "repeat") return request.initial_sources;
        if (mode == "reject") return vector<string>{"../outside.cpp", "main.cpp"};
        return proposal.available_sources;
    });
    if (const auto* error = get_if<Error>(&result)) {
        cerr << error->message << '\n';
        return 2;
    }
    const auto& run = get<RepairRun>(result);
    cout << run.directory << '\n';
    const auto state = JobStore::open(run.directory / "jobs.sqlite");
    if (state.index() || get<JobState>(get<JobStore::Handle>(state)->state(run.job)) != run.state) return 4;
    const auto first = run.directory / "attempt-1";
    const auto second = run.directory / "attempt-2";
    const auto log = read(run.directory / "gremlin.log");
    if (mode == "test_failure") {
        const auto test_log = read(first / "build/gremlin-logs/test.log");
        return requests == 0 && run.attempts == 1 && run.state == JobState::failed && run.failure &&
            run.failure->code == ErrorCode::test && !run.evidence && !fs::exists(second) &&
            test_log.find("Unexpected result for mixed-lines.txt") != test_log.npos &&
            log.find("\"action\":\"verified\"") == log.npos ? 0 : 9;
    }
    if (requests != 1 || run.attempts != 2) return 3;
    if (log.find("\"action\":\"failed\"") == log.npos || log.find("\"task\":\"propose_repair\"") == log.npos ||
        log.find("\"action\":\"repair_scheduled\"") == log.npos ||
        read(first / "source/CMakeLists.txt").find("\"line_count.cpp\"") != string::npos ||
        fs::exists(first / "build/gremlin-logs/test.log")) return 5;
    if (mode == "repeat" || mode == "reject") {
        const auto code = mode == "repeat" ? ErrorCode::build : ErrorCode::policy;
        const auto terminal = mode == "repeat" ? JobState::failed : JobState::blocked;
        return run.state == terminal && run.failure && run.failure->code == code &&
            !run.evidence && !fs::exists(second / "source") ? 0 : 6;
    }
    if (run.failure) cerr << run.failure->message << '\n';
    if (run.state != JobState::verified || !run.evidence || !fs::is_regular_file(run.executable) ||
        !fs::is_regular_file(run.evidence->junit) || read(second / "source/acceptance.cmake") != read(request.checker) ||
        read(second / "source/CMakeLists.txt").find("\"line_count.cpp\"") == string::npos ||
        log.find("\"action\":\"verified\"") == log.npos) return 7;
}
