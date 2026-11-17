#include "gremlin/source/project.hpp"
#include "gremlin/platform/files.hpp"
#include <fstream>
#include <map>

using namespace std;

namespace gremlin {
namespace {
namespace fs = filesystem;

bool safe_component(string_view part) {
    if (part.empty() || part.size() > 80 || part.front() == '.' || part.back() == '.') return false;
    if (part.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-") != part.npos) return false;
    const auto stem = part.substr(0, part.find('.'));
    return stem != "con" && stem != "prn" && stem != "aux" && stem != "nul" &&
        !(stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) &&
          stem.back() >= '0' && stem.back() <= '9');
}

bool is_test_source(string_view path) {
    const auto extension = path.substr(path.find_last_of('.') + 1);
    return extension == "cpp" || extension == "cc" || extension == "cxx" || extension == "c" ||
        extension == "cmake" || extension == "ps1" || extension == "py" || extension == "sh" ||
        extension == "bat" || extension == "cmd";
}

Result<monostate> validate(span<const ProjectFile> files) {
    if (files.empty() || files.size() > 64) return Error{ErrorCode::input, "expected 1 to 64 project files"};
    map<string, pair<string, bool>> paths;
    size_t total = size_t();
    bool implementation = false;
    bool cmake = false;
    bool tests = false;
    for (const auto& file : files) {
        if (file.path.empty() || file.path.size() > 200 || file.content.size() > 131072 ||
            file.content.find('\0') != string::npos || (total += file.content.size()) > 524288) {
            return Error{ErrorCode::input, "generated file exceeds path or content limits or contains NUL"};
        }
        string folded = file.path;
        for (auto& c : folded) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        size_t begin = size_t();
        for (;;) {
            const auto slash = folded.find('/', begin);
            const auto end = slash == string::npos ? folded.size() : slash;
            if (!safe_component(string_view(folded).substr(begin, end - begin))) {
                return Error{ErrorCode::policy, "unsafe generated path: " + file.path};
            }
            const bool leaf = slash == string::npos;
            const auto spelling = file.path.substr(0, end);
            const auto [it, inserted] = paths.emplace(folded.substr(0, end), pair{spelling, leaf});
            if (!inserted && (leaf || it->second.second || it->second.first != spelling)) {
                return Error{ErrorCode::policy, "duplicate or conflicting generated path: " + file.path};
            }
            if (leaf) break;
            begin = slash + 1;
        }
        if (file.content.find_first_not_of(" \t\r\n") == string::npos) continue;
        if (file.path == "CMakeLists.txt") cmake = true;
        if (folded.starts_with("tests/")) tests = tests || is_test_source(folded);
        else if (folded.ends_with(".cpp") || folded.ends_with(".cc") || folded.ends_with(".cxx")) implementation = true;
    }
    if (!implementation) return Error{ErrorCode::input, "missing implementation: expected a nonempty C++ translation unit outside tests/"};
    if (!cmake) return Error{ErrorCode::input, "missing nonempty root CMakeLists.txt"};
    if (!tests) return Error{ErrorCode::input, "missing a nonempty test source or script under tests/"};
    return monostate{};
}
}

Result<fs::path> publish_project(const fs::path& destination, span<const ProjectFile> files) {
    const auto valid = validate(files);
    if (const auto* error = get_if<Error>(&valid)) return *error;
    try {
        const auto target = fs::absolute(destination).lexically_normal();
        if (!target.has_filename() || fs::exists(fs::symlink_status(target))) {
            return Error{ErrorCode::input, "destination must be a new directory"};
        }
        fs::create_directories(target.parent_path());
        const auto stage = target.parent_path() / (".gremlin-project-" + JobId::generate().value);
        if (!fs::create_directory(stage)) return Error{ErrorCode::internal, "could not reserve project staging directory"};
        struct Cleanup {
            fs::path path;
            ~Cleanup() { error_code ignored; fs::remove_all(path, ignored); }
        } cleanup{stage};
        for (const auto& file : files) {
            const auto path = stage / file.path;
            fs::create_directories(path.parent_path());
            ofstream output(path, ios::binary);
            output.write(file.content.data(), static_cast<streamsize>(file.content.size()));
            output.close();
            if (!output) return Error{ErrorCode::internal, "could not write generated file: " + file.path};
        }
        const auto result = publish_new_directory(stage, target);
        if (const auto* error = get_if<Error>(&result)) return *error;
        return target;
    } catch (const fs::filesystem_error& error) {
        return Error{ErrorCode::internal, error.what()};
    }
}
}
