#include "gremlin/logging/jsonl.hpp"

#include <fstream>
#include <iterator>

using namespace std;

int main() {
    using namespace gremlin;
    const auto id = JobId::generate();
    const auto path = filesystem::path("events-" + id.value + ".jsonl");
    JsonlSink sink(path, 200);
    Event event{id, "state", "submitted", id.value, {}};
    if (sink.emit(event).index()) return 1;
    event.action = "started";
    if (sink.emit(event).index()) return 2;
    event.action = "unsafe\nlabel";
    if (!sink.emit(event).index()) return 3;
    auto previous = path;
    previous += ".1";
    const string start = "{\"unix_ms\":0,\"job_id\":\"" + id.value + "\",\"component\":\"state\",\"action\":\"";
    const string end = "\",\"correlation_id\":\"" + id.value + "\"}\n";
    for (const auto& [file, action] : {pair{path, "started"}, pair{previous, "submitted"}}) {
        ifstream input(file, ios::binary);
        const string contents(istreambuf_iterator<char>{input}, {});
        if (contents != start + action + end) return 4;
    }
    return filesystem::remove(path) && filesystem::remove(previous) ? 0 : 5;
}
