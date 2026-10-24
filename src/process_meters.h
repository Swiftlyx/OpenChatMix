#pragma once

#include <windows.h>

#include <map>
#include <memory>
#include <optional>

// Peak meters of single apps from process loopback captures (Windows 10 2004 and later). A capture opens on the
// first read of its process and closes once nobody reads it for a while. It hears only what the app plays, after
// its volume and ChatMix level, but the app's child processes too; an audio session's own meter also shows what
// the app captures from the device, e.g. OBS recording desktop audio or Discord's echo cancellation.
// Must be used from a COM MTA thread.
class ProcessMeters {
public:
    ProcessMeters() = default;
    ~ProcessMeters();
    ProcessMeters(const ProcessMeters&) = delete;
    ProcessMeters& operator=(const ProcessMeters&) = delete;

    // Peak (0-1) of what the process and its children played since the last read; 0 while the capture opens.
    // Empty when the process cannot be captured, e.g. on Windows before process loopback.
    std::optional<float> Peak(DWORD pid);
    // Closes every capture; Peak opens them again.
    void Close();

private:
    struct Capture;
    static void CALLBACK Prune(PTP_CALLBACK_INSTANCE instance, void* self, PTP_TIMER timer);
    // Under lock_.
    void Arm();

    SRWLOCK lock_ = SRWLOCK_INIT;
    std::map<DWORD, std::shared_ptr<Capture>> captures_;  // Under lock_.
    bool armed_ = false;                                  // Under lock_.
    PTP_TIMER timer_ = nullptr;
};
