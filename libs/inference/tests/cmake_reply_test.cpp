#include "cmake_reply.hpp"

using namespace std;

int main() {
    using gremlin::inference::decode_cmake_reply;
    const auto good = decode_cmake_reply(R"json({"done":true,"done_reason":"stop","message":{
        "role":"assistant","content":"{\"cmake_lists\":\"cmake_minimum_required(VERSION 3.21)\"}"}})json");
    if (good.index() || get<string>(good) != "cmake_minimum_required(VERSION 3.21)\n") return 1;
    for (const auto* invalid : {
        "not json",
        R"json({"done":false})json",
        R"json({"done":true,"done_reason":"length","message":{"role":"assistant","content":"{}"}})json",
        R"json({"done":true,"message":{"role":"assistant","content":"{\"cmake_lists\":\"cmake_minimum_required(VERSION 3.21)\",\"file\":\"unexpected\"}"}})json",
        R"json({"done":true,"message":{"role":"assistant","content":"{\"cmake_lists\":\"cmake_minimum_required(VERSION 3.21)\\u0000\"}"}})json",
        R"json({"done":true,"message":{"role":"assistant","content":"{}","tool_calls":[{}]}})json"
    }) {
        if (!decode_cmake_reply(invalid).index()) return 2;
    }
    using gremlin::inference::decode_source_reply;
    const auto repair = decode_source_reply(R"json({"done":true,"message":{
        "role":"assistant","content":"{\"sources\":[\"main.cpp\",\"count.cpp\"]}"}})json");
    if (repair.index() || get<vector<string>>(repair) !=
            vector<string>{"main.cpp", "count.cpp"}) return 3;
    if (!decode_source_reply(R"json({"done":true,"message":{
        "role":"assistant","content":"{\"sources\":[],\"command\":\"run\"}"}})json").index()) return 4;
}
