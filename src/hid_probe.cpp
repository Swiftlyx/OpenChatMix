#include "hid_probe.h"

#include <algorithm>

#include "hid_interfaces.h"
#include "log.h"

namespace {

constexpr DWORD kStopTimeoutMs = 2000;
constexpr size_t kLoggedBytes = 32;    // Enough for every status reply seen so far.
constexpr int kMaxLoggedReports = 2000;  // Keeps the log small if a device streams changing reports.

}  // namespace

HidProbe::~HidProbe() {
    Stop();
}

void HidProbe::Start(const std::vector<uint16_t>& productIds) {
    if (reader_ && productIds == productIds_) {
        return;
    }
    Stop();
    productIds_ = productIds;
    if (productIds.empty()) {
        return;
    }
    const auto interfaces = EnumerateSteelSeriesInterfaces([&](uint16_t productId) {
        return std::find(productIds.begin(), productIds.end(), productId) != productIds.end();
    });
    for (const HidInterface& hid : interfaces) {
        Log("unsupported SteelSeries device 1038:%04x: interface %d, usage %04x:%04x, in %u, out %u, feature %u",
            hid.productId, hid.interfaceNumber, hid.usagePage, hid.usage, hid.inputLength, hid.outputLength,
            hid.featureLength);
        if (hid.usagePage < kVendorUsagePageMin || hid.inputLength == 0) {
            continue;
        }
        Channel channel;
        // Read access only: the probe never writes to a device it does not know.
        channel.handle = CreateFileW(hid.path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                     OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (channel.handle == INVALID_HANDLE_VALUE) {
            Log("probe 1038:%04x interface %d %04x: not readable, error %lu", hid.productId, hid.interfaceNumber,
                hid.usagePage, GetLastError());
            continue;
        }
        channel.productId = hid.productId;
        channel.interfaceNumber = hid.interfaceNumber;
        channel.usagePage = hid.usagePage;
        channel.buffer.resize(hid.inputLength);
        channel.overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        channels_.push_back(std::move(channel));
    }
    if (channels_.empty()) {
        return;
    }
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    reader_ = stopEvent_ ? CreateThread(nullptr, 0, ReaderProc, this, 0, nullptr) : nullptr;
    if (reader_) {
        Log("probe: listening on %zu collections; reports that change are logged", channels_.size());
    } else {
        Stop();
    }
}

void HidProbe::Stop() {
    if (reader_) {
        SetEvent(stopEvent_);
        WaitForSingleObject(reader_, kStopTimeoutMs);
        CloseHandle(reader_);
        reader_ = nullptr;
    }
    if (stopEvent_) {
        CloseHandle(stopEvent_);
        stopEvent_ = nullptr;
    }
    for (Channel& channel : channels_) {
        if (channel.handle != INVALID_HANDLE_VALUE) {
            if (channel.pending) {
                CancelIoEx(channel.handle, &channel.overlapped);
                DWORD ignored = 0;
                GetOverlappedResult(channel.handle, &channel.overlapped, &ignored, TRUE);
            }
            CloseHandle(channel.handle);
        }
        if (channel.overlapped.hEvent) {
            CloseHandle(channel.overlapped.hEvent);
        }
    }
    channels_.clear();
    productIds_.clear();
}

DWORD WINAPI HidProbe::ReaderProc(void* self) {
    static_cast<HidProbe*>(self)->ReadLoop();
    return 0;
}

void HidProbe::ReadLoop() {
    std::vector<HANDLE> waits{stopEvent_};
    for (Channel& channel : channels_) {
        waits.push_back(channel.overlapped.hEvent);
    }
    // A channel that fails stops being read; its event stays unsignaled.
    auto startRead = [](Channel& channel) {
        ResetEvent(channel.overlapped.hEvent);
        channel.pending = ReadFile(channel.handle, channel.buffer.data(), static_cast<DWORD>(channel.buffer.size()),
                                   nullptr, &channel.overlapped) ||
                          GetLastError() == ERROR_IO_PENDING;
        return channel.pending;
    };
    for (Channel& channel : channels_) {
        if (!startRead(channel)) {
            Log("probe 1038:%04x interface %d %04x: read failed, error %lu", channel.productId,
                channel.interfaceNumber, channel.usagePage, GetLastError());
        }
    }
    int logged = 0;
    for (;;) {
        const DWORD wait = WaitForMultipleObjects(static_cast<DWORD>(waits.size()), waits.data(), FALSE, INFINITE);
        if (wait == WAIT_OBJECT_0 || wait >= WAIT_OBJECT_0 + waits.size()) {
            break;
        }
        Channel& channel = channels_[wait - WAIT_OBJECT_0 - 1];
        DWORD received = 0;
        const bool read = GetOverlappedResult(channel.handle, &channel.overlapped, &received, FALSE);
        channel.pending = false;
        if (!read) {
            Log("probe 1038:%04x interface %d %04x: read failed, error %lu", channel.productId,
                channel.interfaceNumber, channel.usagePage, GetLastError());
            ResetEvent(channel.overlapped.hEvent);
            continue;
        }
        const std::vector<uint8_t> report(channel.buffer.begin(),
                                          channel.buffer.begin() + std::min<size_t>(received, kLoggedBytes));
        if (report != channel.last && logged < kMaxLoggedReports) {
            channel.last = report;
            Log("probe 1038:%04x interface %d %04x rx %s", channel.productId, channel.interfaceNumber, channel.usagePage,
                HexBytes(report.data(), report.size()).c_str());
            if (++logged == kMaxLoggedReports) {
                Log("probe: report limit reached, nothing more is logged");
            }
        }
        if (!startRead(channel)) {
            Log("probe 1038:%04x interface %d %04x: read failed, error %lu", channel.productId,
                channel.interfaceNumber, channel.usagePage, GetLastError());
        }
    }
}
