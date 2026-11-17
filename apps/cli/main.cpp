#include "gremlin/state/jobs.hpp"
#include "gremlin/build/build.hpp"
#include "gremlin/inference/ollama.hpp"
#include "gremlin/master/master.hpp"
#include "gremlin/platform/files.hpp"
#include "gremlin/source/cmake_proposal.hpp"
#include "gremlin/source/scaffold.hpp"

#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

using namespace std;

namespace {

string invocation(string_view value) {
    const auto end = value.find_last_not_of("/\\");
    if (end == value.npos) return "program";
    value = value.substr(0, end + 1);
    const auto slash = value.find_last_of("/\\");
    const auto name = value.substr(slash == value.npos ? 0 : slash + 1);
    if (name.empty() || name == "." || name == ".." ||
        name.find_first_of("\r\n\t") != name.npos) return "program";
    return string(name);
}

int fail(const string& name, const gremlin::Error& error) {
    cerr << name << ": " << error.message << '\n';
    return static_cast<int>(error.code);
}

using Options = map<string_view, string>;

filesystem::path curl_path(const Options& options) {
    if (options.contains("--curl")) return gremlin::path_from_utf8(options.at("--curl"));
#ifdef GREMLIN_DEFAULT_CURL
    return gremlin::path_from_utf8(GREMLIN_DEFAULT_CURL);
#else
    return {};
#endif
}

gremlin::Result<string> read_prompt(const Options& options, const filesystem::path& source) {
    namespace fs = filesystem;
    using namespace gremlin;
    if (options.contains("--prompt") && options.contains("--spec")) {
        return Error{ErrorCode::input, "choose either --prompt or --spec"};
    }
    if (options.contains("--prompt")) return options.at("--prompt");
    const auto spec = options.contains("--spec") ? path_from_utf8(options.at("--spec")) : source / "ProjectPrompt.md";
    if (!fs::is_regular_file(spec) || fs::file_size(spec) > cmake_prompt_limit) {
        return Error{ErrorCode::input, "expected a readable prompt file of at most 8192 bytes"};
    }
    ifstream input(spec, ios::binary);
    string prompt(cmake_prompt_limit + 1, '\0');
    input.read(prompt.data(), static_cast<streamsize>(prompt.size()));
    if (input.bad() || input.gcount() == 0 || input.gcount() > static_cast<streamsize>(cmake_prompt_limit)) {
        return Error{ErrorCode::input, "could not read a nonempty bounded prompt file"};
    }
    prompt.resize(static_cast<size_t>(input.gcount()));
    return prompt;
}

int init_project(const string& name, const vector<string>& args) {
    using namespace gremlin;
    if ((args.size() != 4 && args.size() != 6) || args[2] != "--source" ||
        args[3].empty() || args[3].starts_with("--") || (args.size() == 6 && args[4] != "--name")) {
        return fail(name, {ErrorCode::input, "expected " + name + " init --source NEW_DIR [--name NAME]"});
    }
    const auto result = create_starter_project(path_from_utf8(args[3]),
        args.size() == 6 ? args[5] : "sweet_nothings");
    if (const auto* error = get_if<Error>(&result)) return fail(name, *error);
    const auto directory = get<filesystem::path>(result).generic_u8string();
    cout << "Created CMakeLists.txt, main.cpp and check.cmake in "
              << string(directory.begin(), directory.end()) << endl;
    return 0;
}

int generate_cmake(const string& name, const vector<string>& args) {
    using namespace gremlin;
    namespace fs = filesystem;
    Options options;
    for (size_t i = 2; i < args.size(); i += 2) {
        const auto& option = args[i];
        if (i + 1 == args.size() || args[i + 1].empty() ||
            (option != "--source" && option != "--model" && option != "--curl" &&
             option != "--prompt" && option != "--spec") ||
            !options.emplace(option, args[i + 1]).second) {
            return fail(name, {ErrorCode::input, "invalid generate-cmake options. See " + name + " --help"});
        }
    }
    if (!options.contains("--source") || !options.contains("--model") ||
        (options.contains("--prompt") && options.contains("--spec"))) {
        return fail(name, {ErrorCode::input, "generate-cmake requires --source and --model. "
            "Choose either --prompt or --spec"});
    }
    const auto source = path_from_utf8(options.at("--source"));
    auto inputs = cmake_inputs(source);
    if (const auto* error = get_if<Error>(&inputs)) return fail(name, *error);

    auto prompt = read_prompt(options, source);
    if (const auto* error = get_if<Error>(&prompt)) return fail(name, *error);
    const auto log = fs::path("out/logs/inference") / JobId::generate().value;
    const auto result = infer_cmake({options.at("--model"), move(get<string>(prompt)),
        move(get<vector<string>>(inputs)), curl_path(options), log});
    if (const auto* error = get_if<Error>(&result)) return fail(name, *error);
    const auto published = publish_cmake_proposal(source, get<string>(result));
    if (const auto* error = get_if<Error>(&published)) return fail(name, *error);
    const auto path = get<fs::path>(published).generic_u8string();
    cout << "Generated " << string(path.begin(), path.end()) << " using " << options.at("--model")
              << "\nInference logs " << log.generic_string() << endl;
    return 0;
}

int build_project(const string& name, const vector<string>& args) {
    using namespace gremlin;
    BuildRequest request;
    map<string_view, filesystem::path*> paths{
        {"--source", &request.source_dir}, {"--build-dir", &request.build_dir},
        {"--cmake", &request.cmake}, {"--ctest", &request.ctest},
        {"--ninja", &request.ninja}, {"--compiler", &request.compiler}
    };
    bool has_profile = false;
    for (size_t i = 2; i < args.size(); ++i) {
        const auto& option = args[i];
        if (option == "--trust-source" && !request.trusted_source) {
            request.trusted_source = true;
            continue;
        }
        if (i + 1 == args.size() || args[i + 1].empty() || args[i + 1].starts_with("--")) {
            return fail(name, {ErrorCode::input, "invalid build option or missing value. See " + name + " --help"});
        }
        if (option == "--profile" && !has_profile) {
            const auto result = parse_profile(args[++i]);
            if (const auto* error = get_if<Error>(&result)) return fail(name, *error);
            request.profile = get<Profile>(result);
            has_profile = true;
            continue;
        }
        auto field = paths.find(option);
        if (field == paths.end() || !field->second->empty()) {
            return fail(name, {ErrorCode::input, "unknown or repeated build option. See " + name + " --help"});
        }
        *field->second = path_from_utf8(args[++i]);
    }
    for (const auto& [option, value] : paths) {
        if (value->empty()) return fail(name, {ErrorCode::input, "missing " + string(option)});
    }

    const auto outcome = build_and_test(request);
    if (const auto* error = get_if<Error>(&outcome)) return fail(name, *error);
    const auto& evidence = get<BuildEvidence>(outcome);
    const auto build = evidence.build_dir.generic_u8string();
    const auto junit = evidence.junit.generic_u8string();
    cout << "Build and tests passed in " << string(build.begin(), build.end())
              << "\nJUnit report " << string(junit.begin(), junit.end()) << endl;
    return 0;
}

int repair_project(const string& name, const vector<string>& args) {
    using namespace gremlin;
    namespace fs = filesystem;
    RepairRequest request;
    Options options;
    const map<string_view, fs::path*> paths{
        {"--source", &request.build.source_dir}, {"--cmake", &request.build.cmake},
        {"--ctest", &request.build.ctest}, {"--ninja", &request.build.ninja},
        {"--compiler", &request.build.compiler}
    };
    for (size_t i = 2; i < args.size(); ++i) {
        const string_view key = args[i];
        if (key == "--trust-source" && !request.build.trusted_source) {
            request.build.trusted_source = true;
            continue;
        }
        if (i + 1 == args.size() || args[i + 1].empty() || args[i + 1].starts_with("--")) {
            return fail(name, {ErrorCode::input, "invalid repair-cmake option or missing value. See " + name + " --help"});
        }
        const auto& value = args[++i];
        if (key == "--initial-source") {
            request.initial_sources.push_back(value);
            continue;
        }
        if ((!paths.contains(key) && key != "--model" && key != "--profile" && key != "--check" &&
             key != "--max-repairs" && key != "--curl" && key != "--prompt" && key != "--spec") ||
            !options.emplace(key, value).second) {
            return fail(name, {ErrorCode::input, "unknown or repeated repair-cmake option. See " + name + " --help"});
        }
    }
    if (!request.build.trusted_source) {
        return fail(name, {ErrorCode::policy, "repair-cmake requires --trust-source for the sources and checker"});
    }
    for (const auto& [flag, path] : paths) {
        if (!options.contains(flag)) return fail(name, {ErrorCode::input, "missing " + string(flag)});
        *path = path_from_utf8(options.at(flag));
    }
    if (options.contains("--profile")) {
        const auto profile = parse_profile(options.at("--profile"));
        if (const auto* error = get_if<Error>(&profile)) return fail(name, *error);
        request.build.profile = get<Profile>(profile);
    }
    if (options.contains("--max-repairs")) {
        const auto& value = options.at("--max-repairs");
        const auto end = value.data() + value.size();
        const auto parsed = from_chars(value.data(), end, request.max_repairs);
        if (parsed.ec != errc{} || parsed.ptr != end || request.max_repairs > 3) {
            return fail(name, {ErrorCode::input, "--max-repairs must be between 0 and 3"});
        }
    }
    if (request.max_repairs && !options.contains("--model")) {
        return fail(name, {ErrorCode::input, "supply --model NAME:TAG for automatic repair"});
    }
    auto prompt = read_prompt(options, request.build.source_dir);
    if (const auto* error = get_if<Error>(&prompt)) return fail(name, *error);
    request.prompt = move(get<string>(prompt));
    request.checker = options.contains("--check") ? path_from_utf8(options.at("--check")) :
        request.build.source_dir / "acceptance.cmake";
    request.runs_dir = "out/runs";
    request.curl = curl_path(options);
    if (options.contains("--model")) request.model = options.at("--model");

    const auto result = run_repair(request);
    if (const auto* error = get_if<Error>(&result)) return fail(name, *error);
    const auto& run = get<RepairRun>(result);
    const auto path = [](const fs::path& value) {
        const auto text = value.generic_u8string();
        return string(text.begin(), text.end());
    };
    cout << "job=" << run.job.value << " state=" << to_string(run.state) << " attempts=" << run.attempts
              << "\nlog=" << path(run.directory / "gremlin.log") << endl;
    if (run.failure) return fail(name, *run.failure);
    cout << "executable=" << path(run.executable) << "\njunit=" << path(run.evidence->junit) << endl;
    return 0;
}

int dispatch(const vector<string>& args) {
    using namespace gremlin;
    const auto name = invocation(args.empty() ? "" : args.front());
    if (args.size() <= 1 || (args.size() == 2 && (args[1] == "help" || args[1] == "--help"))) {
        cout << "Usage: " << name << " [--help | --version]\n"
                  << "       " << name << " submit DATABASE PROMPT\n"
                  << "       " << name << " jobs status DATABASE ID\n"
                  << "       " << name << " init --source NEW_DIR [--name NAME]\n"
                  << "       " << name << " generate-cmake --source DIR --model NAME:TAG\n"
                  << "          [--prompt TEXT | --spec FILE] [--curl FILE]\n"
                  << "       " << name << " build --trust-source --source DIR --build-dir NEW_DIR\n"
                  << "          --cmake FILE --ctest FILE --ninja FILE --compiler FILE [--profile Debug]\n"
                  << "       " << name << " repair-cmake --trust-source --source DIR --model NAME:TAG\n"
                  << "          --cmake FILE --ctest FILE --ninja FILE --compiler FILE\n"
                  << "          [--initial-source FILE ...] [--max-repairs 0-3] [--profile Debug]\n"
                  << "          [--check FILE] [--prompt TEXT | --spec FILE] [--curl FILE]\n\n"
                  << "submit records an original request as PENDING.\n"
                  << "init writes a C++ starter app and its CMake/CTest files.\n"
                  << "generate-cmake writes a local Ollama proposal for CMakeLists.txt.\n"
                  << "build configures, builds and tests an existing trusted CMake project.\n"
                  << "repair-cmake schedules and repairs a build recipe for trusted C++ files." << endl;
        return 0;
    }
    if (args.size() == 2 && args[1] == "--version") {
        cout << name << ' ' << GREMLIN_VERSION << endl;
        return 0;
    }
    if (args[1] == "build" || args[1] == "init" || args[1] == "generate-cmake" || args[1] == "repair-cmake") {
        try {
            if (args[1] == "generate-cmake") return generate_cmake(name, args);
            if (args[1] == "repair-cmake") return repair_project(name, args);
            return args[1] == "init" ? init_project(name, args) : build_project(name, args);
        } catch (const exception& error) {
            return fail(name, {ErrorCode::internal, error.what()});
        }
    }

    const bool submit = args.size() == 4 && args[1] == "submit";
    const bool status = args.size() == 5 && args[1] == "jobs" && args[2] == "status";
    if (!submit && !status) {
        return fail(name, {ErrorCode::input, "invalid command or arguments. See " + name + " --help"});
    }
    try {
        const auto path = path_from_utf8(args[submit ? 2 : 3]);
        if (!submit && !filesystem::is_regular_file(path)) {
            return fail(name, {ErrorCode::input, "job database does not exist"});
        }
        if (submit && args[3].find_first_not_of(" \t\r\n") == string::npos) {
            return fail(name, {ErrorCode::input, "prompt is empty"});
        }
        auto opened = JobStore::open(path);
        if (const auto* error = get_if<Error>(&opened)) return fail(name, *error);
        auto& store = *get<JobStore::Handle>(opened);
        if (submit) {
            const auto id = JobId::generate();
            const auto result = store.submit(id, args[3]);
            if (const auto* error = get_if<Error>(&result)) return fail(name, *error);
            cout << id.value << " PENDING" << endl;
        } else {
            const auto id = JobId::parse(args[4]);
            if (const auto* error = get_if<Error>(&id)) return fail(name, *error);
            const auto result = store.state(get<JobId>(id));
            if (const auto* error = get_if<Error>(&result)) return fail(name, *error);
            cout << args[4] << ' ' << to_string(get<JobState>(result)) << endl;
        }
    } catch (const exception& error) {
        return fail(name, {ErrorCode::internal, error.what()});
    }
    return 0;
}

}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    vector<string> args;
    for (int i = int(); argv && i < argc; ++i) {
        if (!argv[i]) break;
        const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1, nullptr, 0, nullptr, nullptr);
        if (!size) {
            if (i == 0) { args.emplace_back(); continue; }
            return fail(invocation(args.front()), {gremlin::ErrorCode::input, "invalid UTF-16 argument"});
        }
        string utf8(static_cast<size_t>(size), '\0');
        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1, utf8.data(), size, nullptr, nullptr)) {
            return fail(invocation(args.empty() ? "" : args.front()),
                        {gremlin::ErrorCode::input, "could not encode argument"});
        }
        utf8.pop_back();
        args.emplace_back(move(utf8));
    }
    return dispatch(args);
}
#else
int main(int argc, char** argv) {
    vector<string> args;
    for (int i = int(); argv && i < argc && argv[i]; ++i) args.emplace_back(argv[i]);
    return dispatch(args);
}
#endif
