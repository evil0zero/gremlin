#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <iostream>
#include <string>

using namespace std;

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 1;
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE read{}, write{};
    if (!CreatePipe(&read, &write, &attributes, 0)) return 2;
    if (!SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(read);
        CloseHandle(write);
        return 3;
    }
    HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &attributes, OPEN_EXISTING, 0, nullptr);
    if (input == INVALID_HANDLE_VALUE) {
        CloseHandle(read);
        CloseHandle(write);
        return 4;
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input;
    startup.hStdOutput = write;
    startup.hStdError = write;
    PROCESS_INFORMATION child{};
    wstring command = LR"("z:\made up\Fàcade.CmD" --help)";
    const BOOL launched = CreateProcessW(argv[1], command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child);
    CloseHandle(input);
    CloseHandle(write);
    if (!launched) {
        CloseHandle(read);
        return 5;
    }
    CloseHandle(child.hThread);
    if (WaitForSingleObject(child.hProcess, 5000) != WAIT_OBJECT_0) {
        TerminateProcess(child.hProcess, 1);
        CloseHandle(child.hProcess);
        CloseHandle(read);
        return 6;
    }
    DWORD exit_code = DWORD();
    if (!GetExitCodeProcess(child.hProcess, &exit_code)) exit_code = 1;
    CloseHandle(child.hProcess);
    string captured;
    char buffer[512]{};
    DWORD size = DWORD();
    while (ReadFile(read, buffer, sizeof(buffer), &size, nullptr) && size) {
        captured.append(buffer, size);
    }
    CloseHandle(read);
    cout << "child exit=" << exit_code << endl << captured;
    return exit_code == 0 && captured.starts_with("Usage: F\xc3\xa0" "cade.CmD ") ? 0 : 7;
}
