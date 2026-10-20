#pragma once

#include <windows.h>

#include <cstdint>
#include <vector>

// Logs SteelSeries headsets that OpenChatMix does not support: their HID collections and the reports they send
// on vendor pages, so a log from such a headset shows how it talks. Read-only: nothing is ever sent to them.
// Meant to run only while the log is on.
class HidProbe {
public:
    HidProbe() = default;
    HidProbe(const HidProbe&) = delete;
    HidProbe& operator=(const HidProbe&) = delete;
    ~HidProbe();

    // Lists the collections of these products and logs their vendor input reports until Stop(). Does nothing
    // while it already watches the same products.
    void Start(const std::vector<uint16_t>& productIds);
    void Stop();

private:
    struct Channel {
        HANDLE handle = INVALID_HANDLE_VALUE;
        uint16_t productId = 0;
        int interfaceNumber = -1;
        USHORT usagePage = 0;
        std::vector<uint8_t> buffer;
        std::vector<uint8_t> last;  // Previous report, so repeats stay out of the log.
        OVERLAPPED overlapped{};
        bool pending = false;  // A read is in flight and owns the buffer.
    };

    static DWORD WINAPI ReaderProc(void* self);
    void ReadLoop();

    std::vector<uint16_t> productIds_;
    std::vector<Channel> channels_;
    HANDLE stopEvent_ = nullptr;
    HANDLE reader_ = nullptr;
};
