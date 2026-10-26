#include "chatmix_device.h"

#include <hidsdi.h>

#include <algorithm>
#include <string>

#include "app.h"
#include "hid_interfaces.h"
#include "log.h"

namespace {

constexpr USHORT kCommandUsagePage = 0xFFC0;

// Nova Pro parameters (second byte after report ID 0x06).
constexpr uint8_t kNovaProChatMix = 0x45;        // [game 0-100] [chat 0-100]
constexpr uint8_t kNovaProChatMixEnable = 0x49;  // 1: dial controls the ChatMix balance
constexpr uint8_t kNovaProChatMixIcon = 0x8D;    // 1: ChatMix icon on the display
constexpr uint8_t kNovaProEventReportId = 0x07;
constexpr uint8_t kNovaProVolume = 0x25;         // Event: knob turned in volume mode.
constexpr int kVolumeSteps = 56;                 // The knob's attenuation: 0 is the loudest, 56 the quietest.
// Events the Nova Pro Wireless base station sends on its own, as Linux-Arctis-Manager lists them.
constexpr uint8_t kNovaProLinkEvent = 0xB5;      // The 2.4 GHz link to the headset changed.
constexpr uint8_t kNovaProBatteryEvent = 0xB7;   // Headset or spare battery level changed.
constexpr uint8_t kNovaStatusRequest = 0xB0;
constexpr uint8_t kNovaStatusLinkEvent = 0xB9;   // Dongles: the 2.4 GHz link to the headset changed.
constexpr uint8_t kArctis7DialRequest = 0x24;    // Arctis 7: answered with 06 24 [game] [chat].

constexpr DWORD kCommandGapMs = 10;
constexpr DWORD kStatusPollMs = 100;
constexpr DWORD kArctis7PollMs = 250;  // The dial only shows in the panel, so a slower poll will do.
constexpr ULONGLONG kBatteryPollMs = 30000;
constexpr DWORD kReaderStopTimeoutMs = 2000;

struct ProtocolLayout {
    int commandInterface;
    uint8_t reportId;
    int listenInterfaces[2];
    bool polled;
};

constexpr ProtocolLayout kNovaProLayout{4, 0x06, {4, -1}, false};
constexpr ProtocolLayout kNovaStatusLayout{3, 0x00, {3, 5}, true};
// Commands and their replies on page FFC0, events on page FF00, both on interface 3.
constexpr ProtocolLayout kEliteLayout{3, 0x01, {3, -1}, false};
constexpr ProtocolLayout kArctis7Layout{5, 0x06, {5, -1}, true};

const ProtocolLayout& LayoutFor(const DeviceModel& model) {
    switch (model.protocol) {
    case DeviceProtocol::NovaPro: return kNovaProLayout;
    case DeviceProtocol::Elite: return kEliteLayout;
    case DeviceProtocol::ArctisLegacy: return kArctis7Layout;
    case DeviceProtocol::NovaStatus: break;
    }
    return kNovaStatusLayout;
}

// What a polled device is asked for every poll.
uint8_t PollRequest(const DeviceModel& model) {
    return model.protocol == DeviceProtocol::ArctisLegacy ? kArctis7DialRequest : kNovaStatusRequest;
}

// Base stations that push 07 events: the dial, the knob and the menu, as on the GameDAC.
bool PushesEvents(const DeviceModel& model) {
    return model.protocol == DeviceProtocol::NovaPro || model.protocol == DeviceProtocol::Elite;
}

uint32_t Pack(Levels levels) {
    return (static_cast<uint32_t>(levels.game) << 8) | static_cast<uint32_t>(levels.chat);
}

Levels Unpack(uint32_t packed) {
    return {static_cast<int>((packed >> 8) & 0xFF), static_cast<int>(packed & 0xFF)};
}

// The GameDAC keeps 12 steps per side and stores step ceil((100 - value) * 12 / 100), reporting each
// step as floor(100 * (12 - step) / 12), e.g. 91 for step 1. Sending that reported value back would
// land on the next step, so each side is sent as the smallest value that maps onto its step.
uint8_t DacChatMixValue(int level) {
    const long step = std::lround((100 - std::clamp(level, 0, 100)) * kBalanceSteps / 100.0);
    return static_cast<uint8_t>(100 - 100 * step / kBalanceSteps);
}

}  // namespace

ChatMixDevice::~ChatMixDevice() {
    Close();
}

bool ChatMixDevice::Open(HWND notifyWindow) {
    Close();
    notify_ = notifyWindow;

    const std::vector<HidInterface> interfaces =
        EnumerateSteelSeriesInterfaces([](uint16_t productId) { return FindDeviceModel(productId) != nullptr; });
    const DeviceModel* model = nullptr;
    for (const auto& candidate : SupportedDevices()) {
        if (std::any_of(interfaces.begin(), interfaces.end(),
                        [&](const HidInterface& hid) { return hid.productId == candidate.productId; })) {
            model = &candidate;
            break;
        }
    }
    if (!model) {
        return false;
    }

    const ProtocolLayout& layout = LayoutFor(*model);
    const DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE;
    const HidInterface* command = nullptr;
    for (const auto& hid : interfaces) {
        if (hid.productId != model->productId || hid.interfaceNumber != layout.commandInterface ||
            hid.outputLength == 0) {
            continue;
        }
        if (!command || hid.usagePage == kCommandUsagePage) {
            command = &hid;
        }
    }
    if (command) {
        command_ = CreateFileW(command->path.c_str(), GENERIC_READ | GENERIC_WRITE, share, nullptr, OPEN_EXISTING, 0,
                               nullptr);
        outputLength_ = command->outputLength;
        commandPath_ = command->path;
        featureLength_ = command->featureLength;
        Log("%ls command: %ls (out %u, feature %u)", model->name, command->path.c_str(), outputLength_,
            featureLength_);
    }
    if (command_ == INVALID_HANDLE_VALUE) {
        Log("%ls: command interface unavailable, error %lu", model->name, GetLastError());
        Close();
        return false;
    }

    for (const auto& hid : interfaces) {
        const bool listened = std::find(std::begin(layout.listenInterfaces), std::end(layout.listenInterfaces),
                                        hid.interfaceNumber) != std::end(layout.listenInterfaces);
        if (hid.productId != model->productId || !listened || hid.usagePage < kVendorUsagePageMin ||
            hid.inputLength == 0) {
            continue;
        }
        Channel channel;
        channel.handle = CreateFileW(hid.path.c_str(), GENERIC_READ | GENERIC_WRITE, share, nullptr, OPEN_EXISTING,
                                     FILE_FLAG_OVERLAPPED, nullptr);
        if (channel.handle == INVALID_HANDLE_VALUE) {
            Log("%ls: skip %ls, error %lu", model->name, hid.path.c_str(), GetLastError());
            continue;
        }
        channel.buffer.resize(hid.inputLength);
        channel.overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        Log("%ls events: %ls (in %u)", model->name, hid.path.c_str(), hid.inputLength);
        channels_.push_back(std::move(channel));
    }
    if (channels_.empty()) {
        Log("%ls: no readable event interface", model->name);
        Close();
        return false;
    }

    model_ = model;
    reportId_ = layout.reportId;
    lastDeviceLevels_ = kNoLevels;
    levelsPending_ = false;
    pushEventsSeen_ = false;
    pollFailing_ = false;
    dialMode_ = DialMode::Unknown;
    lastEventTick_ = 0;
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    reader_ = stopEvent_ ? CreateThread(nullptr, 0, ReaderProc, this, 0, nullptr) : nullptr;
    if (!reader_) {
        Log("%ls: reader start failed, error %lu", model->name, GetLastError());
        Close();
        return false;
    }
    return true;
}

void ChatMixDevice::Close() {
    if (reader_) {
        SetEvent(stopEvent_);
        WaitForSingleObject(reader_, kReaderStopTimeoutMs);
        CloseHandle(reader_);
        reader_ = nullptr;
    }
    if (stopEvent_) {
        CloseHandle(stopEvent_);
        stopEvent_ = nullptr;
    }
    for (auto& channel : channels_) {
        if (channel.handle != INVALID_HANDLE_VALUE) {
            CloseHandle(channel.handle);
        }
        if (channel.overlapped.hEvent) {
            CloseHandle(channel.overlapped.hEvent);
        }
    }
    channels_.clear();
    if (command_ != INVALID_HANDLE_VALUE) {
        CloseHandle(command_);
        command_ = INVALID_HANDLE_VALUE;
    }
    commandPath_.clear();
    featureLength_ = 0;
    model_ = nullptr;
}

void ChatMixDevice::Start(Levels levels) {
    if (!model_) {
        return;
    }
    if (model_->protocol == DeviceProtocol::NovaPro) {
        Send({kNovaProChatMixIcon, 1});
        Sleep(kCommandGapMs);
        Send({kNovaProChatMixEnable, 1});
        Sleep(kCommandGapMs);
        SetLevels(levels);
    } else if (model_->protocol == DeviceProtocol::Elite) {
        // The same switches as on the Nova Pro, as Linux-Arctis-Manager lists them for the Elite. The balance is
        // not sent: the Elite's command for it is unknown.
        Send({kNovaProChatMixIcon, 1});
        Sleep(kCommandGapMs);
        Send({kNovaProChatMixEnable, 1});
    } else {
        Send({PollRequest(*model_)});
    }
    // Read-only queries; their answers arrive as headset updates.
    const HeadsetFeatures& features = FeaturesFor(model_->family);
    for (const uint8_t query : {features.stateQuery, features.batteryQuery}) {
        // The dongles' battery comes with the status request sent above.
        if (query && !(model_->protocol == DeviceProtocol::NovaStatus && query == kNovaStatusRequest)) {
            Sleep(kCommandGapMs);
            Send({query});
        }
    }
}

bool ChatMixDevice::SendCommand(std::span<const uint8_t> payload) {
    return model_ && Send(payload);
}

bool ChatMixDevice::SendFeature(std::span<const uint8_t> payload) {
    if (!model_ || command_ == INVALID_HANDLE_VALUE || featureLength_ <= payload.size()) {
        return false;
    }
    std::vector<uint8_t> report(featureLength_, 0);
    report[0] = reportId_;
    std::copy(payload.begin(), payload.end(), report.begin() + 1);
    // Only the bytes that carry data are logged; the zeros after them fill the report.
    if (!HidD_SetFeature(command_, report.data(), featureLength_)) {
        Log("tx feature %s failed, error %lu", HexBytes(report.data(), payload.size() + 1).c_str(), GetLastError());
        return false;
    }
    Log("tx feature %s", HexBytes(report.data(), payload.size() + 1).c_str());
    return true;
}

void ChatMixDevice::QueryState() {
    if (model_) {
        if (const uint8_t query = FeaturesFor(model_->family).stateQuery) {
            Send({query});
        }
    }
}

std::optional<DisplayCollection> ChatMixDevice::Display() const {
    if (!model_ || !FeaturesFor(model_->family).oled || commandPath_.empty()) {
        return std::nullopt;
    }
    return DisplayCollection{commandPath_, featureLength_, outputLength_, reportId_};
}

std::vector<HeadsetUpdate> ChatMixDevice::TakeHeadsetUpdates() {
    AcquireSRWLockExclusive(&updatesLock_);
    std::vector<HeadsetUpdate> updates = std::move(headsetUpdates_);
    headsetUpdates_.clear();
    ReleaseSRWLockExclusive(&updatesLock_);
    return updates;
}

void ChatMixDevice::QueueHeadsetUpdates(std::vector<HeadsetUpdate> updates) {
    if (updates.empty()) {
        return;
    }
    AcquireSRWLockExclusive(&updatesLock_);
    const bool wasEmpty = headsetUpdates_.empty();
    headsetUpdates_.insert(headsetUpdates_.end(), updates.begin(), updates.end());
    ReleaseSRWLockExclusive(&updatesLock_);
    if (wasEmpty) {
        PostMessageW(notify_, WM_APP_HEADSET, 0, 0);
    }
}

void ChatMixDevice::Stop() {
    if (model_ && PushesEvents(*model_)) {
        Send({kNovaProChatMixEnable, 0});
        Sleep(kCommandGapMs);
        Send({kNovaProChatMixIcon, 0});
    }
}

void ChatMixDevice::SetLevels(Levels levels) {
    if (!model_ || model_->protocol != DeviceProtocol::NovaPro) {
        return;
    }
    // The DAC does not echo this command, so record it as the current dial position.
    lastDeviceLevels_ = Pack(levels);
    Send({kNovaProChatMix, DacChatMixValue(levels.game), DacChatMixValue(levels.chat)});
}

bool ChatMixDevice::TakeLevels(Levels& levels) {
    // Clear the flag before reading so a report stored after this point posts a new message.
    if (!levelsPending_.exchange(false)) {
        return false;
    }
    levels = Unpack(pendingLevels_.load());
    return true;
}

bool ChatMixDevice::Send(std::span<const uint8_t> payload) {
    if (command_ == INVALID_HANDLE_VALUE || outputLength_ <= payload.size()) {
        return false;
    }
    std::vector<uint8_t> report(outputLength_, 0);
    report[0] = reportId_;
    std::copy(payload.begin(), payload.end(), report.begin() + 1);
    // Status polls repeat several times a second; only their first failure and recovery are logged.
    const bool poll = model_ && LayoutFor(*model_).polled && payload[0] == PollRequest(*model_);
    DWORD written = 0;
    if (!WriteFile(command_, report.data(), static_cast<DWORD>(report.size()), &written, nullptr)) {
        const DWORD error = GetLastError();
        if (!poll || !pollFailing_.exchange(true)) {
            Log("tx %s failed, error %lu", HexBytes(report.data(), payload.size() + 1).c_str(), error);
        }
        return false;
    }
    if (!poll) {
        Log("tx %s", HexBytes(report.data(), payload.size() + 1).c_str());
    } else if (pollFailing_.exchange(false)) {
        Log("status polling recovered");
    }
    return true;
}

bool ChatMixDevice::PublishLevels(Levels levels) {
    // Only dial movement counts: repeated status reports must not undo changes made in the app.
    const uint32_t packed = Pack(levels);
    if (lastDeviceLevels_.exchange(packed) == packed) {
        return false;
    }
    pendingLevels_ = packed;
    if (!levelsPending_.exchange(true)) {
        PostMessageW(notify_, WM_APP_CHATMIX, 0, 0);
    }
    return true;
}

void ChatMixDevice::HandleReport(Channel& channel, DWORD size) {
    const uint8_t* data = channel.buffer.data();
    // Unnumbered reports arrive with a leading 0x00 report ID.
    if (size > 0 && data[0] == 0x00) {
        ++data;
        --size;
    }
    // Settings replies are logged in full, other reports by their first bytes; everything from an experimental
    // model in full, so its log shows what it sends.
    const bool state = model_->protocol == DeviceProtocol::NovaPro && size >= 2 && data[1] == 0x20;
    const bool full = state || model_->protocol == DeviceProtocol::Elite || model_->protocol == DeviceProtocol::ArctisLegacy;
    const std::vector<uint8_t> current(data, data + std::min<DWORD>(size, full ? 32 : 8));
    if (current != channel.last) {
        Log("rx %s", HexBytes(current.data(), current.size()).c_str());
        channel.last = current;
    }

    if (PushesEvents(*model_) && size >= 2 && data[0] == kNovaProEventReportId && data[1] != kNovaProLinkEvent &&
        data[1] != kNovaProBatteryEvent) {
        // Every other event comes from someone using the GameDAC: its knob, buttons or menu.
        lastEventTick_ = GetTickCount64();
    }
    if (PushesEvents(*model_) && size >= 2 && data[0] == kNovaProEventReportId &&
        (data[1] == kNovaProVolume || data[1] == kNovaProChatMix)) {
        const DialMode mode = data[1] == kNovaProVolume ? DialMode::Volume : DialMode::ChatMix;
        if (dialMode_.exchange(mode) != mode) {
            PostMessageW(notify_, WM_APP_DIAL_MODE, 0, 0);
        }
        if (mode == DialMode::Volume && size >= 3) {
            volume_ = ((kVolumeSteps - std::min<int>(data[2], kVolumeSteps)) * 100 + kVolumeSteps / 2) / kVolumeSteps;
            PostMessageW(notify_, WM_APP_HEADSET_VOLUME, 0, 0);
        }
    }

    QueueHeadsetUpdates(ParseHeadsetReport(*model_, data, size));

    // The headset was switched on or off: its status is asked for at once instead of at the next battery poll,
    // so the output can follow within a moment.
    const uint8_t batteryQuery = FeaturesFor(model_->family).batteryQuery;
    const bool linkChanged = PushesEvents(*model_)
                                 ? size >= 2 && data[0] == kNovaProEventReportId && data[1] == kNovaProLinkEvent
                                 : size >= 1 && data[0] == kNovaStatusLinkEvent;
    if (batteryQuery && linkChanged) {
        Send({batteryQuery});
    }

    bool pushed = false;
    if (const auto levels = ParseChatMixReport(*model_, data, size, pushed)) {
        if (pushed && !pushEventsSeen_.exchange(true) && LayoutFor(*model_).polled) {
            Log("device pushes ChatMix events, status polling stopped");
        }
        PublishLevels(*levels);
    }
}

DWORD WINAPI ChatMixDevice::ReaderProc(void* self) {
    static_cast<ChatMixDevice*>(self)->ReadLoop();
    return 0;
}

void ChatMixDevice::ReadLoop() {
    const bool polled = LayoutFor(*model_).polled;
    std::vector<HANDLE> waits{stopEvent_};
    for (auto& channel : channels_) {
        waits.push_back(channel.overlapped.hEvent);
    }

    auto startRead = [&](Channel& channel) {
        ResetEvent(channel.overlapped.hEvent);
        if (!ReadFile(channel.handle, channel.buffer.data(), static_cast<DWORD>(channel.buffer.size()), nullptr,
                      &channel.overlapped) &&
            GetLastError() != ERROR_IO_PENDING) {
            Log("read failed, error %lu", GetLastError());
            return false;
        }
        return true;
    };

    bool lost = false;
    for (auto& channel : channels_) {
        if (!startRead(channel)) {
            lost = true;
        }
    }

    const uint8_t batteryQuery = FeaturesFor(model_->family).batteryQuery;
    ULONGLONG nextBatteryPoll = GetTickCount64() + kBatteryPollMs;
    while (!lost) {
        const DWORD pollMs = model_->protocol == DeviceProtocol::ArctisLegacy ? kArctis7PollMs : kStatusPollMs;
        DWORD timeout = polled && !pushEventsSeen_ ? pollMs : INFINITE;
        if (batteryQuery) {
            const ULONGLONG now = GetTickCount64();
            timeout = std::min<DWORD>(timeout, nextBatteryPoll > now ? static_cast<DWORD>(nextBatteryPoll - now) : 0);
        }
        const DWORD wait = WaitForMultipleObjects(static_cast<DWORD>(waits.size()), waits.data(), FALSE, timeout);
        if (wait == WAIT_TIMEOUT) {
            if (batteryQuery && GetTickCount64() >= nextBatteryPoll) {
                Send({batteryQuery});
                nextBatteryPoll = GetTickCount64() + kBatteryPollMs;
            }
            if (polled && !pushEventsSeen_) {
                Send({PollRequest(*model_)});
            }
            continue;
        }
        if (wait == WAIT_OBJECT_0 || wait >= WAIT_OBJECT_0 + waits.size()) {
            break;
        }
        Channel& channel = channels_[wait - WAIT_OBJECT_0 - 1];
        DWORD received = 0;
        if (!GetOverlappedResult(channel.handle, &channel.overlapped, &received, FALSE)) {
            Log("read failed, error %lu", GetLastError());
            lost = true;
            break;
        }
        HandleReport(channel, received);
        if (!startRead(channel)) {
            lost = true;
        }
    }

    for (auto& channel : channels_) {
        CancelIoEx(channel.handle, &channel.overlapped);
        DWORD ignored = 0;
        GetOverlappedResult(channel.handle, &channel.overlapped, &ignored, TRUE);
    }
    if (lost) {
        PostMessageW(notify_, WM_APP_DEVICE_LOST, 0, 0);
    }
}
