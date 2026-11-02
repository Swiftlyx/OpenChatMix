#pragma once

#include <windows.h>

#include <string>
#include <vector>

// Listens to what a playback device plays (WASAPI loopback), or to what some apps play (process loopback), on a
// thread of its own and keeps the latest second of it as mono samples, e.g. for the visualizer.
class AudioTap {
public:
    AudioTap() = default;
    ~AudioTap();
    AudioTap(const AudioTap&) = delete;
    AudioTap& operator=(const AudioTap&) = delete;

    // With processes, hears only them and the ones they started, on whatever device they play, added together
    // (Windows 10 2004 and later); without any, or where Windows cannot, everything the device plays.
    bool Start(const std::wstring& endpointId, std::vector<DWORD> processIds = {});
    void Stop();
    bool IsRunning() const { return thread_ != nullptr; }
    // Whether it runs for this device and these processes.
    bool Listens(const std::wstring& endpointId, const std::vector<DWORD>& processIds) const {
        return IsRunning() && endpointId_ == endpointId && processIds_ == processIds;
    }
    // Copies the latest count samples, oldest first, and returns their sample rate; while nothing plays the samples
    // are silence. 0 when nothing is captured.
    int Latest(float* out, size_t count) const;

private:
    // What one capture heard.
    struct Source {
        std::vector<float> ring;
        size_t write = 0;
        ULONGLONG lastData = 0;  // Tick of the last packet.
    };

    static DWORD WINAPI ThreadProc(void* self);
    void Run();
    void Append(size_t index, const float* frames, UINT32 count, int channels, bool silent);

    std::wstring endpointId_;
    std::vector<DWORD> processIds_;
    HANDLE thread_ = nullptr;
    HANDLE stop_ = nullptr;
    mutable SRWLOCK lock_ = SRWLOCK_INIT;
    std::vector<Source> sources_;  // Under lock_.
    int rate_ = 0;                 // Under lock_.
};
