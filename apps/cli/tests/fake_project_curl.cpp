#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <nlohmann/json.hpp>

using namespace std;
using nlohmann::json;
namespace fs = filesystem;

int main(int argc, char** argv) {
    if (argc < 2) return 1;
    const string_view url = argv[argc - 1];
    fs::path output_path;
    fs::path request_path;
    string timeout;
    for (int i = 1; i + 1 < argc; ++i) {
        const string_view option = argv[i];
        if (option == "--output") output_path = argv[++i];
        if (option == "--max-time") timeout = argv[++i];
        if (option == "--data-binary") {
            const string_view data = argv[++i];
            if (!data.starts_with('@')) return 2;
            request_path = data.substr(1);
        }
    }

    constexpr auto model = "fixture:local";
    constexpr auto endpoint = "http://127.0.0.1:11434/api/";
    if (output_path.empty() || !url.starts_with(endpoint)) return 3;
    const auto operation = url.substr(string_view(endpoint).size());
    json response;
    if (operation == "tags") {
        response = {{"models", json::array({{{"name", model}}})}};
    } else {
        ifstream request(request_path, ios::binary);
        const auto payload = json::parse(request, nullptr, false);
        if (!payload.is_object() || payload.value("model", "") != model) return 4;
        if (operation == "show") {
            const auto blob = fs::absolute(fs::path("../provider") / ("sha256-" + string(64, 'a')));
            const auto path = blob.generic_u8string();
            response = {{"modelfile", "FROM " + string(path.begin(), path.end()) + "\n"}};
        } else if (operation == "chat") {
            ifstream data("../provider/project_fixture.json", ios::binary);
            const auto fixture = json::parse(data);
            ifstream spec("Input Spec.md", ios::binary);
            const string specification((istreambuf_iterator<char>(spec)), {});
            const auto& message = payload.at("messages").back();
            if (!spec || message.at("role") != "user" || message.at("content") != specification) {
                cerr << "The chat request did not contain the exact --spec contents" << endl;
                return 5;
            }
            const auto context = payload.at("options").at("num_ctx").get<size_t>();
            const auto output_tokens = payload.at("options").at("num_predict").get<size_t>();
            size_t expected_output_tokens = size_t(12288);
            if (fs::exists("expected-output-tokens.txt")) {
                ifstream expected("expected-output-tokens.txt");
                expected >> expected_output_tokens;
                if (!expected) return 11;
            }
            if (output_tokens != expected_output_tokens) {
                cerr << "Expected output token budget " << expected_output_tokens << ", got " << output_tokens << endl;
                return 12;
            }
            if (context < specification.size() + output_tokens + 2048 || context > 106496) {
                cerr << "The chat context does not cover the specification and output budget" << endl;
                return 8;
            }
            string expected_timeout = "900";
            if (fs::exists("expected-timeout.txt")) {
                ifstream expected("expected-timeout.txt");
                expected >> expected_timeout;
                if (!expected) return 9;
            }
            if (timeout != expected_timeout) {
                cerr << "Expected chat timeout " << expected_timeout << ", got " << timeout << endl;
                return 10;
            }
            if (fs::exists("simulate-timeout.txt")) {
                cerr << "curl: (28) Simulated request timeout" << endl;
                cout << "http=000" << endl;
                return 28;
            }
            const json reply = {{"files", fixture.at("files")}};
            response = {{"done", true}, {"done_reason", "stop"},
                {"message", {{"role", "assistant"}, {"content", reply.dump()}}}};
        } else {
            return 6;
        }
    }

    ofstream output(output_path, ios::binary);
    output << response.dump();
    output.close();
    if (!output) return 7;
    cout << "http=200" << endl;
}
