#include "frame_clock.h"

namespace ui {
namespace {

constexpr DWORD kQuitTimeoutMs = 1000;
constexpr DWORD kFallbackRefreshHz = 60;

LONGLONG Now() {
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return counter.QuadPart;
}

DWORD RefreshRate(HMONITOR monitor) {
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (GetMonitorInfoW(monitor, &info) && EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode) &&
        mode.dmDisplayFrequency > 1) {
        return mode.dmDisplayFrequency;
    }
    return kFallbackRefreshHz;
}

}  // namespace

FrameClock::~FrameClock() {
    if (thread_) {
        quit_ = true;
        SetEvent(wake_);
        WaitForSingleObject(thread_, kQuitTimeoutMs);
        CloseHandle(thread_);
    }
    if (wake_) {
        CloseHandle(wake_);
    }
    if (timer_) {
        CloseHandle(timer_);
    }
}

void FrameClock::Start(HWND window, UINT message) {
    window_ = window;
    message_ = message;
    if (!thread_) {
        if (!wake_) {
            wake_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        }
        if (!timer_) {
            timer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        }
        if (!timer_) {
            // High-resolution timers need Windows 10 1803; older systems get the regular one.
            timer_ = CreateWaitableTimerW(nullptr, TRUE, nullptr);
        }
        if (!wake_ || !(thread_ = CreateThread(nullptr, 0, ThreadProc, this, 0, nullptr))) {
            return;
        }
        SetThreadPriority(thread_, THREAD_PRIORITY_ABOVE_NORMAL);
    }
    if (!running_.exchange(true)) {
        // A message posted to a window that was destroyed since then never arrives.
        posted_ = false;
        SetEvent(wake_);
    }
}

void FrameClock::Stop() {
    if (running_.exchange(false) && wake_) {
        ResetEvent(wake_);
    }
}

DWORD WINAPI FrameClock::ThreadProc(void* self) {
    static_cast<FrameClock*>(self)->Run();
    return 0;
}

void FrameClock::Run() {
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    frequency_ = frequency.QuadPart;
    for (;;) {
        WaitForSingleObject(wake_, INFINITE);
        if (quit_) {
            break;
        }
        WaitForRefresh();
        if (quit_) {
            break;
        }
        if (running_ && !posted_.exchange(true) && !PostMessageW(window_, message_, 0, 0)) {
            posted_ = false;
        }
    }
    output_.Reset();
    factory_.Reset();
}

IDXGIOutput* FrameClock::OutputFor(HMONITOR monitor) {
    if (monitor == monitor_ && period_ && factory_ && factory_->IsCurrent()) {
        return output_.Get();
    }
    monitor_ = monitor;
    period_ = frequency_ / RefreshRate(monitor);
    output_.Reset();
    if (!factory_ || !factory_->IsCurrent()) {
        factory_.Reset();
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory_)))) {
            return nullptr;
        }
    }
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    for (UINT a = 0; !output_ && factory_->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a) {
        Microsoft::WRL::ComPtr<IDXGIOutput> output;
        for (UINT o = 0; adapter->EnumOutputs(o, &output) != DXGI_ERROR_NOT_FOUND; ++o) {
            DXGI_OUTPUT_DESC desc{};
            if (SUCCEEDED(output->GetDesc(&desc)) && desc.Monitor == monitor) {
                output_ = output;
                break;
            }
        }
    }
    return output_.Get();
}

void FrameClock::WaitForRefresh() {
    IDXGIOutput* output = OutputFor(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST));
    // A vertical blank right after the previous frame is skipped, so frames never come faster
    // than the refresh rate even if the wait returns early, e.g. while the display is off.
    for (int attempt = 0; output && attempt < 2; ++attempt) {
        if (FAILED(output->WaitForVBlank())) {
            break;
        }
        const LONGLONG now = Now();
        if (now - lastFrame_ >= period_ / 2) {
            lastFrame_ = now;
            return;
        }
    }
    // Without vertical blanks (displays asleep, some remote sessions) a timer keeps the refresh rate.
    // Frames stay on a fixed grid so timer latency does not add up; a stall restarts the grid.
    LONGLONG next = lastFrame_ + period_;
    if (next < Now() - period_) {
        next = Now();
    }
    const LONGLONG remaining = next - Now();
    if (remaining > 0) {
        // Relative due time in 100 ns units.
        LARGE_INTEGER due;
        due.QuadPart = -(remaining * 10'000'000 / frequency_);
        if (timer_ && SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE)) {
            WaitForSingleObject(timer_, INFINITE);
        } else {
            Sleep(static_cast<DWORD>(remaining * 1000 / frequency_) + 1);
        }
    }
    lastFrame_ = next;
}

}  // namespace ui
