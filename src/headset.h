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

// How a setting's value goes on the wire after its command byte.
enum class SettingWire : uint8_t {
    Byte,           // [value]
    SwitchedLevel,  // [on] [level]: 0 is off at the last level, 1..max is on at that level (Nova Elite sidetone).
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
    SettingWire wire = SettingWire::Byte;
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

// How the equalizer curve reaches the headset.
enum class EqFormat : uint8_t {
    Graphic,     // [bandsCommand] [10 levels]: baseline plus half-decibel steps.
    Parametric,  // Nova 7 Gen 2 protocol: a preset name, ten filters, then a commit (see EqualizerCommands).
};

struct EqualizerSpec {
    uint8_t bandsCommand = 0;  // 0: the headset has no equalizer.
    uint8_t baseline = 0x14;   // Byte value for 0 dB; each step is 0.5 dB.
    int8_t range = 20;         // Maximum boost or cut in half-decibel steps.
    bool terminator = false;   // A 0x00 byte follows the bands.
    uint8_t presetCommand = 0;  // Non-zero: presets are stored on the device and selected by index.
    std::span<const EqPreset> presets = {};
    EqFormat format = EqFormat::Graphic;
    bool experimental = false;  // Written from protocol descriptions only, not yet confirmed on the headset.
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
    unsigned saveDelayMs = 0;            // Quiet time after the last change before the save; 0 for the default.
    uint8_t stateQuery = 0;              // Answered with the current settings (Nova Pro Wired).
    uint8_t batteryQuery = 0;            // Answered with the battery status (Nova Pro Wireless, dongles' b0).
    bool oled = false;                   // 128x64 display that accepts drawing reports (06 93).
    bool bluetooth = false;              // The battery reply also tells the base station's Bluetooth.
    bool experimental = false;           // Settings written from protocol descriptions only; the tab says so.
    bool volumeKnob = false;             // A knob that sets the volume itself and reports it (07 25).

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
    int spareBattery = -1;            // Percent of the spare battery in the base station; -1 unknown or none.
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
    int spare = -1;  // Battery: the spare battery's percent, where the base station has one.
    bool fromEvent = false;  // Setting: changed on the headset or base station, not read from a status reply.
};

// Settings and battery reported by the headset, e.g. after a change on the GameDAC menu.
// Data starts with the report ID as in ParseChatMixReport.
std::vector<HeadsetUpdate> ParseHeadsetReport(const DeviceModel& model, const uint8_t* data, size_t size);
void ApplyUpdate(const HeadsetFeatures& features, HeadsetState& state, const HeadsetUpdate& update);
// The noise control command of base stations that have it (off, transparency, ANC).
inline constexpr uint8_t kNoiseControlCommand = 0xBD;

// The commands that set the curve, in order: one for a graphic equalizer, three for a parametric one.
std::vector<std::vector<uint8_t>> EqualizerCommands(const EqualizerSpec& spec, const EqCurve& curve);
// The bytes after the report ID that set one setting: [command, value], or the whole stream mix.
std::vector<uint8_t> SettingCommand(const HeadsetFeatures& features, const HeadsetState& state, uint8_t command,
                                    int value);

// Valid values of a setting in display order.
std::vector<uint8_t> SettingValues(const SettingSpec& spec);
