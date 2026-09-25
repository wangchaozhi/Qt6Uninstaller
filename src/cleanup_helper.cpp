#include <windows.h>
#include <shellapi.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {
std::wstring argumentValue(const std::vector<std::wstring> &arguments, const std::wstring &name)
{
    for (std::size_t i = 0; i + 1 < arguments.size(); ++i) {
        if (arguments[i] == name)
            return arguments[i + 1];
    }
    return {};
}

bool isSafeInstallDirectory(const fs::path &path)
{
    std::error_code error;
    const fs::path absolute = fs::absolute(path, error).lexically_normal();
    if (error || absolute.empty() || absolute == absolute.root_path())
        return false;

    wchar_t windowsDirectory[MAX_PATH]{};
    GetWindowsDirectoryW(windowsDirectory, MAX_PATH);
    wchar_t systemDirectory[MAX_PATH]{};
    GetSystemDirectoryW(systemDirectory, MAX_PATH);

    const auto equalInsensitive = [](const std::wstring &left, const std::wstring &right) {
        return _wcsicmp(left.c_str(), right.c_str()) == 0;
    };
    const bool forbidden =
        equalInsensitive(absolute.wstring(), fs::path(windowsDirectory).lexically_normal().wstring())
        || equalInsensitive(absolute.wstring(), fs::path(systemDirectory).lexically_normal().wstring());
    return !forbidden && fs::is_regular_file(absolute / L"uninstaller.marker", error) && !error;
}

void appendLog(const fs::path &path, const std::string &line)
{
    if (path.empty())
        return;
    std::ofstream stream(path, std::ios::app);
    stream << line << '\n';
}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    int count = 0;
    LPWSTR *rawArguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!rawArguments)
        return 2;

    std::vector<std::wstring> arguments(rawArguments, rawArguments + count);
    LocalFree(rawArguments);

    const std::wstring pidText = argumentValue(arguments, L"--parent-pid");
    const fs::path installDirectory = argumentValue(arguments, L"--install-dir");
    const fs::path helperDirectory = argumentValue(arguments, L"--helper-dir");
    const fs::path logPath = argumentValue(arguments, L"--log");
    if (pidText.empty() || !isSafeInstallDirectory(installDirectory)) {
        appendLog(logPath, "Refused unsafe cleanup request.");
        return 3;
    }

    const DWORD parentPid = static_cast<DWORD>(std::stoul(pidText));
    if (HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, parentPid)) {
        WaitForSingleObject(parent, 30000);
        CloseHandle(parent);
    }

    std::error_code error;
    bool removed = false;
    for (int attempt = 0; attempt < 20; ++attempt) {
        error.clear();
        fs::remove_all(installDirectory, error);
        std::error_code existsError;
        if (!fs::exists(installDirectory, existsError) && !existsError) {
            removed = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    appendLog(logPath, removed ? "Install directory removed." : "Install directory cleanup failed.");

    wchar_t selfPath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, selfPath, MAX_PATH);
    MoveFileExW(selfPath, nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    if (!helperDirectory.empty())
        MoveFileExW(helperDirectory.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    return removed ? 0 : 4;
}
