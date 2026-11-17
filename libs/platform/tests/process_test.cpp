#include "gremlin/platform/process.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <thread>

using namespace std;

#ifdef _WIN32
#define ENTRY_POINT wmain
#define ARG_CHAR wchar_t
#else
#define ENTRY_POINT main
#define ARG_CHAR char
#endif

int ENTRY_POINT(int argc, ARG_CHAR** argv) {
    using namespace gremlin;
    using namespace chrono_literals;
    const vector<string> arguments{"--child", "", "two words", R"(quote"and trailing\)",
        "ampersand & pipe | angle >"};
    if (argc > 1) {
        if (filesystem::path(argv[1]) == "--timeout") {
            cout << "started" << endl;
            this_thread::sleep_for(30s);
            return 1;
        }
        if (static_cast<size_t>(argc - 1) != arguments.size()) return 2;
        for (int i = 1; i < argc; ++i) {
            if (filesystem::path(argv[i]) != arguments[i - 1]) return 3;
        }
        cout << "stdout\n" << flush;
        cerr << "stderr\n";
        return 17;
    }

    const auto executable = filesystem::absolute(argv[0]);
    const auto log = filesystem::absolute("process-" + JobId::generate().value + ".log");
    ofstream(log).close();
    const auto result = run_process({executable, arguments, log, 5s});
    const auto* completed = get_if<ProcessExit>(&result);
    if (!completed || completed->timed_out || completed->code != 17) return 4;
    const auto timeout = run_process({executable, {"--timeout"}, log, 1s});
    const auto* stopped = get_if<ProcessExit>(&timeout);
    if (!stopped || !stopped->timed_out || stopped->code != 124) return 5;
    ifstream file(log);
    const string output(istreambuf_iterator<char>{file}, {});
    file.close();
    if (output != "stdout\nstderr\nstarted\n") return 6;
    return filesystem::remove(log) ? 0 : 7;
}
