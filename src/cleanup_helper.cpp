#include <windows.h>
#include <shellapi.h>

#include <algorithm>
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

bool scheduleTreeForReboot(const fs::path &root, const fs::path &logPath)
{
    std::error_code error;
    if (!fs::exists(root, error))
        return !error;

    std::vector<fs::path> entries;
    fs::recursive_directory_iterator iterator(
        root, fs::directory_options::skip_permission_denied, error);
    const fs::recursive_directory_iterator end;
    while (!error && iterator != end) {
        entries.push_back(iterator->path());
        iterator.increment(error);
    }
    if (error) {
        appendLog(logPath, "Could not enumerate all remaining files for reboot cleanup.");
        return false;
    }

    std::sort(entries.begin(), entries.end(), [](const fs::path &left, const fs::path &right) {
        return left.native().size() > right.native().size();
    });

    bool scheduled = true;
    for (const fs::path &entry : entries) {
        if (!MoveFileExW(entry.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT))
            scheduled = false;
    }
    if (!MoveFileExW(root.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT))
        scheduled = false;

    appendLog(logPath, scheduled
        ? "Locked leftovers scheduled for removal after restart."
        : "Failed to schedule every locked leftover for restart cleanup.");
    return scheduled;
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

    DWORD parentPid = 0;
    try {
        parentPid = static_cast<DWORD>(std::stoul(pidText));
    } catch (...) {
        appendLog(logPath, "Invalid parent process identifier.");
        return 3;
    }
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

    bool cleanupSucceeded = removed;
    if (removed) {
        appendLog(logPath, "Install directory removed.");
    } else {
        appendLog(logPath, "Immediate cleanup was incomplete.");
        cleanupSucceeded = scheduleTreeForReboot(installDirectory, logPath);
    }

    wchar_t selfPath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, selfPath, MAX_PATH);
    MoveFileExW(selfPath, nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    if (!helperDirectory.empty())
        MoveFileExW(helperDirectory.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    return cleanupSucceeded ? 0 : 4;
}
