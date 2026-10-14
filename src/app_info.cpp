#include "app_info.h"

#include <windows.h>

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

}  // namespace

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
