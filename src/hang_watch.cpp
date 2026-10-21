#include "hang_watch.h"

#include <dbghelp.h>

#include <cwchar>

#include "log.h"

namespace {

constexpr DWORD kCheckMs = 2000;
constexpr UINT kHangMs = 10000;
constexpr DWORD kStopTimeoutMs = 3000;

}  // namespace

HangWatch::~HangWatch() {
    Stop();
}

void HangWatch::Start(HWND window, const std::wstring& directory) {
    Stop();
    window_ = window;
    directory_ = directory;
    stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    thread_ = stop_ ? CreateThread(nullptr, 0, WatchProc, this, 0, nullptr) : nullptr;
}

void HangWatch::Stop() {
    if (thread_) {
        SetEvent(stop_);
        // The watch may be waiting for this thread to answer its check, so sent messages are handled meanwhile.
        const ULONGLONG deadline = GetTickCount64() + kStopTimeoutMs;
        for (;;) {
            const ULONGLONG now = GetTickCount64();
            const DWORD left = now < deadline ? static_cast<DWORD>(deadline - now) : 0;
            const DWORD wait = MsgWaitForMultipleObjectsEx(1, &thread_, left, QS_SENDMESSAGE, 0);
            if (wait != WAIT_OBJECT_0 + 1) {
                break;
            }
            MSG message;
            PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE);
        }
        CloseHandle(thread_);
        thread_ = nullptr;
    }
    if (stop_) {
        CloseHandle(stop_);
        stop_ = nullptr;
    }
}

DWORD WINAPI HangWatch::WatchProc(void* self) {
    static_cast<HangWatch*>(self)->Watch();
    return 0;
}

void HangWatch::Watch() {
    bool hung = false;
    while (WaitForSingleObject(stop_, kCheckMs) == WAIT_TIMEOUT) {
        DWORD_PTR result = 0;
        const ULONGLONG sent = GetTickCount64();
        if (SendMessageTimeoutW(window_, WM_NULL, 0, 0, SMTO_BLOCK, kHangMs, &result)) {
            if (hung) {
                Log("window thread answers again after %llu ms", GetTickCount64() - sent);
                hung = false;
            }
            continue;
        }
        if (GetLastError() != ERROR_TIMEOUT || WaitForSingleObject(stop_, 0) == WAIT_OBJECT_0 || hung) {
            continue;
        }
        hung = true;
        Log("window thread has not answered for %u ms", kHangMs);
        WriteDump();
    }
}

void HangWatch::WriteDump() {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t name[64];
    swprintf_s(name, L"\\OpenChatMix-hang-%04u%02u%02u-%02u%02u%02u.dmp", now.wYear, now.wMonth, now.wDay, now.wHour,
               now.wMinute, now.wSecond);
    const std::wstring path = directory_ + name;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        Log("hang dump not written, error %lu", GetLastError());
        return;
    }
    // Thread stacks and module list are enough to see where each thread waits.
    const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
    const BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type, nullptr, nullptr, nullptr);
    const DWORD error = ok ? 0 : GetLastError();
    CloseHandle(file);
    if (ok) {
        Log("hang dump written: %ls", path.c_str());
    } else {
        DeleteFileW(path.c_str());
        Log("hang dump not written, error %lu", error);
    }
}
