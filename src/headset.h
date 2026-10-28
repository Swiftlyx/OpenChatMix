#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "devices.h"
#include "i18n.h"
#include "parametric_eq.h"

// Native headset settings (equalizer, microphone, display, power, wireless) per device family.
// Protocol details come from HeadsetControl, Linux-Arctis-Manager and captures of a Nova Pro Wired.

constexpr int kEqBands = 10;
using EqCurve = std::array<int8_t, kEqBands>;  // Half-decibel steps relative to flat.
// The filters of a parametric headset equalizer (Nova 7 Gen 2, Nova Elite): type, frequency, gain and Q each.
static_assert(kEqBands == kParametricBands);
using EqFilters = std::array<EqBand, kEqBands>;
// Limits of the parametric headsets' filters, as GG sets them.
inline constexpr float kHeadsetFilterMaxGain = 12.0f;

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
    Prefixed,       // [prefix] [value] (the Arctis Pro Wireless' AA, the Arctis 7 lights' 01, the Arctis 9's 00).
    PaddedLevel,    // [on] 00 [level]: 0 is off (sidetone of the Arctis 7 and Arctis 1).
    Seconds,        // [prefix] [seconds, 16 bits big-endian] of a value in minutes (Arctis 9 auto-off).
    Trailing,       // [value] [prefix] [suffix]: two fixed bytes after it (Nova 4 mute light).
    // A 16-bit register of the Arctis 5's chip: 40 02 [prefix: address high] [command: address low] [value scaled
    // from 0-max to 0-7FFF, big-endian].
    Register16,
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
    uint8_t prefix = 0;  // Prefixed and Seconds: the byte between the command and the value; Trailing: after it.
    uint8_t suffix = 0;  // Trailing: the last byte.
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
    const char* alias = nullptr;  // GG's short name for the preset, which the Nova Elite takes along with it.
    int8_t slot = -1;             // The Nova Elite's onboard slot GG writes the preset to; -1: its index.
};

// The Nova Elite's equalizers besides the main one, each with ten fixed bands.
enum class ExtraEqualizer : uint8_t { Bluetooth, Microphone };
inline constexpr int kExtraEqualizers = 2;

// How a parametric filter's gain is written.
enum class FilterGain : uint8_t {
    Tenths,        // Signed, in 0.1 dB.
    HalfDecibels,  // 20 plus two steps a decibel, ±10 dB (Nova 5).
};

// How the equalizer curve reaches the headset.
enum class EqFormat : uint8_t {
    Graphic,     // [bandsCommand] [10 levels]: baseline plus half-decibel steps.
    Parametric,  // Ten filters after the bands command, as the spec's parametric fields say (see EqualizerCommands).
    Feature,     // Nova Elite: one feature report with the preset's names and ten filters.
    FeatureGains,  // Nova Elite Bluetooth: the same report with ten gains of fixed bands instead of filters.
    // Arctis 7, Arctis 1 Wireless and Arctis 5: the computer works out the filter of each fixed band and writes it
    // into the sound chip, which keeps nothing over a reconnect (see ChipBandCommands).
    Chip,
};

// The sound chips of EqFormat::Chip.
enum class SoundChip : uint8_t {
    Av6x02,   // Arctis 7 and Arctis 1 Wireless dongles: six bands, float coefficients, both channels.
    Cx20892,  // Arctis 5: five bands, 16-bit coefficients, then a commit.
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
    bool notch = false;         // Parametric: the headset has a notch filter type of its own.
    // Parametric: only peaks are known to play right. The Nova 7 Gen 2 whistles loudly with some shelves and passes
    // (issue #7), so other types become peaks before they are sent.
    bool peaksOnly = false;
    uint8_t customSlot = 4;     // Nova Elite: the onboard slot of the custom setting.
    // Fixed bands of their own, as labelled under the sliders, in the first entries of a curve; empty: the ten
    // octave bands.
    std::span<const wchar_t* const> bandLabels = {};
    // Parametric: the command that names the filters before they go out and the one that applies them after (0:
    // none), whether the bands command carries the link byte, the byte order of the frequency and the gain's form.
    uint8_t nameCommand = 0;
    uint8_t applyCommand = 0;
    bool linkByte = false;
    bool bigEndianFrequency = false;
    FilterGain filterGain = FilterGain::Tenths;
    SoundChip chip = SoundChip::Av6x02;  // Chip.
    // The custom curve is preset index presets.size().
    int CustomIndex() const { return static_cast<int>(presets.size()); }
    int Bands() const { return bandLabels.empty() ? kEqBands : static_cast<int>(bandLabels.size()); }
    // Nova Elite: the onboard slot of a preset index or of the custom setting; -1 for neither.
    int SlotOf(int index) const {
        if (index >= 0 && index < CustomIndex()) {
            return presets[index].slot >= 0 ? presets[index].slot : index;
        }
        return index == CustomIndex() ? customSlot : -1;
    }
    // The preset index of an onboard slot, the custom one included; -1 when no preset has it.
    int IndexOfSlot(int slot) const {
        for (int index = 0; index <= CustomIndex(); ++index) {
            if (SlotOf(index) == slot) {
                return index;
            }
        }
        return -1;
    }
    // Filters of any frequency, type and Q instead of ten fixed bands; the custom setting is EqFilters.
    bool Parametric() const { return format == EqFormat::Parametric || format == EqFormat::Feature; }
    // Sent as a feature report rather than output reports.
    bool SentAsFeature() const { return format == EqFormat::Feature || format == EqFormat::FeatureGains; }
};

struct SaveCommand {
    uint8_t bytes[6];
    uint8_t length;
};

struct HeadsetFeatures {
    std::span<const SettingSection> sections;
    EqualizerSpec equalizer;
    EqualizerSpec bluetoothEqualizer;    // Of the sound over Bluetooth, apart from the main one (Nova Elite).
    EqualizerSpec microphoneEqualizer;   // Of the microphone, in the headset itself (Nova Elite).
    std::span<const SaveCommand> save;  // Sent after changes so the headset keeps them.
    unsigned saveDelayMs = 0;            // Quiet time after the last change before the save; 0 for the default.
    uint8_t stateQuery = 0;              // Answered with the current settings (Nova Pro Wired).
    uint8_t batteryQuery = 0;            // Answered with the battery status (Nova Pro Wireless, dongles' b0).
    uint8_t linkQuery = 0;               // Answered with whether the headset is on, asked with the battery (Arctis 7).
    uint8_t queryArgument = 0;          // Sent after each query byte (AA on the Arctis Pro Wireless); 0 for none.
    // The battery is asked for once the link reply says the headset is on, as neither reply names its query
    // (Arctis Pro Wireless); otherwise both are asked for together.
    bool batteryAfterLink = false;
    // The dongle answers 0% while the headset is off (original Arctis 7): until a link reply tells it, 0% that
    // repeats counts as switched off, and any other battery as on.
    bool offAtZeroBattery = false;
    unsigned batteryPollMs = 0;          // How often the battery and link are asked for; 0 for the default.
    bool oled = false;                   // 128x64 display that accepts drawing reports (06 93).
    bool bluetooth = false;              // The battery reply also tells the base station's Bluetooth.
    bool experimental = false;           // Settings written from protocol descriptions only; the tab says so.
    bool volumeKnob = false;             // A knob that sets the volume itself and reports it (07 25).
    bool chargingCase = false;           // The spare battery is that of a charging case of its own (GameBuds).

    bool HasSettings() const { return !sections.empty() || equalizer.bandsCommand != 0; }
    const EqualizerSpec& Extra(ExtraEqualizer which) const {
        return which == ExtraEqualizer::Bluetooth ? bluetoothEqualizer : microphoneEqualizer;
    }
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
    EqFilters eqFilters{};            // The custom filters of a parametric equalizer.
    bool eqFiltersKnown = false;
    // The extra equalizers, by ExtraEqualizer, as eqPreset, eqCurve and eqCurveKnown.
    struct ExtraEq {
        int preset = -1;
        EqCurve curve{};
        bool curveKnown = false;
    };
    std::array<ExtraEq, kExtraEqualizers> extraEq{};
    int battery = -1;                 // Percent; -1 unknown.
    int spareBattery = -1;            // Percent of the spare battery in the base station; -1 unknown or none.
    bool charging = false;
    bool headsetOff = false;
    bool linkSeen = false;            // A link reply came, which then alone tells whether the headset is on.
    int zeroBatteryReads = 0;         // 0% replies in a row (HeadsetFeatures::offAtZeroBattery).
    BluetoothState bluetooth = BluetoothState::Unknown;

    HeadsetState() { values.fill(-1); }
};

struct HeadsetUpdate {
    // ChatMix: the balance the device applied, game in value and chat in chat.
    // Bluetooth: the status reply's Bluetooth mode in value and its link in chat.
    // Link: whether the headset is on, in headsetOff, from a reply that tells nothing about the battery.
    enum class Kind { Setting, EqPreset, EqCurve, Battery, ChatMix, Bluetooth, Link } kind = Kind::Setting;
    uint8_t command = 0;
    int value = 0;
    int chat = 0;
    EqCurve curve{};
    bool charging = false;
    bool headsetOff = false;
    int spare = -1;  // Battery: the spare battery's percent, where the base station has one.
    bool fromEvent = false;  // Setting: changed on the headset or base station, not read from a status reply.
    bool linkKnown = true;   // Battery: headsetOff holds; false when the reply does not tell (Arctis 7).
};

// Settings and battery reported by the headset, e.g. after a change on the GameDAC menu.
// Data starts with the report ID as in ParseChatMixReport. lastQuery: the query sent last, for replies that do not
// repeat it (Arctis Pro Wireless).
std::vector<HeadsetUpdate> ParseHeadsetReport(const DeviceModel& model, const uint8_t* data, size_t size,
                                              uint8_t lastQuery = 0);
void ApplyUpdate(const HeadsetFeatures& features, HeadsetState& state, const HeadsetUpdate& update);
// The noise control command of base stations that have it (off, transparency, ANC).
inline constexpr uint8_t kNoiseControlCommand = 0xBD;

// The commands that set the curve, in order: one for a graphic equalizer, three for a parametric one, one feature
// report (without its report ID) for the Nova Elite, those of every band for a sound chip. preset: the index of the
// built-in preset the curve is, which the Nova Elite takes along by name; -1 for the custom curve.
std::vector<std::vector<uint8_t>> EqualizerCommands(const EqualizerSpec& spec, const EqCurve& curve, int preset = -1);
// The commands that set one band of a sound chip (EqFormat::Chip) to a gain in dB.
std::vector<std::vector<uint8_t>> ChipBandCommands(const EqualizerSpec& spec, int band, float gainDb);
// The command that makes the chip take the bands written, where it needs one; empty otherwise.
std::vector<uint8_t> ChipCommit(const EqualizerSpec& spec);
// The same for the filters of a parametric equalizer.
std::vector<std::vector<uint8_t>> EqualizerCommands(const EqualizerSpec& spec, const EqFilters& filters,
                                                    int preset = -1);
// A curve as filters: peaks at the usual octave centres with a Q of one octave; with shelves, the lowest and
// highest band are shelves instead.
EqFilters CurveFilters(const EqCurve& curve, int8_t range, bool shelves = false);
// A built-in preset as GG sets it on a parametric headset: shelves at both ends and peaks between, Flat all peaks;
// all peaks on a headset that plays only peaks.
EqFilters BuiltinPresetFilters(const EqualizerSpec& spec, int index);
// The bands of a preset as the headset's filters, within its limits. Without a notch type of its own, a notch
// becomes the deepest peak cut.
EqFilters PresetFilters(const ParametricEq& eq, bool notch = false);
// Filters as the headset plays them, within all its limits: as PresetFilters, and on a headset that plays only peaks
// a shelf becomes a peak with its gain and a low or high pass a peak without gain.
EqFilters HeadsetFilters(const EqFilters& filters, const EqualizerSpec& spec);
// The filters the headset plays now: a preset's curve as filters, or the custom ones.
EqFilters ActiveFilters(const EqualizerSpec& spec, const HeadsetState& state);
// The bytes after the report ID that set one setting: [command, value], or the whole stream mix.
std::vector<uint8_t> SettingCommand(const HeadsetFeatures& features, const HeadsetState& state, uint8_t command,
                                    int value);

// Valid values of a setting in display order.
std::vector<uint8_t> SettingValues(const SettingSpec& spec);
