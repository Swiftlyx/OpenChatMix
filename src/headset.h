#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "devices.h"
#include "i18n.h"

// Native headset settings (equalizer, microphone, display, power, wireless) per device family.
// Protocol details come from HeadsetControl, Linux-Arctis-Manager and captures of a Nova Pro Wired.

constexpr int kEqBands = 10;
using EqCurve = std::array<int8_t, kEqBands>;  // Half-decibel steps relative to flat.

enum class SettingKind {
    Slider,
    Choice,
    Toggle,
};

struct SettingOption {
    uint8_t value;
    Text text = Text::OptOff;
    int minutes = 0;                   // Non-zero: labelled "<minutes> min" instead of text.
    const wchar_t* literal = nullptr;  // Overrides text, e.g. "−12 dB".
};

struct SettingSpec {
    uint8_t command;  // Sent as [command, value] after the report ID; see SettingCommand().
    Text label;
    SettingKind kind;
    uint8_t min = 0;  // Slider range; Toggle uses min as off and max as on.
    uint8_t max = 0;
    std::span<const SettingOption> options = {};
    uint8_t shownWith = 0;  // Non-zero: shown only while that setting has the value below.
    uint8_t shownWithValue = 0;
};

// The GameDAC's stream mix is one command for three sliders: 47 [main] 00 [aux] [mic]. The aux and
// microphone sliders keep their values under these IDs, which no device command uses.
constexpr uint8_t kStreamMain = 0x47;
constexpr uint8_t kStreamAux = 0xE1;
constexpr uint8_t kStreamMic = 0xE2;

struct SettingSection {
    Text title;
    std::span<const SettingSpec> settings;
};

struct EqPreset {
    const wchar_t* name;  // SteelSeries preset names are product names and stay untranslated.
    EqCurve curve;
};

struct EqualizerSpec {
    uint8_t bandsCommand = 0;  // 0: the headset has no equalizer.
    uint8_t baseline = 0x14;   // Byte value for 0 dB; each step is 0.5 dB.
    int8_t range = 20;         // Maximum boost or cut in half-decibel steps.
    bool terminator = false;   // A 0x00 byte follows the bands.
    uint8_t presetCommand = 0;  // Non-zero: presets are stored on the device and selected by index.
    std::span<const EqPreset> presets = {};
    // The custom curve is preset index presets.size() (4 on every supported device).
    int CustomIndex() const { return static_cast<int>(presets.size()); }
};

struct SaveCommand {
    uint8_t bytes[2];
    uint8_t length;
};

struct HeadsetFeatures {
    std::span<const SettingSection> sections;
    EqualizerSpec equalizer;
    std::span<const SaveCommand> save;  // Sent after changes so the headset keeps them.
    uint8_t stateQuery = 0;              // Answered with the current settings (Nova Pro Wired).
    uint8_t batteryQuery = 0;            // Answered with the battery status (Nova Pro Wireless, dongles' b0).
    bool oled = false;                   // 128x64 display that accepts drawing reports (06 93).
    bool bluetooth = false;              // The battery reply also tells the base station's Bluetooth.

    bool HasSettings() const { return !sections.empty() || equalizer.bandsCommand != 0; }
    const SettingSpec* FindSetting(uint8_t command) const;
};

const HeadsetFeatures& FeaturesFor(DeviceFamily family);

// Bluetooth of a base station that has it (Nova Pro Wireless).
enum class BluetoothState { Unknown, Off, Pairing, On, Connected };

struct HeadsetState {
    std::array<int16_t, 256> values;  // Indexed by command; -1 while unknown.
    int eqPreset = -1;                // -1 unknown; CustomIndex() for the custom curve.
    EqCurve eqCurve{};                // The custom curve.
    bool eqCurveKnown = false;
    int battery = -1;                 // Percent; -1 unknown.
    bool charging = false;
    bool headsetOff = false;
    BluetoothState bluetooth = BluetoothState::Unknown;

    HeadsetState() { values.fill(-1); }
};

struct HeadsetUpdate {
    // ChatMix: the balance the device applied, game in value and chat in chat.
    // Bluetooth: the status reply's Bluetooth mode in value and its link in chat.
    enum class Kind { Setting, EqPreset, EqCurve, Battery, ChatMix, Bluetooth } kind = Kind::Setting;
    uint8_t command = 0;
    int value = 0;
    int chat = 0;
    EqCurve curve{};
    bool charging = false;
    bool headsetOff = false;
};

// Settings and battery reported by the headset, e.g. after a change on the GameDAC menu.
// Data starts with the report ID as in ParseChatMixReport.
std::vector<HeadsetUpdate> ParseHeadsetReport(const DeviceModel& model, const uint8_t* data, size_t size);
void ApplyUpdate(const HeadsetFeatures& features, HeadsetState& state, const HeadsetUpdate& update);

std::vector<uint8_t> EqualizerBandsCommand(const EqualizerSpec& spec, const EqCurve& curve);
// The bytes after the report ID that set one setting: [command, value], or the whole stream mix.
std::vector<uint8_t> SettingCommand(const HeadsetState& state, uint8_t command, int value);

// Valid values of a setting in display order.
std::vector<uint8_t> SettingValues(const SettingSpec& spec);
