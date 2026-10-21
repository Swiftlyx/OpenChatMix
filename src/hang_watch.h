#pragma once

#include <windows.h>

#include <string>

// While the log is on, a thread checks that the window's thread keeps answering messages. When it stops
// answering for kHangMs, the process goes into a minidump next to the log, once per hang, so the call it is stuck
// in can be read from the dump with the build's PDB.
class HangWatch {
public:
    HangWatch() = default;
    HangWatch(const HangWatch&) = delete;
    HangWatch& operator=(const HangWatch&) = delete;
    ~HangWatch();

    void Start(HWND window, const std::wstring& directory);
    // Call on the window's thread: it answers the watch's pending check while it waits.
    void Stop();

private:
    static DWORD WINAPI WatchProc(void* self);
    void Watch();
    void WriteDump();

    HWND window_ = nullptr;
    std::wstring directory_;
    HANDLE stop_ = nullptr;
    HANDLE thread_ = nullptr;
};
