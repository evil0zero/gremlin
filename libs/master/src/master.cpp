#include "gremlin/master/master.hpp"

#include "gremlin/logging/jsonl.hpp"
#include "gremlin/scheduler/scheduler.hpp"
#include "gremlin/state/jobs.hpp"
#include "project.hpp"

#include <algorithm>
#include <fstream>
#include <set>

using namespace std;

namespace gremlin {
namespace {

namespace fs = filesystem;

TaskResult log_master(EventSink& sink, const RepairRun& run, const char* action, optional<ErrorCode> error = {}) {
    Event event{run.job, "master", action, run.job.value};
    event.attempt = run.attempts;
    event.error = error;
    return sink.emit(event);
}

string failed_diagnostic(const fs::path& log) {
    ifstream input(log, ios::binary | ios::ate);
    const auto size = input.tellg();
    if (size <= 0) return {};
    const auto count = min<streamoff>(size, 4096);
    string tail(static_cast<size_t>(count), '\0');
    input.seekg(-count, ios::end);
    if (!input.read(tail.data(), count) || !tail.ends_with("\ntimed_out=0\n")) return {};
    if (tail.starts_with("argv: ")) tail.erase(0, tail.find('\n') + 1);
    for (char& c : tail) {
        const auto byte = static_cast<unsigned char>(c);
        if ((byte < 32 && c != '\n' && c != '\r' && c != '\t') || byte > 126) c = '?';
    }
    return tail;
}

TaskResult execute_plan(const RepairRequest& request, const RepairProvider& provider, RepairRun& run, EventSink& events) {
    master::ProjectSnapshot snapshot;
    const vector<ScheduledTask> intake{{"snapshot", {}, [&]() -> TaskResult {
        auto result = master::snapshot_project(request, run.directory / "input");
        if (const auto* error = get_if<Error>(&result)) return *error;
        snapshot = move(get<master::ProjectSnapshot>(result));
        return monostate{};
    }}};
    auto intake_result = schedule_tasks(intake, 1, run.job, 0, events);
    if (const auto* error = get_if<Error>(&intake_result)) return *error;
    if (const auto& error = get<vector<TaskOutcome>>(intake_result).front().error) return *error;

    auto selection = request.initial_sources.empty() ? snapshot.sources : request.initial_sources;
    sort(selection.begin(), selection.end());
    if (!master::valid_selection(selection, snapshot)) {
        return Error{ErrorCode::input, "initial sources must be unique members of the C++ source inventory"};
    }
    set<vector<string>> seen{selection};
    string diagnostic;
    for (unsigned attempt = 1; attempt <= request.max_repairs + 1; ++attempt) {
        run.attempts = attempt;
        const auto logged = log_master(events, run, "attempt_started");
        if (const auto* error = get_if<Error>(&logged)) return *error;

        const auto revision = run.directory / ("attempt-" + std::to_string(attempt));
        auto build = request.build;
        build.source_dir = revision / "source";
        build.build_dir = revision / "build";
        optional<BuildSession> session;
        vector<ScheduledTask> tasks;
        auto enqueue = [&](string name, function<TaskResult()> worker) {
            vector<size_t> dependencies;
            if (!tasks.empty()) dependencies.push_back(tasks.size() - 1);
            tasks.push_back({move(name), move(dependencies), move(worker)});
        };
        if (attempt > 1) {
            enqueue("propose_repair", [&]() -> TaskResult {
                auto proposal = provider({request.model, request.prompt, snapshot.sources, selection,
                    diagnostic, request.curl, revision / "inference"});
                if (const auto* error = get_if<Error>(&proposal)) return *error;
                auto candidate = move(get<vector<string>>(proposal));
                sort(candidate.begin(), candidate.end());
                if (!master::valid_selection(candidate, snapshot)) {
                    return Error{ErrorCode::policy, "model proposed unsupported or duplicate source paths"};
                }
                if (seen.contains(candidate)) {
                    return Error{ErrorCode::build, "model repeated a previously attempted source selection"};
                }
                selection = move(candidate);
                return monostate{};
            });
        }
        enqueue("stage_revision", [&]() -> TaskResult {
            const auto staged = master::stage_project(snapshot, selection, build.source_dir);
            if (const auto* error = get_if<Error>(&staged)) return *error;
            auto prepared = prepare_build(build);
            if (const auto* error = get_if<Error>(&prepared)) return *error;
            session = move(get<BuildSession>(prepared));
            return monostate{};
        });
        enqueue("configure", [&] { return session->configure(); });
        enqueue("compile", [&] { return session->compile(); });
        enqueue("trusted_test", [&] { return session->test(); });

        auto completed = schedule_tasks(tasks, 1, run.job, attempt, events);
        if (const auto* error = get_if<Error>(&completed)) return *error;
        const auto& outcomes = get<vector<TaskOutcome>>(completed);
        const auto failure = find_if(outcomes.begin(), outcomes.end(), [](const TaskOutcome& outcome) {
            return outcome.state == TaskState::failed;
        });
        if (failure == outcomes.end()) {
            const auto executable = session->evidence().build_dir / "bin" /
#ifdef _WIN32
                "repair_app.exe";
#else
                "repair_app";
#endif
            if (!fs::is_regular_file(executable)) return Error{ErrorCode::test, "verified executable is missing"};
            run.evidence = session->evidence();
            run.executable = executable;
            return monostate{};
        }

        const auto& stage = tasks[static_cast<size_t>(failure - outcomes.begin())].name;
        const auto code = failure->error->code;
        if (attempt > request.max_repairs || !session ||
            (code != ErrorCode::build && code != ErrorCode::test)) return *failure->error;
        const char* logfile = stage == "configure" ? "configure.log" : stage == "compile" ? "build.log" : "test.log";
        diagnostic = failed_diagnostic(session->evidence().log_dir / logfile);
        if (diagnostic.empty()) return *failure->error;
        seen.insert(selection);
        const auto retry = log_master(events, run, "repair_scheduled", code);
        if (const auto* error = get_if<Error>(&retry)) return *error;
    }
    return Error{ErrorCode::internal, "repair plan reached an invalid terminal state"};
}

}

Result<RepairRun> run_repair(const RepairRequest& request, RepairProvider provider) {
    if (!request.build.trusted_source) {
        return Error{ErrorCode::policy, "repair requires explicitly trusted source files and checker"};
    }
    if (request.prompt.size() > cmake_prompt_limit ||
        request.prompt.find_first_not_of(" \t\r\n") == string::npos ||
        request.max_repairs > 3 || request.runs_dir.empty() || (request.max_repairs && !provider)) {
        return Error{ErrorCode::input, "repair requires a bounded nonempty prompt, a run directory, and at most three repairs"};
    }
    try {
        if (!fs::is_directory(request.build.source_dir)) {
            return Error{ErrorCode::input, "source must be an existing project directory"};
        }
        const auto job = JobId::generate();
        RepairRun run{job, JobState::pending, fs::absolute(request.runs_dir) / job.value};
        if (!fs::create_directories(run.directory)) {
            return Error{ErrorCode::internal, "cannot reserve a new repair run directory"};
        }
        auto opened = JobStore::open(run.directory / "jobs.sqlite");
        if (const auto* error = get_if<Error>(&opened)) return *error;
        auto& store = *get<JobStore::Handle>(opened);
        const auto saved = store.submit(job, request.prompt);
        if (const auto* error = get_if<Error>(&saved)) return *error;

        JsonlSink log(run.directory / "gremlin.log", 1024 * 1024);
        const auto started = log_master(log, run, "run_started");
        if (const auto* error = get_if<Error>(&started)) return *error;
        const auto running = store.transition(job, JobState::pending, JobState::running);
        if (const auto* error = get_if<Error>(&running)) return *error;
        run.state = JobState::running;
        TaskResult result;
        try {
            result = execute_plan(request, provider, run, log);
        } catch (const exception& error) {
            result = Error{ErrorCode::internal, error.what()};
        }
        run.state = JobState::verified;
        if (const auto* error = get_if<Error>(&result)) {
            run.failure = *error;
            run.state = error->code == ErrorCode::policy || error->code == ErrorCode::prerequisite
                ? JobState::blocked : JobState::failed;
        }
        const auto saved_status = store.transition(job, JobState::running, run.state);
        if (const auto* error = get_if<Error>(&saved_status)) return *error;
        const char* terminal = run.state == JobState::verified ? "verified" :
            run.state == JobState::blocked ? "blocked" : "failed";
        const auto finished = log_master(log, run, terminal, run.failure ? optional(run.failure->code) : nullopt);
        if (const auto* error = get_if<Error>(&finished)) return *error;
        return run;
    } catch (const fs::filesystem_error& error) {
        return Error{ErrorCode::internal, error.what()};
    }
}

}
