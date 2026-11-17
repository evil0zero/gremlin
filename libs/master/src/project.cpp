#include "project.hpp"

#include <algorithm>
#include <fstream>

using namespace std;

namespace gremlin::master {
namespace {

namespace fs = filesystem;
constexpr uintmax_t file_limit = 1024 * 1024;

bool source_extension(const fs::path& extension) {
    return extension == ".cpp" || extension == ".cxx" || extension == ".cc";
}

bool header_extension(const fs::path& extension) {
    return extension == ".hpp" || extension == ".hxx" || extension == ".hh" || extension == ".h";
}

bool safe_name(const string& filename) {
    return !filename.empty() && filename.size() <= 100 && filename.front() != '.' &&
        all_of(filename.begin(), filename.end(), [](unsigned char c) {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                c == '-' || c == '_' || c == '.';
        });
}

}

Result<ProjectSnapshot> snapshot_project(const RepairRequest& request, const fs::path& directory) {
    const auto source = fs::canonical(request.build.source_dir);
    if (!fs::is_regular_file(fs::symlink_status(request.checker)) || fs::file_size(request.checker) > file_limit) {
        return Error{ErrorCode::input, "checker must be a regular local file of at most 1 MiB"};
    }
    if (!fs::create_directory(directory)) return Error{ErrorCode::internal, "input snapshot already exists"};
    ProjectSnapshot snapshot{directory, {}};
    size_t files = size_t();
    for (const auto& entry : fs::directory_iterator(source)) {
        const auto extension = entry.path().extension();
        const bool translation_unit = source_extension(extension);
        if (!translation_unit && !header_extension(extension)) continue;
        const auto basename = entry.path().filename().u8string();
        const string name(basename.begin(), basename.end());
        if (!safe_name(name) || !fs::is_regular_file(entry.symlink_status()) ||
            fs::file_size(entry.path()) > file_limit || ++files > 64) {
            return Error{ErrorCode::input, "repair inputs must be at most 64 regular, portably named C++ files of at most 1 MiB each"};
        }
        fs::copy_file(entry.path(), directory / name);
        if (translation_unit) snapshot.sources.push_back(name);
    }
    if (snapshot.sources.empty()) return Error{ErrorCode::input, "project has no top-level C++ implementation files"};
    fs::copy_file(request.checker, directory / "acceptance.cmake");
    sort(snapshot.sources.begin(), snapshot.sources.end());
    return snapshot;
}

bool valid_selection(const vector<string>& selection, const ProjectSnapshot& snapshot) {
    return !selection.empty() && selection.size() <= snapshot.sources.size() &&
        adjacent_find(selection.begin(), selection.end()) == selection.end() &&
        includes(snapshot.sources.begin(), snapshot.sources.end(), selection.begin(), selection.end());
}

Result<monostate> stage_project(const ProjectSnapshot& snapshot, const vector<string>& selection,
        const fs::path& destination) {
    if (!valid_selection(selection, snapshot)) {
        return Error{ErrorCode::policy, "repair attempted to select files outside the immutable source inventory"};
    }
    if (!fs::create_directories(destination)) return Error{ErrorCode::internal, "revision directory already exists"};
    for (const auto& file : fs::directory_iterator(snapshot.directory)) {
        fs::copy_file(file.path(), destination / file.path().filename());
    }
    ofstream cmake(destination / "CMakeLists.txt", ios::binary);
    cmake << "cmake_minimum_required(VERSION 3.21)\nproject(GremlinRepair LANGUAGES CXX)\n"
             "add_executable(repair_app";
    for (const auto& file : selection) cmake << "\n    \"" << file << '"';
    cmake << R"cmake()
target_compile_features(repair_app PRIVATE cxx_std_20)
set_target_properties(repair_app PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")
include(CTest)
add_test(NAME trusted_acceptance COMMAND "${CMAKE_COMMAND}"
    "-DAPP=$<TARGET_FILE:repair_app>" "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/acceptance.cmake")
)cmake";
    cmake.close();
    if (!cmake) return Error{ErrorCode::internal, "cannot write the staged CMake recipe"};
    return monostate{};
}

}
