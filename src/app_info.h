#pragma once

#include <string>
#include <vector>

#include <windows.h>

struct ProcessApp {
    std::wstring exe;   // File name, e.g. "Discord.exe".
    std::wstring path;  // Full image path; may be empty.
    DWORD pid = 0;      // Process that owns an audio session; 0 when unknown.
};

// Human-readable name from the version resource (FileDescription), or the file name without extension.
std::wstring AppDisplayName(const std::wstring& path, const std::wstring& exe);

// Processes that own a visible top-level window, as listed in Alt+Tab.
std::vector<ProcessApp> WindowedApps();

std::wstring FileNameOf(const std::wstring& path);
