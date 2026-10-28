#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "balance.h"
#include "devices.h"
#include "headset.h"

// What the GameDAC knob currently adjusts. Inferred from its reports: pressing the knob sends
// nothing, but turning it reports 07 25 (volume) or 07 45 (ChatMix).
enum class DialMode {
    Unknown,
    Volume,
    ChatMix,
};

// HID collection that also accepts display drawing reports (GameDAC / base station OLED).
struct DisplayCollection {
    std::wstring path;
    USHORT featureLength = 0;
    USHORT outputLength = 0;
    uint8_t reportId = 0;
};

// HID link to the first connected headset from SupportedDevices().
class ChatMixDevice {
public:
    ChatMixDevice() = default;
    ChatMixDevice(const ChatMixDevice&) = delete;
    ChatMixDevice& operator=(const ChatMixDevice&) = delete;
    ~ChatMixDevice();

    // Opens the command and event collections and starts the reader thread.
    // Posts WM_APP_CHATMIX and WM_APP_DEVICE_LOST to notifyWindow.
    bool Open(HWND notifyWindow);
    void Close();
    bool IsOpen() const { return model_ != nullptr; }
    const DeviceModel* Model() const { return model_; }

    // Enables dial reporting and moves the device to the given levels where supported.
    void Start(Levels levels);
    // Returns the dial to its standalone function.
    void Stop();
    // Mirrors levels changed in the app on the device display (Nova Pro only).
    void SetLevels(Levels levels);

    // Returns the latest levels reported by the dial; false if nothing new arrived.
    bool TakeLevels(Levels& levels);
    // Posts WM_APP_DIAL_MODE when it changes; Unknown until the knob is turned.
    DialMode CurrentDialMode() const { return dialMode_.load(); }
    // The volume the knob was last turned to, in percent; -1 until then. Posts WM_APP_HEADSET_VOLUME on each turn.
    int CurrentVolume() const { return volume_.load(); }

    // Sends a native setting, e.g. {0x37, 7}; the report ID is added here.
    bool SendCommand(std::span<const uint8_t> payload);
    // Sends a feature report on the command collection, padded with zeros to its full length (Nova Elite equalizer).
    bool SendFeature(std::span<const uint8_t> payload);
    // Settings and battery reported by the headset since the last call (WM_APP_HEADSET).
    std::vector<HeadsetUpdate> TakeHeadsetUpdates();
    // Asks for the current settings where the headset can report them; answered as headset updates.
    void QueryState();

    // The collection to draw on, for headsets with a display.
    std::optional<DisplayCollection> Display() const;
    // GetTickCount64() of the last event caused by using the device itself; 0 if none since Open().
    ULONGLONG LastEventTick() const { return lastEventTick_.load(); }

private:
    struct Channel {
        HANDLE handle = INVALID_HANDLE_VALUE;
        std::vector<uint8_t> buffer;
        // Previous report of each kind, used to keep the log free of repeats: by the query an Arctis 7 reply names,
        // otherwise all under 0.
        std::map<uint8_t, std::vector<uint8_t>> last;
        OVERLAPPED overlapped{};
    };

    bool Send(std::initializer_list<uint8_t> payload) { return Send(std::span<const uint8_t>(payload.begin(), payload.size())); }
    // quiet: a repeated query that, like status polls, logs only its first failure and recovery.
    bool Send(std::span<const uint8_t> payload, bool quiet = false);
    // A query byte with the model's query argument, remembered for replies that do not name it.
    bool SendQuery(uint8_t query, bool quiet = false);
    // The battery, and whether the headset is on where the model tells that apart; quiet when repeated.
    void QueryBattery(bool quiet = false);
    void QueueHeadsetUpdates(std::vector<HeadsetUpdate> updates);
    static DWORD WINAPI ReaderProc(void* self);
    void ReadLoop();
    void HandleReport(Channel& channel, DWORD size);
    bool PublishLevels(Levels levels);

    const DeviceModel* model_ = nullptr;
    HWND notify_ = nullptr;
    HANDLE command_ = INVALID_HANDLE_VALUE;  // Synchronous handle for output reports.
    USHORT outputLength_ = 0;
    std::wstring commandPath_;
    USHORT featureLength_ = 0;
    uint8_t reportId_ = 0;
    std::vector<Channel> channels_;
    HANDLE stopEvent_ = nullptr;
    HANDLE reader_ = nullptr;

    static constexpr uint32_t kNoLevels = 0xFFFFFFFF;
    std::atomic<uint32_t> lastDeviceLevels_{kNoLevels};  // (game << 8) | chat as last seen from the device
    std::atomic<uint32_t> pendingLevels_{0};
    std::atomic<bool> levelsPending_{false};
    std::atomic<bool> pushEventsSeen_{false};
    std::atomic<bool> pollFailing_{false};
    std::atomic<DialMode> dialMode_{DialMode::Unknown};
    std::atomic<int> volume_{-1};
    std::atomic<ULONGLONG> lastEventTick_{0};
    std::atomic<uint8_t> lastQuery_{0};

    SRWLOCK updatesLock_ = SRWLOCK_INIT;
    std::vector<HeadsetUpdate> headsetUpdates_;
};
