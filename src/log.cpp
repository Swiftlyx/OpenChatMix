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
    g_file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
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
