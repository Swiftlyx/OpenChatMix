#include "chatmix_device.h"

#include <hidsdi.h>
#include <setupapi.h>

#include <algorithm>
#include <string>

#include "app.h"
#include "log.h"

namespace {

constexpr USHORT kCommandUsagePage = 0xFFC0;
constexpr USHORT kVendorUsagePageMin = 0xFF00;

// Nova Pro parameters (second byte after report ID 0x06).
constexpr uint8_t kNovaProChatMix = 0x45;        // [game 0-100] [chat 0-100]
constexpr uint8_t kNovaProChatMixEnable = 0x49;  // 1: dial controls the ChatMix balance
constexpr uint8_t kNovaProChatMixIcon = 0x8D;    // 1: ChatMix icon on the display
constexpr uint8_t kNovaProEventReportId = 0x07;
constexpr uint8_t kNovaProVolume = 0x25;         // Event: knob turned in volume mode.
constexpr uint8_t kNovaStatusRequest = 0xB0;

constexpr DWORD kCommandGapMs = 10;
constexpr DWORD kStatusPollMs = 100;
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

const ProtocolLayout& LayoutFor(const DeviceModel& model) {
    return model.protocol == DeviceProtocol::NovaPro ? kNovaProLayout : kNovaStatusLayout;
}

struct HidInterface {
    std::wstring path;
    uint16_t productId = 0;
    int interfaceNumber = -1;
    USHORT usagePage = 0;
    USHORT inputLength = 0;
    USHORT outputLength = 0;
    USHORT featureLength = 0;
};

bool QueryCaps(HidInterface& item) {
    // Zero access rights are enough for HidD_GetPreparsedData and never conflict with other owners.
    HANDLE device = CreateFileW(item.path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
                                nullptr);
    if (device == INVALID_HANDLE_VALUE) {
        return false;
    }
    bool ok = false;
    PHIDP_PREPARSED_DATA preparsed = nullptr;
    if (HidD_GetPreparsedData(device, &preparsed)) {
        HIDP_CAPS caps{};
        if (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS) {
            item.usagePage = caps.UsagePage;
            item.inputLength = caps.InputReportByteLength;
            item.outputLength = caps.OutputReportByteLength;
            item.featureLength = caps.FeatureReportByteLength;
            ok = true;
        }
        HidD_FreePreparsedData(preparsed);
    }
    CloseHandle(device);
    return ok;
}

std::vector<HidInterface> EnumerateSteelSeriesInterfaces() {
    std::vector<HidInterface> result;
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);
    HDEVINFO set = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) {
        return result;
    }
    SP_DEVICE_INTERFACE_DATA item{};
    item.cbSize = sizeof(item);
    for (DWORD index = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &hidGuid, index, &item); ++index) {
        DWORD size = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &item, nullptr, 0, &size, nullptr);
        if (size == 0) {
            continue;
        }
        std::vector<BYTE> buffer(size);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &item, detail, size, nullptr, nullptr)) {
            continue;
        }
        HidInterface hid;
        hid.path = detail->DevicePath;
        const auto productId = ParseSteelSeriesProductId(hid.path);
        const auto interfaceNumber = ParseInterfaceNumber(hid.path);
        if (!productId || !interfaceNumber || !FindDeviceModel(*productId)) {
            continue;
        }
        hid.productId = *productId;
        hid.interfaceNumber = *interfaceNumber;
        if (QueryCaps(hid)) {
            result.push_back(std::move(hid));
        }
    }
    SetupDiDestroyDeviceInfoList(set);
    return result;
}

std::string Hex(const uint8_t* data, size_t length) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string text;
    for (size_t i = 0; i < length; ++i) {
        if (i) {
            text += ' ';
        }
        text += kDigits[data[i] >> 4];
        text += kDigits[data[i] & 0x0F];
    }
    return text;
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

    const std::vector<HidInterface> interfaces = EnumerateSteelSeriesInterfaces();
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
    } else {
        Send({kNovaStatusRequest});
    }
    // Read-only queries; their answers arrive as headset updates.
    const HeadsetFeatures& features = FeaturesFor(model_->family);
    for (const uint8_t query : {features.stateQuery, features.batteryQuery}) {
        if (query) {
            Sleep(kCommandGapMs);
            Send({query});
        }
    }
}

bool ChatMixDevice::SendCommand(std::span<const uint8_t> payload) {
    return model_ && Send(payload);
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
    if (model_ && model_->protocol == DeviceProtocol::NovaPro) {
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
    // Status polls repeat every 100 ms; only their first failure and recovery are logged.
    const bool poll = model_ && model_->protocol == DeviceProtocol::NovaStatus && payload[0] == kNovaStatusRequest;
    DWORD written = 0;
    if (!WriteFile(command_, report.data(), static_cast<DWORD>(report.size()), &written, nullptr)) {
        const DWORD error = GetLastError();
        if (!poll || !pollFailing_.exchange(true)) {
            Log("tx %s failed, error %lu", Hex(report.data(), payload.size() + 1).c_str(), error);
        }
        return false;
    }
    if (!poll) {
        Log("tx %s", Hex(report.data(), payload.size() + 1).c_str());
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
    // Settings replies are logged in full, other reports by their first bytes.
    const bool state = model_->protocol == DeviceProtocol::NovaPro && size >= 2 && data[1] == 0x20;
    const std::vector<uint8_t> current(data, data + std::min<DWORD>(size, state ? 32 : 8));
    if (current != channel.last) {
        Log("rx %s", Hex(current.data(), current.size()).c_str());
        channel.last = current;
    }

    if (model_->protocol == DeviceProtocol::NovaPro && size >= 2 && data[0] == kNovaProEventReportId) {
        // Every event comes from someone using the GameDAC: its knob, buttons or menu.
        lastEventTick_ = GetTickCount64();
    }
    if (model_->protocol == DeviceProtocol::NovaPro && size >= 2 && data[0] == kNovaProEventReportId &&
        (data[1] == kNovaProVolume || data[1] == kNovaProChatMix)) {
        const DialMode mode = data[1] == kNovaProVolume ? DialMode::Volume : DialMode::ChatMix;
        if (dialMode_.exchange(mode) != mode) {
            PostMessageW(notify_, WM_APP_DIAL_MODE, 0, 0);
        }
    }

    QueueHeadsetUpdates(ParseHeadsetReport(*model_, data, size));

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
        DWORD timeout = polled && !pushEventsSeen_ ? kStatusPollMs : INFINITE;
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
                Send({kNovaStatusRequest});
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
