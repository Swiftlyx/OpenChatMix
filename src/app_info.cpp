#include "app_info.h"

#include <windows.h>

#include <appmodel.h>
#include <dwmapi.h>

#include <algorithm>

namespace {

std::wstring QueryVersionString(const std::vector<BYTE>& data, const wchar_t* field) {
    struct Translation {
        WORD language;
        WORD codePage;
    };
    Translation* translations = nullptr;
    UINT size = 0;
    if (!VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translations), &size) ||
        size < sizeof(Translation)) {
        return {};
    }
    wchar_t query[128];
    swprintf_s(query, L"\\StringFileInfo\\%04x%04x\\%s", translations[0].language, translations[0].codePage, field);
    wchar_t* value = nullptr;
    if (!VerQueryValueW(data.data(), query, reinterpret_cast<void**>(&value), &size) || size == 0) {
        return {};
    }
    std::wstring text(value);
    text.erase(text.find_last_not_of(L" \t") + 1);
    return text;
}

std::wstring ProcessPath(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) {
        return {};
    }
    std::wstring path(MAX_PATH * 2, L'\0');
    DWORD length = static_cast<DWORD>(path.size());
    if (QueryFullProcessImageNameW(process, 0, path.data(), &length)) {
        path.resize(length);
    } else {
        path.clear();
    }
    CloseHandle(process);
    return path;
}

bool IsAltTabWindow(HWND window) {
    if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER) ||
        (GetWindowLongW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) || GetWindowTextLengthW(window) == 0) {
        return false;
    }
    // Suspended UWP frames and windows on other virtual desktops are cloaked.
    DWORD cloaked = 0;
    DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    return cloaked == 0;
}

bool FileExists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

// Discord's launcher in its install folder, or the newest app-<version> folder when the launcher is missing.
std::wstring DiscordPath() {
    wchar_t local[MAX_PATH];
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }
    const std::wstring root = std::wstring(local) + L"\\Discord\\";
    if (FileExists(root + L"Discord.exe")) {
        return root + L"Discord.exe";
    }
    std::wstring newest;
    FILETIME newestTime{};
    WIN32_FIND_DATAW found{};
    HANDLE search = FindFirstFileW((root + L"app-*").c_str(), &found);
    if (search == INVALID_HANDLE_VALUE) {
        return {};
    }
    do {
        const std::wstring path = root + found.cFileName + L"\\Discord.exe";
        if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && FileExists(path) &&
            (newest.empty() || CompareFileTime(&found.ftCreationTime, &newestTime) > 0)) {
            newest = path;
            newestTime = found.ftCreationTime;
        }
    } while (FindNextFileW(search, &found));
    FindClose(search);
    return newest;
}

// The file inside an installed package of the given family, for the current user.
std::wstring PackageFile(const wchar_t* family, const std::wstring& file) {
    UINT32 count = 0;
    UINT32 bufferLength = 0;
    if (GetPackagesByPackageFamily(family, &count, nullptr, &bufferLength, nullptr) != ERROR_INSUFFICIENT_BUFFER ||
        count == 0) {
        return {};
    }
    std::vector<PWSTR> names(count);
    std::vector<wchar_t> buffer(bufferLength);
    if (GetPackagesByPackageFamily(family, &count, names.data(), &bufferLength, buffer.data()) != ERROR_SUCCESS) {
        return {};
    }
    for (UINT32 index = 0; index < count; ++index) {
        UINT32 pathLength = 0;
        if (GetPackagePathByFullName(names[index], &pathLength, nullptr) != ERROR_INSUFFICIENT_BUFFER) {
            continue;
        }
        std::wstring root(pathLength, L'\0');
        if (GetPackagePathByFullName(names[index], &pathLength, root.data()) != ERROR_SUCCESS) {
            continue;
        }
        root.resize(wcslen(root.c_str()));
        const std::wstring path = root + L"\\" + file;
        if (FileExists(path)) {
            return path;
        }
    }
    return {};
}

}  // namespace

std::wstring InstalledAppPath(const std::wstring& exe) {
    if (_wcsicmp(exe.c_str(), L"Discord.exe") == 0) {
        return DiscordPath();
    }
    if (_wcsicmp(exe.c_str(), L"XboxPartyChatHost.exe") == 0) {
        // The party chat host ships with the Xbox app; older systems had it in the Game Bar package.
        for (const wchar_t* family : {L"Microsoft.GamingApp_8wekyb3d8bbwe", L"Microsoft.XboxGamingOverlay_8wekyb3d8bbwe"}) {
            if (std::wstring path = PackageFile(family, exe); !path.empty()) {
                return path;
            }
        }
    }
    return {};
}

std::wstring FileNameOf(const std::wstring& path) {
    return path.substr(path.find_last_of(L"\\/") + 1);
}

std::wstring AppDisplayName(const std::wstring& path, const std::wstring& exe) {
    if (!path.empty()) {
        DWORD ignored = 0;
        const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
        if (size > 0) {
            std::vector<BYTE> data(size);
            if (GetFileVersionInfoW(path.c_str(), 0, size, data.data())) {
                std::wstring name = QueryVersionString(data, L"FileDescription");
                if (name.empty()) {
                    name = QueryVersionString(data, L"ProductName");
                }
                if (!name.empty()) {
                    return name;
                }
            }
        }
    }
    const std::wstring file = exe.empty() ? FileNameOf(path) : exe;
    const size_t dot = file.find_last_of(L'.');
    return dot == std::wstring::npos ? file : file.substr(0, dot);
}

std::vector<ProcessApp> WindowedApps() {
    std::vector<DWORD> pids;
    EnumWindows(
        [](HWND window, LPARAM param) -> BOOL {
            if (IsAltTabWindow(window)) {
                DWORD pid = 0;
                GetWindowThreadProcessId(window, &pid);
                reinterpret_cast<std::vector<DWORD>*>(param)->push_back(pid);
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&pids));

    std::sort(pids.begin(), pids.end());
    pids.erase(std::unique(pids.begin(), pids.end()), pids.end());

    const DWORD self = GetCurrentProcessId();
    std::vector<ProcessApp> apps;
    for (const DWORD pid : pids) {
        if (pid == self) {
            continue;
        }
        const std::wstring path = ProcessPath(pid);
        if (path.empty()) {
            continue;
        }
        const std::wstring exe = FileNameOf(path);
        const bool known = std::any_of(apps.begin(), apps.end(),
                                       [&](const ProcessApp& app) { return _wcsicmp(app.exe.c_str(), exe.c_str()) == 0; });
        if (!known) {
            apps.push_back({exe, path});
        }
    }
    return apps;
}

void OpenWithExplorer(const std::wstring& target) {
    std::wstring command = L"explorer.exe \"" + target + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
}
