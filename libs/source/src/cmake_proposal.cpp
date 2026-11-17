#include "gremlin/source/cmake_proposal.hpp"
#include "gremlin/platform/files.hpp"

#include <algorithm>
#include <fstream>

using namespace std;

namespace gremlin {
namespace {

namespace fs = filesystem;

struct StagedFile {
    fs::path dir;
    ~StagedFile() {
        error_code ignored;
        fs::remove(dir / "CMakeLists.txt", ignored);
        fs::remove(dir, ignored);
    }
};

}

Result<vector<string>> cmake_inputs(const fs::path& source_dir) {
    try {
        if (!fs::is_directory(source_dir)) {
            return Error{ErrorCode::input, "source must be an existing directory"};
        }
        if (fs::exists(fs::symlink_status(source_dir / "CMakeLists.txt"))) {
            return Error{ErrorCode::input, "source already has CMakeLists.txt"};
        }
        vector<string> sources;
        size_t bytes = size_t();
        for (const auto& entry : fs::directory_iterator(source_dir)) {
            const auto extension = entry.path().extension();
            if (extension != ".cpp" && extension != ".cxx" && extension != ".cc") continue;
            if (!fs::is_regular_file(entry.symlink_status())) continue;
            const auto name = entry.path().filename().u8string();
            bytes += name.size();
            if (bytes > 8192 || sources.size() >= 96) {
                return Error{ErrorCode::input, "source inventory exceeds CMake inference limit"};
            }
            sources.emplace_back(name.begin(), name.end());
        }
        sort(sources.begin(), sources.end());
        return sources;
    } catch (const fs::filesystem_error& error) {
        return Error{ErrorCode::internal, error.what()};
    }
}

Result<fs::path> publish_cmake_proposal(const fs::path& source_dir, string_view contents) {
    if (contents.empty() || contents.size() > 16385) {
        return Error{ErrorCode::input, "expected nonempty bounded CMake text"};
    }
    try {
        const auto directory = fs::canonical(source_dir);
        const auto destination = directory / "CMakeLists.txt";
        const auto staging = directory / (".gremlin-cmake-" + JobId::generate().value);
        if (!fs::create_directory(staging)) {
            return Error{ErrorCode::internal, "cannot reserve a staging directory for the CMake proposal"};
        }
        const StagedFile cleanup{staging};
        const auto staged = staging / "CMakeLists.txt";
        ofstream output(staged, ios::binary);
        output << contents;
        output.close();
        if (!output) return Error{ErrorCode::internal, "cannot write staged CMake proposal"};

        const auto published = publish_new_file(staged, destination);
        if (const auto* error = get_if<Error>(&published)) {
            if (error->code == ErrorCode::input) {
                return Error{ErrorCode::input, "source already has CMakeLists.txt"};
            }
            return *error;
        }
        return destination;
    } catch (const fs::filesystem_error& error) {
        return Error{ErrorCode::internal, error.what()};
    }
}

}
