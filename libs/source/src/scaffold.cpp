#include "gremlin/source/scaffold.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <utility>

using namespace std;

namespace gremlin {
namespace {

namespace fs = filesystem;

bool ascii_letter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool valid_name(string_view name) {
    if (name.empty() || name.size() > 64 || !ascii_letter(name.front()) ||
        !all_of(name.begin(), name.end(), [](char c) {
            return ascii_letter(c) || (c >= '0' && c <= '9') || c == '_' || c == '-';
        })) return false;

    string upper(name);
    for (char& c : upper) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    return upper != "CON" && upper != "PRN" && upper != "AUX" && upper != "NUL" &&
        !(upper.size() == 4 && (upper.starts_with("COM") || upper.starts_with("LPT")) &&
          upper.back() >= '0' && upper.back() <= '9');
}

string cmake_script(string_view name) {
    return R"cmake(cmake_minimum_required(VERSION 3.21)
project(GremlinStarter LANGUAGES CXX)

add_executable(starter_app main.cpp)
target_compile_features(starter_app PRIVATE cxx_std_20)
set_target_properties(starter_app PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin"
    OUTPUT_NAME ")cmake" + string(name) + R"cmake(")

include(CTest)
if(BUILD_TESTING)
    add_test(NAME starter.output COMMAND "${CMAKE_COMMAND}"
        "-DAPP=$<TARGET_FILE:starter_app>" -P "${CMAKE_CURRENT_SOURCE_DIR}/check.cmake")
endif()
)cmake";
}

constexpr auto main_source = R"cpp(#include <iostream>

using namespace std;

int main() {
    cout << "sweet nothings" << endl;
}
)cpp";

constexpr auto output_check = R"cmake(execute_process(COMMAND "${APP}" RESULT_VARIABLE code OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT code STREQUAL "0" OR NOT output STREQUAL "sweet nothings\n" OR NOT error STREQUAL "")
    message(FATAL_ERROR "Unexpected exit or output (${code}): ${output}${error}")
endif()
)cmake";

}

Result<fs::path> create_starter_project(const fs::path& source_dir, string_view executable_name) {
    if (source_dir.empty() || !valid_name(executable_name)) {
        return Error{ErrorCode::input, "expected a new source directory and a portable executable name "
            "starting with a letter and using at most 64 letters, digits, underscores or hyphens"};
    }

    try {
        auto directory = fs::absolute(source_dir);
        if (directory.has_relative_path() && !directory.has_filename()) directory = directory.parent_path();
        const array<pair<fs::path, string>, 3> files{{
            {directory / "CMakeLists.txt", cmake_script(executable_name)},
            {directory / "main.cpp", main_source},
            {directory / "check.cmake", output_check}
        }};
        fs::create_directories(directory.parent_path());
        error_code creation_error;
        if (!fs::create_directory(directory, creation_error)) {
            if (!creation_error || creation_error == errc::file_exists) {
                return Error{ErrorCode::input, "source path already exists. Choose a fresh directory"};
            }
            return Error{ErrorCode::internal, "cannot create source directory: " + creation_error.message()};
        }

        for (const auto& [path, contents] : files) {
            ofstream file(path, ios::binary);
            file << contents;
            file.close();
            if (!file) {
                error_code ignored;
                for (const auto& entry : files) fs::remove(entry.first, ignored);
                fs::remove(directory, ignored);
                return Error{ErrorCode::internal, "could not finish writing the starter project"};
            }
        }
        return directory;
    } catch (const fs::filesystem_error& error) {
        return Error{ErrorCode::internal, error.what()};
    }
}

}
