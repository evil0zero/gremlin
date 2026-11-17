#include "gremlin/source/cmake_proposal.hpp"

#include <fstream>
#include <iostream>
#include <iterator>

using namespace std;

int main() {
    namespace fs = filesystem;
    using namespace gremlin;

    const auto dir = fs::path("proposal-" + JobId::generate().value);
    if (!fs::create_directory(dir)) return 1;
    ofstream(dir / "two.cc") << "// test source";
    ofstream(dir / "one.cpp") << "// test source";
    ofstream(dir / "ProjectPrompt.md") << "Create a local C++ project.";
    const auto files = cmake_inputs(dir);
    if (files.index() || get<vector<string>>(files) !=
            vector<string>{"one.cpp", "two.cc"}) return 2;

    const string proposal = "cmake_minimum_required(VERSION 3.21)\n";
    const auto published = publish_cmake_proposal(dir, proposal);
    if (const auto* error = get_if<Error>(&published)) {
        cerr << error->message << '\n';
        return 3;
    }
    if (!cmake_inputs(dir).index() || !publish_cmake_proposal(dir, "replacement").index()) return 4;
    ifstream saved(dir / "CMakeLists.txt", ios::binary);
    const string text(istreambuf_iterator<char>{saved}, {});
    saved.close();
    if (text != proposal) return 5;

    for (const auto* file : {"CMakeLists.txt", "one.cpp", "two.cc", "ProjectPrompt.md"}) {
        if (!fs::remove(dir / file)) return 6;
    }
    return fs::remove(dir) ? 0 : 7;
}
