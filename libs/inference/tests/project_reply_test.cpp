#include "cmake_reply.hpp"
#include "gremlin/source/project.hpp"
#include <fstream>
#include <nlohmann/json.hpp>

using namespace std;
using namespace gremlin;
using nlohmann::json;

int main() {
    const auto root = filesystem::path("generation-" + JobId::generate().value);
    filesystem::create_directory(root);
    ofstream(root / "ProjectPrompt.md") << "Print fixture output." << endl;
    const auto destination = root / "project";
    json files = json::array({
        {{"path","main.cpp"},{"content","#include \"value.hpp\"\nint main(){return value();}\n"}},
        {{"path","value.hpp"},{"content","inline int value(){return 0;}\n"}},
        {{"path","CMakeLists.txt"},{"content","cmake_minimum_required(VERSION 3.21)\n"}},
        {{"path","tests/check.cmake"},{"content","execute_process(COMMAND \"${APP}\")\n"}}
    });
    const auto decode = [](const json& payload) {
        return inference::decode_project_reply(json{{"done",true},{"message",
            {{"role","assistant"},{"content",payload.dump()}}}}.dump());
    };
    if (!decode(json{{"files",json::array({1})}}).index() || !inference::decode_project_reply("garbage").index()) return 1;
    auto incomplete = files;
    incomplete.erase(0);
    incomplete.erase(0);
    auto decoded = decode(json{{"files",incomplete}});
    if (decoded.index()) return 2;
    auto absent = publish_project(destination, get<vector<ProjectFile>>(decoded));
    if (!absent.index() || get<Error>(absent).message.find("missing implementation") == string::npos ||
        filesystem::exists(destination)) return 3;
    auto fixture_only = files;
    fixture_only[3]["path"] = "tests/README.md";
    decoded = decode(json{{"files",fixture_only}});
    if (decoded.index()) return 8;
    absent = publish_project(destination, get<vector<ProjectFile>>(decoded));
    if (!absent.index() || get<Error>(absent).message.find("test source or script") == string::npos ||
        filesystem::exists(destination)) return 9;
    for (const auto* path : {"/absolute.cpp", "../escape.cpp", "AUX.txt", "main.cpp:stream",
                             "MAIN.cpp", "main.cpp", "C:/drive", "Tests/other.cpp", "tests/check.cmake/child"}) {
        auto invalid = files;
        invalid.push_back({{"path",path},{"content","x"}});
        decoded = decode(json{{"files",invalid}});
        if (decoded.index() || !publish_project(destination, get<vector<ProjectFile>>(decoded)).index() ||
            filesystem::exists(destination)) return 4;
    }
    decoded = decode(json{{"files",files}});
    if (decoded.index()) return 5;
    const auto& generated = get<vector<ProjectFile>>(decoded);
    if (publish_project(destination, generated).index() || !publish_project(destination, generated).index()) return 6;
    for (const auto& file : generated) {
        ifstream input(destination / file.path, ios::binary);
        const string actual((istreambuf_iterator<char>(input)), {});
        if (actual != file.content) return 7;
    }
}
