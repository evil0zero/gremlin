#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "gremlin/platform/files.hpp"
#include "gremlin/platform/process.hpp"

#include <vector>

using namespace std;

namespace gremlin {
namespace {

class Handle {
public:
    explicit Handle(HANDLE value) : value_(value) {}
    ~Handle() { if (value_ && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE get() const { return value_; }
private:
    HANDLE value_;
};

class Attributes {
public:
    bool initialize() {
        SIZE_T bytes = SIZE_T();
        InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        if (!bytes) return false;
        data_.resize(bytes);
        auto* list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(data_.data());
        if (!InitializeProcThreadAttributeList(list, 1, 0, &bytes)) return false;
        list_ = list;
        return true;
    }
    ~Attributes() { if (list_) DeleteProcThreadAttributeList(list_); }
    LPPROC_THREAD_ATTRIBUTE_LIST get() const { return list_; }
private:
    vector<unsigned char> data_;
    LPPROC_THREAD_ATTRIBUTE_LIST list_{};
};

wstring quote(wstring_view arg) {
    wstring quoted(1, L'"');
    size_t slashes = size_t();
    for (const wchar_t c : arg) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        quoted.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        quoted.push_back(c);
        slashes = 0;
    }
    quoted.append(slashes * 2, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

Error platform_error(const char* operation) {
    const DWORD code = GetLastError();
    return {ErrorCode::internal, string(operation) + " failed (Windows error "
        + std::to_string(code) + ')'};
}

}

Result<ProcessExit> run_process(const ProcessRequest& request) {
    if (!request.executable.is_absolute() || request.timeout.count() <= 0 ||
        request.timeout > chrono::hours(24)) {
        return Error{ErrorCode::input, "invalid executable path or process timeout"};
    }
    wstring command = quote(request.executable.native());
    for (const auto& arg : request.arguments) {
        if (arg.find('\0') != string::npos) {
            return Error{ErrorCode::input, "process argument contains NUL"};
        }
        command += L' ';
        command += quote(path_from_utf8(arg).native());
    }

    const Handle job(CreateJobObjectW(nullptr, nullptr));
    if (!job.get()) return platform_error("CreateJobObjectW");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        return platform_error("SetInformationJobObject");
    }

    SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
    const Handle output(CreateFileW(request.log.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &inherit, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (output.get() == INVALID_HANDLE_VALUE) return platform_error("open process log");
    const Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &inherit, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (input.get() == INVALID_HANDLE_VALUE) return platform_error("open NUL");

    Attributes attributes;
    if (!attributes.initialize()) return platform_error("InitializeProcThreadAttributeList");
    HANDLE inherited[] = {input.get(), output.get()};
    if (!UpdateProcThreadAttribute(attributes.get(), 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            inherited, sizeof(inherited), nullptr, nullptr)) {
        return platform_error("UpdateProcThreadAttribute");
    }
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = input.get();
    startup.StartupInfo.hStdOutput = output.get();
    startup.StartupInfo.hStdError = output.get();
    startup.lpAttributeList = attributes.get();
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(request.executable.c_str(), command.data(), nullptr, nullptr, TRUE,
            EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED | CREATE_NO_WINDOW,
            nullptr, nullptr, &startup.StartupInfo, &child)) {
        return platform_error("CreateProcessW");
    }
    const Handle process(child.hProcess);
    const Handle thread(child.hThread);
    if (!AssignProcessToJobObject(job.get(), process.get())) {
        const auto error = platform_error("AssignProcessToJobObject");
        TerminateProcess(process.get(), 1);
        return error;
    }
    if (ResumeThread(thread.get()) == static_cast<DWORD>(-1)) return platform_error("ResumeThread");

    const auto timeout_ms = chrono::duration_cast<chrono::milliseconds>(request.timeout).count();
    const DWORD waited = WaitForSingleObject(process.get(), static_cast<DWORD>(timeout_ms));
    if (waited == WAIT_TIMEOUT) {
        if (!TerminateJobObject(job.get(), 124)) return platform_error("TerminateJobObject");
        WaitForSingleObject(process.get(), 5000);
        return ProcessExit{124, true};
    }
    if (waited != WAIT_OBJECT_0) return platform_error("WaitForSingleObject");
    DWORD code = DWORD();
    if (!GetExitCodeProcess(process.get(), &code)) return platform_error("GetExitCodeProcess");
    return ProcessExit{static_cast<uint32_t>(code), false};
}

}
