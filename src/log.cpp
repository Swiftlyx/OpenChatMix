#include "log.h"

#include <windows.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>

namespace {

SRWLOCK g_lock = SRWLOCK_INIT;
HANDLE g_file = INVALID_HANDLE_VALUE;

}  // namespace

void LogOpen(const std::wstring& path) {
    LogClose();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE && GetLastError() == ERROR_SHARING_VIOLATION) {
        // An instance that is still ending holds the file; this one logs beside it, e.g. OpenChatMix-1234.log.
        const size_t dot = path.rfind(L'.');
        const std::wstring alternate =
            path.substr(0, dot) + L"-" + std::to_wstring(GetCurrentProcessId()) + path.substr(dot);
        file = CreateFileW(alternate.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    g_file = file;
}

void LogClose() {
    AcquireSRWLockExclusive(&g_lock);
    if (g_file != INVALID_HANDLE_VALUE) {
        CloseHandle(g_file);
        g_file = INVALID_HANDLE_VALUE;
    }
    ReleaseSRWLockExclusive(&g_lock);
}

void Log(const char* format, ...) {
    if (g_file == INVALID_HANDLE_VALUE) {
        return;
    }

    // Text area plus room for CRLF; the printf calls below always leave a terminator inside it.
    constexpr size_t kTextSize = 1022;
    char line[kTextSize + 2];
    SYSTEMTIME now;
    GetLocalTime(&now);
    const int stamp = std::snprintf(line, kTextSize, "%02u:%02u:%02u.%03u ", now.wHour, now.wMinute, now.wSecond,
                                    now.wMilliseconds);
    size_t length = stamp > 0 ? static_cast<size_t>(stamp) : 0;

    va_list args;
    va_start(args, format);
    const int body = std::vsnprintf(line + length, kTextSize - length, format, args);
    va_end(args);

    // vsnprintf returns the untruncated length; the stored text ends before the terminator.
    length = std::min(length + (body > 0 ? static_cast<size_t>(body) : 0), kTextSize - 1);
    line[length++] = '\r';
    line[length++] = '\n';

    AcquireSRWLockExclusive(&g_lock);
    if (g_file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(g_file, line, static_cast<DWORD>(length), &written, nullptr);
    }
    ReleaseSRWLockExclusive(&g_lock);
}
