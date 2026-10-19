#pragma once

#include <windows.h>

#include <atomic>
#include <string>
#include <vector>

// Listens to what a playback device plays (WASAPI loopback) on a thread of its own and keeps the latest second of
// it as mono samples, e.g. for the visualizer.
class AudioTap {
public:
    AudioTap() = default;
    ~AudioTap();
    AudioTap(const AudioTap&) = delete;
    AudioTap& operator=(const AudioTap&) = delete;

    bool Start(const std::wstring& endpointId);
    void Stop();
    bool IsRunning() const { return thread_ != nullptr; }
    // Copies the latest count samples, oldest first, and returns their sample rate; while nothing plays the samples
    // are silence. 0 when nothing is captured.
    int Latest(float* out, size_t count) const;

private:
    static DWORD WINAPI ThreadProc(void* self);
    void Run();
    void Append(const float* frames, UINT32 count, int channels, bool silent);

    std::wstring endpointId_;
    HANDLE thread_ = nullptr;
    HANDLE stop_ = nullptr;
    mutable SRWLOCK lock_ = SRWLOCK_INIT;
    std::vector<float> ring_;  // Under lock_.
    size_t write_ = 0;         // Under lock_.
    int rate_ = 0;             // Under lock_.
    std::atomic<ULONGLONG> lastData_{0};  // Tick of the last packet.
};
