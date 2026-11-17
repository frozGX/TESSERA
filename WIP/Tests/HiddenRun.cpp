// Runs a program on a desktop of its own, so nothing it shows ever reaches the screen in use.
//   HiddenRun <seconds> <program> [arguments...]
// Gives back the program's exit code, or 124 when it had to be stopped.
#include <windows.h>

#include <cstdio>
#include <cwchar>
#include <string>

int wmain(int count, wchar_t** arguments)
{
    if (count < 3) {
        std::printf("usage: HiddenRun <seconds> <program> [arguments...]\n");
        return 2;
    }
    wchar_t name[] = L"TesseraTestDesktop";
    const HDESK desktop = CreateDesktopW(name, nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    if (!desktop) {
        std::printf("CreateDesktop failed: %lu\n", GetLastError());
        return 2;
    }

    std::wstring command;
    for (int i = 2; i < count; ++i) command += (i > 2 ? L" \"" : L"\"") + std::wstring(arguments[i]) + L"\"";

    STARTUPINFOW startup{ sizeof(startup) };
    startup.lpDesktop = name;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
        std::printf("CreateProcess failed: %lu\n", GetLastError());
        CloseDesktop(desktop);
        return 2;
    }

    DWORD code = 124;
    if (WaitForSingleObject(process.hProcess, static_cast<DWORD>(_wtoi(arguments[1])) * 1000) == WAIT_OBJECT_0) {
        GetExitCodeProcess(process.hProcess, &code);
    } else {
        TerminateProcess(process.hProcess, 124);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseDesktop(desktop);
    return static_cast<int>(code);
}
