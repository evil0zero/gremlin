#include "cmake_reply.hpp"

#include <algorithm>
#include <nlohmann/json.hpp>

using namespace std;

namespace gremlin::inference {
namespace {

Result<nlohmann::json> decode_proposal(string_view response) {
    using nlohmann::json;
    const auto envelope = json::parse(response, nullptr, false);
    if (envelope.is_object() && envelope.contains("done_reason") && envelope["done_reason"] == "length") {
        return Error{ErrorCode::prerequisite, "Ollama exhausted the generation token limit. For generate-project, increase --max-output-tokens and allow sufficient --timeout-seconds"};
    }
    if (!envelope.is_object() || envelope.contains("error") ||
        !envelope.contains("done") || envelope["done"] != true ||
        (envelope.contains("done_reason") && envelope["done_reason"] != "stop") ||
        !envelope.contains("message") || !envelope["message"].is_object()) {
        return Error{ErrorCode::prerequisite, "Ollama did not return a completed chat response"};
    }
    const auto& message = envelope["message"];
    if (!message.contains("role") || message["role"] != "assistant" ||
        !message.contains("content") || !message["content"].is_string() ||
        (message.contains("tool_calls") && message["tool_calls"] != json::array())) {
        return Error{ErrorCode::prerequisite, "Ollama did not return a plain assistant proposal"};
    }
    return json::parse(message["content"].get_ref<const string&>(), nullptr, false);
}

}

Result<string> decode_cmake_reply(string_view response) {
    const auto decoded = decode_proposal(response);
    if (const auto* error = get_if<Error>(&decoded)) return *error;
    const auto& proposal = get<nlohmann::json>(decoded);
    if (!proposal.is_object() || proposal.size() != 1 ||
        !proposal.contains("cmake_lists") || !proposal["cmake_lists"].is_string()) {
        return Error{ErrorCode::prerequisite, "Ollama proposal must contain only the cmake_lists string"};
    }
    auto cmake = proposal["cmake_lists"].get<string>();
    if (cmake.empty() || cmake.size() > 16384 || !cmake.starts_with("cmake_minimum_required(") ||
        cmake.find("```") != string::npos ||
        any_of(cmake.begin(), cmake.end(), [](unsigned char c) {
            return c < 32 && c != '\n' && c != '\r' && c != '\t';
        })) {
        return Error{ErrorCode::prerequisite, "Ollama proposal is not a bounded plain CMake script"};
    }
    if (cmake.back() != '\n') cmake.push_back('\n');
    return cmake;
}

Result<vector<ProjectFile>> decode_project_reply(string_view response) {
    const auto decoded = decode_proposal(response);
    if (const auto* error = get_if<Error>(&decoded)) return *error;
    const auto& proposal = get<nlohmann::json>(decoded);
    const Error malformed{ErrorCode::input, "expected JSON with one files array containing path and content strings"};
    if (!proposal.is_object() || proposal.size() != 1 || !proposal.contains("files") ||
        !proposal["files"].is_array() || proposal["files"].empty() || proposal["files"].size() > 64) return malformed;
    vector<ProjectFile> files;
    for (const auto& file : proposal["files"]) {
        if (!file.is_object() || file.size() != 2 || !file.contains("path") || !file["path"].is_string() ||
            !file.contains("content") || !file["content"].is_string()) return malformed;
        files.push_back({file["path"].get<string>(), file["content"].get<string>()});
    }
    return files;
}

Result<vector<string>> decode_source_reply(string_view response) {
    const auto decoded = decode_proposal(response);
    if (const auto* error = get_if<Error>(&decoded)) return *error;
    const auto& proposal = get<nlohmann::json>(decoded);
    const Error bad{ErrorCode::policy, "Ollama repair proposal must contain only a bounded sources array"};
    if (!proposal.is_object() || proposal.size() != 1 ||
        !proposal.contains("sources") || !proposal["sources"].is_array()) return bad;
    const auto& files = proposal["sources"];
    if (files.empty() || files.size() > 64) return bad;
    vector<string> sources;
    for (const auto& file : files) {
        if (!file.is_string()) return bad;
        const auto& name = file.get_ref<const string&>();
        if (name.empty() || name.size() > 100) return bad;
        sources.push_back(name);
    }
    return sources;
}

}
