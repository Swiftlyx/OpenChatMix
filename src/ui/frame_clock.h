#pragma once

#include <windows.h>

#include <dxgi.h>
#include <wrl/client.h>

#include <atomic>

namespace ui {

// Posts a message to a window once per display refresh while running, so animations follow the
// monitor refresh rate (e.g. 240 Hz) instead of a fixed timer. The helper thread waits for the
// vertical blank of the monitor that shows the window and exists only after the first Start().
class FrameClock {
public:
    FrameClock() = default;
    ~FrameClock();
    FrameClock(const FrameClock&) = delete;
    FrameClock& operator=(const FrameClock&) = delete;

    // Starts or retargets the clock; repeated calls while running are cheap.
    void Start(HWND window, UINT message);
    void Stop();
    bool IsRunning() const { return running_.load(); }
    // Call when the posted message is handled; at most one message is queued at a time.
    void FrameHandled() { posted_ = false; }

private:
    static DWORD WINAPI ThreadProc(void* self);
    void Run();
    void WaitForRefresh();
    // Output and refresh period of the monitor; null output when DXGI cannot provide it.
    IDXGIOutput* OutputFor(HMONITOR monitor);

    HANDLE thread_ = nullptr;
    HANDLE wake_ = nullptr;   // Manual-reset, signaled while running.
    HANDLE timer_ = nullptr;  // High-resolution timer for the fallback wait.
    std::atomic<HWND> window_{nullptr};
    std::atomic<UINT> message_{0};
    std::atomic<bool> running_{false};
    std::atomic<bool> quit_{false};
    std::atomic<bool> posted_{false};

    // Used by the helper thread only.
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory_;
    Microsoft::WRL::ComPtr<IDXGIOutput> output_;
    HMONITOR monitor_ = nullptr;
    LONGLONG period_ = 0;     // Refresh period in performance counter ticks.
    LONGLONG lastFrame_ = 0;  // Performance counter at the last refresh.
    LONGLONG frequency_ = 0;
};

}  // namespace ui
