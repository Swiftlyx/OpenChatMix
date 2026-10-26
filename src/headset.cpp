#include "headset.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace {

using enum Text;

constexpr SettingOption kFourLevels[] = {{0, OptOff}, {1, OptLow}, {2, OptMedium}, {3, OptHigh}};
constexpr SettingOption kGain[] = {{1, OptLow}, {2, OptHigh}};
// Nova Pro timeouts are indices into fixed minute steps.
constexpr SettingOption kNovaProTimeouts[] = {{0, OptNever},        {1, OptNever, 1},  {2, OptNever, 5},
                                              {3, OptNever, 10},    {4, OptNever, 15}, {5, OptNever, 30},
                                              {6, OptNever, 60}};
// Nova 7 / 7+ / 5X timeouts are minutes.
constexpr SettingOption kMinuteTimeouts[] = {{0, OptNever},      {1, OptNever, 1},   {5, OptNever, 5},
                                             {10, OptNever, 10}, {15, OptNever, 15}, {30, OptNever, 30},
                                             {45, OptNever, 45}, {60, OptNever, 60}, {75, OptNever, 75},
                                             {90, OptNever, 90}};
constexpr SettingOption kWirelessMode[] = {{0, OptSpeed}, {1, OptRange}};
constexpr SettingOption kLineOutModes[] = {{1, OptSpeakers}, {2, OptStreaming}};
constexpr SettingOption kNova5XMicLed[] = {{0, OptOff}, {1, OptLow}, {4, OptMedium}, {10, OptHigh}};
constexpr SettingOption kCallMute[] = {{0, OptOff}, {1, OptOff, 0, L"−12 dB"}, {2, OptMute}};

// Nova Pro Wired (GameDAC Gen 2).
constexpr SettingSpec kNovaProMicrophone[] = {
    {0x37, MicVolume, SettingKind::Slider, 1, 10},
    {0x39, Sidetone, SettingKind::Choice, 0, 3, kFourLevels},
};
constexpr SettingSpec kNovaProSound[] = {
    {0x27, Gain, SettingKind::Choice, 1, 2, kGain},
};
constexpr SettingSpec kNovaProDisplay[] = {
    {0x85, Brightness, SettingKind::Slider, 1, 10},
    {0x83, ScreenDim, SettingKind::Choice, 0, 6, kNovaProTimeouts},
};
// The GameDAC's line out: the headset sound for speakers, or a stream mix of its own.
constexpr uint8_t kLineOutCommand = 0x43;
constexpr uint8_t kLineOutStreaming = 2;
constexpr SettingSpec kNovaProLineOut[] = {
    {kLineOutCommand, LineOutMode, SettingKind::Choice, 1, 2, kLineOutModes},
    {kStreamMain, StreamMain, SettingKind::Slider, 0, 100, {}, kLineOutCommand, kLineOutStreaming},
    {kStreamAux, StreamAux, SettingKind::Slider, 0, 100, {}, kLineOutCommand, kLineOutStreaming},
    {kStreamMic, StreamMic, SettingKind::Slider, 0, 100, {}, kLineOutCommand, kLineOutStreaming},
};
constexpr SettingSection kNovaProWiredSections[] = {
    {Microphone, kNovaProMicrophone},
    {Sound, kNovaProSound},
    {LineOut, kNovaProLineOut},
    {Display, kNovaProDisplay},
};

// Noise control of the Nova Pro Wireless and Nova Elite base stations: off, transparency or ANC, with the
// transparency level (and on the Elite the ANC level).
constexpr uint8_t kNoiseMode = kNoiseControlCommand;
constexpr uint8_t kNoiseTransparent = 1;
constexpr uint8_t kNoiseCancelling = 2;
constexpr uint8_t kTransparencyLevel = 0xB9;
constexpr SettingOption kNoiseModes[] = {{0, OptOff}, {kNoiseTransparent, OptTransparency},
                                         {kNoiseCancelling, OptNoiseCancelling}};
// Bluetooth of the base stations: on together with the headset, and what a call does to the game.
constexpr SettingSpec kStationBluetooth[] = {
    {0xB2, BluetoothPowerOn, SettingKind::Toggle, 0, 1},
    {0xB3, BluetoothAutoMute, SettingKind::Choice, 0, 2, kCallMute},
};

// Nova Pro Wireless base station. Noise control and Bluetooth as NovaPanel documents them for the 12E0.
constexpr SettingSpec kNovaProWirelessMicrophone[] = {
    {0x37, MicVolume, SettingKind::Slider, 1, 10},
    {0x39, Sidetone, SettingKind::Choice, 0, 3, kFourLevels},
    {0xBF, MicLed, SettingKind::Slider, 1, 10},
};
constexpr SettingSpec kNovaProWirelessPower[] = {
    {0xC1, AutoOff, SettingKind::Choice, 0, 6, kNovaProTimeouts},
};
constexpr SettingSpec kNovaProWirelessRadio[] = {
    {0xC3, WirelessMode, SettingKind::Choice, 0, 1, kWirelessMode},
};
constexpr SettingSpec kNovaProWirelessNoise[] = {
    {kNoiseMode, NoiseMode, SettingKind::Choice, 0, 2, kNoiseModes},
    {kTransparencyLevel, TransparencyLevel, SettingKind::Slider, 1, 10, {}, kNoiseMode, kNoiseTransparent},
};
constexpr SettingSection kNovaProWirelessSections[] = {
    {Microphone, kNovaProWirelessMicrophone},
    {NoiseControl, kNovaProWirelessNoise},
    {Sound, kNovaProSound},
    {LineOut, kNovaProLineOut},
    {Display, kNovaProDisplay},
    {Power, kNovaProWirelessPower},
    {Wireless, kNovaProWirelessRadio},
    {Bluetooth, kStationBluetooth},
};

// Nova 7 family.
constexpr SettingSpec kNova7Microphone[] = {
    {0x37, MicVolume, SettingKind::Slider, 0, 7},
    {0x39, Sidetone, SettingKind::Choice, 0, 3, kFourLevels},
    {0xAE, MicLed, SettingKind::Choice, 0, 3, kFourLevels},
};
constexpr SettingSpec kNova7Sound[] = {
    {0x3A, VolumeLimiter, SettingKind::Toggle, 0, 1},
};
constexpr SettingSpec kMinutePower[] = {
    {0xA3, AutoOff, SettingKind::Choice, 0, 90, kMinuteTimeouts},
};
constexpr SettingSpec kNova7Bluetooth[] = {
    {0xB2, BluetoothPowerOn, SettingKind::Toggle, 0, 1},
    {0xB3, BluetoothAutoMute, SettingKind::Choice, 0, 2, kCallMute},
};
constexpr SettingSection kNova7Sections[] = {
    {Microphone, kNova7Microphone},
    {Sound, kNova7Sound},
    {Power, kMinutePower},
    {Bluetooth, kNova7Bluetooth},
};

// Arctis 7+ family.
constexpr SettingSpec kArctis7PlusMicrophone[] = {
    {0x39, Sidetone, SettingKind::Choice, 0, 3, kFourLevels},
};
constexpr SettingSection kArctis7PlusSections[] = {
    {Microphone, kArctis7PlusMicrophone},
    {Power, kMinutePower},
};

// Nova 5X.
constexpr SettingSpec kNova5XMicrophone[] = {
    {0x37, MicVolume, SettingKind::Slider, 0, 15},
    {0x39, Sidetone, SettingKind::Slider, 0, 10},
    {0xAE, MicLed, SettingKind::Choice, 0, 10, kNova5XMicLed},
};
constexpr SettingSpec kNova5XSound[] = {
    {0x27, VolumeLimiter, SettingKind::Toggle, 0, 1},
};
constexpr SettingSection kNova5XSections[] = {
    {Microphone, kNova5XMicrophone},
    {Sound, kNova5XSound},
    {Power, kMinutePower},
};

// SteelSeries Engine EQ presets, as GG keeps them in Engine's database and sends them to the Nova Elite (both of
// its equalizers, issue #6), with GG's short names. The Nova Pro stores them on the device (indices 0-3).
constexpr EqPreset kNova7Presets[] = {
    {L"Flat", {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, "FLAT"},
    {L"Bass Boost", {7, 11, 8, 2, -3, -3, -2, -2, -2, -2}, "BASS"},
    {L"Focus", {-10, -7, -2, -7, -5, 8, 12, 7, -7, 0}, "FOCUS"},
    {L"Smiley", {6, 7, 3, -3, -8, -8, -5, 3, 6, 8}, "SMILEY"},
};
constexpr EqPreset kArctis7PlusPresets[] = {
    {L"Flat", {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
    {L"Bass Boost", {7, 8, 2, -3, -3, -2, -2, -2, -2, 11}},
    {L"Smiley", {6, 3, -3, -8, -8, -5, 3, 6, 8, 7}},
    {L"Focus", {-10, -2, -7, -5, 8, 12, 7, -7, 0, -7}},
};

constexpr SaveCommand kSaveState[] = {{{0x09}, 1}};
constexpr SaveCommand kNova5XSave[] = {{{0x09}, 1}, {{0x35, 0x01}, 2}};

const HeadsetFeatures kNovaProWired{
    .sections = kNovaProWiredSections,
    .equalizer = {.bandsCommand = 0x33, .baseline = 0x14, .range = 20, .presetCommand = 0x2E, .presets = kNova7Presets},
    .stateQuery = 0x20,
    .oled = true,
    .volumeKnob = true,
};
const HeadsetFeatures kNovaProWireless{
    .sections = kNovaProWirelessSections,
    .equalizer = {.bandsCommand = 0x33, .baseline = 0x14, .range = 20, .presetCommand = 0x2E, .presets = kNova7Presets},
    .save = kSaveState,
    .batteryQuery = 0xB0,
    .oled = true,
    .bluetooth = true,
    .volumeKnob = true,
};
// The dongles answer the b0 status request with the battery too (see ParseStatusBattery).
constexpr uint8_t kStatusRequest = 0xB0;

const HeadsetFeatures kNova7{
    .sections = kNova7Sections,
    .equalizer = {.bandsCommand = 0x33, .baseline = 0x14, .range = 20, .terminator = true, .presets = kNova7Presets},
    .batteryQuery = kStatusRequest,
};
const HeadsetFeatures kNova7Gen2{
    .sections = kNova7Sections,
    .equalizer = {.bandsCommand = 0x33,
                  .range = 20,
                  .presets = kNova7Presets,
                  .format = EqFormat::Parametric,
                  .experimental = true},
    .save = kSaveState,
    // The sound pops while these headsets save (issue #7), so a run of changes is saved once, after it ends.
    .saveDelayMs = 3000,
    .batteryQuery = kStatusRequest,
};
const HeadsetFeatures kArctis7Plus{
    .sections = kArctis7PlusSections,
    .equalizer = {.bandsCommand = 0x33, .baseline = 0x18, .range = 24, .terminator = true, .presets = kArctis7PlusPresets},
    .save = kSaveState,
    .batteryQuery = kStatusRequest,
};
const HeadsetFeatures kNova5X{
    .sections = kNova5XSections,
    .save = kNova5XSave,
    .batteryQuery = kStatusRequest,
};
// Nova Elite base station (experimental): the commands of Linux-Arctis-Manager's device description, with the
// value ranges the base station reports in its own events (the log in issue #6). The stream mix and line out
// work as on the GameDAC.
constexpr uint8_t kEliteDisplayQuery = 0x80;  // Answered with the screen settings.
constexpr uint8_t kEliteBoomSidetone = 0x38;
constexpr uint8_t kEliteOnEarSidetone = 0x39;
constexpr SettingOption kThreeLevels[] = {{1, OptLow}, {2, OptMedium}, {3, OptHigh}};
constexpr SettingOption kHomeScreenViews[] = {{0, OptDetailed}, {1, OptSimple}};
constexpr SettingSpec kEliteMicrophone[] = {
    {0x37, MicVolume, SettingKind::Slider, 1, 10},
    {kEliteBoomSidetone, SidetoneBoomMic, SettingKind::Slider, 0, 10, {}, 0, 0, SettingWire::SwitchedLevel},
    {kEliteOnEarSidetone, SidetoneOnEarMic, SettingKind::Slider, 0, 10, {}, 0, 0, SettingWire::SwitchedLevel},
    {0xBF, MicLed, SettingKind::Slider, 0, 10},
};
constexpr SettingSpec kEliteNoise[] = {
    {kNoiseMode, NoiseMode, SettingKind::Choice, 0, 2, kNoiseModes},
    {0xB8, NoiseCancellingLevel, SettingKind::Choice, 1, 3, kThreeLevels, kNoiseMode, kNoiseCancelling},
    {kTransparencyLevel, TransparencyLevel, SettingKind::Slider, 1, 10, {}, kNoiseMode, kNoiseTransparent},
};
constexpr SettingSpec kEliteSound[] = {
    {0x27, VolumeLimiter, SettingKind::Toggle, 0, 1},
};
constexpr SettingSpec kEliteDisplay[] = {
    {0x85, Brightness, SettingKind::Slider, 1, 10},
    {0x83, ScreenDim, SettingKind::Choice, 0, 6, kNovaProTimeouts},
    {0x89, HomeScreen, SettingKind::Choice, 0, 1, kHomeScreenViews},
};
constexpr SettingSection kEliteSections[] = {
    {Microphone, kEliteMicrophone},
    {NoiseControl, kEliteNoise},
    {Sound, kEliteSound},
    {LineOut, kNovaProLineOut},
    {Display, kEliteDisplay},
    {Power, kNovaProWirelessPower},
    {Bluetooth, kStationBluetooth},
};
// The Elite's equalizers: 01 1b for the main one, 01 1f for the sound over Bluetooth and 01 1d for the microphone,
// each a 1036-byte feature report, as GG sends them (captures in issue #6, see EqualizerCommands).
constexpr uint8_t kEliteEqCommand = 0x1B;
constexpr uint8_t kEliteBluetoothEqCommand = 0x1F;
constexpr uint8_t kEliteMicEqCommand = 0x1D;
// The microphone presets GG offers on the Elite, in its order, with the slots it writes them to and in half-decibel
// steps (captures in issue #6; GG cuts their -12 dB to the equalizer's -10 dB). GG writes the custom setting to
// slot 8.
constexpr EqPreset kEliteMicPresets[] = {
    {L"Flat", {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, "FLAT", 0},
    {L"Balanced", {-20, 0, 0, -6, -4, 0, 4, 6, 2, 0}, "BAL", 1},
    {L"Broadcast High Pitch", {-20, -20, -6, 12, 6, -5, 0, 6, 8, 8}, "BC HI", 2},
    {L"Broadcast Low Pitch", {-20, 4, 10, 4, -6, -4, 0, 6, 8, 8}, "BC LO", 3},
    {L"Clarity High Pitch", {-20, -20, -8, -8, 0, 0, 6, 8, 8, 8}, "CT HI", 5},
    {L"Clarity Low Pitch", {-20, -6, -5, 0, 0, 0, 6, 8, 8, 8}, "CT LO", 4},
    {L"Deep Voice", {-20, 8, 8, 6, 0, -6, -2, 0, 0, 0}, "DEEP", 6},
    {L"Less Nasal", {-20, 0, 0, 0, -4, -8, -4, 4, 8, 0}, "NASAL-", 7},
};
constexpr uint8_t kEliteMicCustom = 8;
const HeadsetFeatures kNovaElite{
    .sections = kEliteSections,
    .equalizer = {.bandsCommand = kEliteEqCommand,
                  .range = 20,
                  .presets = kNova7Presets,
                  .format = EqFormat::Feature,
                  .notch = true},
    .bluetoothEqualizer = {.bandsCommand = kEliteBluetoothEqCommand,
                           .range = 20,
                           .presets = kNova7Presets,
                           .format = EqFormat::FeatureGains},
    .microphoneEqualizer = {.bandsCommand = kEliteMicEqCommand,
                            .range = 20,
                            .presets = kEliteMicPresets,
                            .format = EqFormat::FeatureGains,
                            .customSlot = kEliteMicCustom},
    .save = kSaveState,
    .stateQuery = kEliteDisplayQuery,
    .batteryQuery = kStatusRequest,
    .experimental = true,
    .volumeKnob = true,
};

constexpr uint8_t kNovaProEventReportId = 0x07;
constexpr uint8_t kNovaProCommandReportId = 0x06;
constexpr uint8_t kNovaProState = 0x20;
constexpr uint8_t kNovaProBattery = 0xB0;
constexpr uint8_t kNovaProBatteryEvent = 0xB7;
constexpr uint8_t kNovaProHeadsetOff = 0x01;  // Headset state in 06 b0 byte 15 and 07 b7 byte 4.
constexpr uint8_t kNovaProCharging = 0x02;
constexpr uint8_t kGainCommand = 0x27;
constexpr uint8_t kMicVolumeCommand = 0x37;
constexpr uint8_t kSidetoneCommand = 0x39;

// Nova 7 Gen 2 parametric equalizer, as Linux-Arctis-Manager describes the protocol: A7 [link] [kind] [name],
// then 33 [link] and ten filters of six bytes each (see AppendFilter, frequency big-endian), then 27 to apply them.
constexpr uint8_t kGen2EqName = 0xA7;
constexpr uint8_t kGen2EqCommit = 0x27;
constexpr uint8_t kGen2Radio = 0x00;         // The 2.4 GHz link; 1 is Bluetooth, 2 the microphone.
constexpr uint8_t kGen2CustomPreset = 0x01;  // 0 is a SteelSeries preset.
constexpr char kGen2PresetName[] = "OpenChatMix";
// A curve's ten bands as filters: peaks at the usual octave centres with a Q of one octave.
constexpr float kOctaveFrequencies[kEqBands] = {32, 64, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
constexpr float kOctaveQ = 1.414f;

// Nova Elite equalizers, as GG sets them (captures in issue #6): 1b, 1f or 1d [kind] [alias, 6 bytes] [name, 61
// bytes], then for 1b ten filters of six bytes each (see AppendFilter, frequency little-endian), for 1f (Bluetooth)
// and 1d (microphone) ten gains of fixed bands (signed, in 0.1 dB); the rest of the report stays zero. Kind is the
// onboard slot of a built-in preset (0 Flat, 1 Bass Boost, 2 Focus, 3 Smiley, with their names; the microphone's
// own), EqualizerSpec::customSlot with the alias "CUSTOM" the custom setting (4 on the main and Bluetooth
// equalizers, 8 on the microphone's), 5 one of GG's game presets.
constexpr char kEliteEqAlias[] = "CUSTOM";
constexpr size_t kEliteEqAliasLength = 6;
constexpr size_t kEliteEqNameLength = 61;

// Filter types on the wire, as GG sends its presets (captures in issue #6): peak, low pass, high pass, low shelf,
// high shelf and, on the Nova Elite, notch.
constexpr uint8_t kWirePeak = 1;
constexpr uint8_t kWireLowPass = 2;
constexpr uint8_t kWireHighPass = 3;
constexpr uint8_t kWireLowShelf = 4;
constexpr uint8_t kWireHighShelf = 5;
constexpr uint8_t kWireNotch = 6;
// A band that is off, as GG sends it: a peak at this frequency without gain.
constexpr uint16_t kWireBandOff = 20001;

uint8_t WireType(::FilterType type) {
    switch (type) {
    case ::FilterType::LowPass: return kWireLowPass;
    case ::FilterType::HighPass: return kWireHighPass;
    case ::FilterType::LowShelf: return kWireLowShelf;
    case ::FilterType::HighShelf: return kWireHighShelf;
    case ::FilterType::Notch: return kWireNotch;
    case ::FilterType::Peaking: break;
    }
    return kWirePeak;
}

// One filter in six bytes: frequency (u16), type, gain (signed, in 0.1 dB) and Q (x1000, u16 little-endian).
// notchType: the headset has a notch type, which GG sends at the deepest cut; without one, like PresetFilters, the
// deepest peak cut stands in for it.
void AppendFilter(std::vector<uint8_t>& out, EqBand band, bool bigEndianFrequency, bool notchType) {
    const bool notch = band.type == ::FilterType::Notch;
    if (notch && !notchType) {
        band.type = ::FilterType::Peaking;
    }
    const float gain = notch ? -kHeadsetFilterMaxGain : UsesGain(band.type) ? band.gain : 0.0f;
    const uint16_t frequency =
        band.enabled ? static_cast<uint16_t>(std::lround(std::clamp(band.frequency, kEqMinFrequency, kEqMaxFrequency)))
                     : kWireBandOff;
    const int tenths =
        band.enabled ? std::lround(std::clamp(gain, -kHeadsetFilterMaxGain, kHeadsetFilterMaxGain) * 10.0f) : 0;
    const uint16_t q = static_cast<uint16_t>(std::lround(std::clamp(band.q, kEqMinQ, kEqMaxQ) * 1000.0f));
    const uint8_t frequencyHigh = static_cast<uint8_t>(frequency >> 8);
    const uint8_t frequencyLow = static_cast<uint8_t>(frequency & 0xFF);
    out.push_back(bigEndianFrequency ? frequencyHigh : frequencyLow);
    out.push_back(bigEndianFrequency ? frequencyLow : frequencyHigh);
    out.push_back(band.enabled ? WireType(band.type) : kWirePeak);
    out.push_back(static_cast<uint8_t>(static_cast<int8_t>(tenths)));
    out.push_back(static_cast<uint8_t>(q & 0xFF));
    out.push_back(static_cast<uint8_t>(q >> 8));
}

EqCurve CurveFromBytes(const uint8_t* bytes, uint8_t baseline, int8_t range) {
    EqCurve curve{};
    for (int band = 0; band < kEqBands; ++band) {
        curve[band] = static_cast<int8_t>(std::clamp(static_cast<int>(bytes[band]) - baseline, -range, +range));
    }
    return curve;
}

bool IsValidValue(const SettingSpec& spec, int value) {
    switch (spec.kind) {
    case SettingKind::Slider:
        return value >= spec.min && value <= spec.max;
    case SettingKind::Toggle:
        return value == spec.min || value == spec.max;
    case SettingKind::Choice:
        return std::any_of(spec.options.begin(), spec.options.end(),
                           [&](const SettingOption& option) { return option.value == value; });
    }
    return false;
}

// Battery in the dongles' b0 status reply (byte layouts from HeadsetControl):
// - Nova 7 family: level at 2; byte 3 is 0 while the headset is off, 1 or 2 while it charges.
// - Arctis 7+: level at 2; byte 1 is 1 while the headset is off, byte 3 is 1 while it charges.
// - Nova 5X: level at 3; byte 1 is 2 while the headset is off, byte 4 is 1 while it charges.
// The level runs to statusBatteryMax: 4 on five-step models, 100 on the others.
std::optional<HeadsetUpdate> ParseStatusBattery(const DeviceModel& model, const uint8_t* data, size_t size) {
    if (size < 5 || data[0] != kStatusRequest) {
        return std::nullopt;
    }
    HeadsetUpdate update{.kind = HeadsetUpdate::Kind::Battery};
    int level = 0;
    switch (model.family) {
    case DeviceFamily::Nova7:
    case DeviceFamily::Nova7Gen2:
        level = data[2];
        update.headsetOff = data[3] == 0x00;
        update.charging = data[3] == 0x01 || data[3] == 0x02;
        break;
    case DeviceFamily::Arctis7Plus:
        level = data[2];
        update.headsetOff = data[1] == 0x01;
        update.charging = data[3] == 0x01;
        break;
    case DeviceFamily::Nova5X:
        level = data[3];
        update.headsetOff = data[1] == 0x02;
        update.charging = data[4] == 0x01;
        break;
    default:
        return std::nullopt;
    }
    update.value = std::min(100, level * 100 / std::max<int>(model.statusBatteryMax, 1));
    return update;
}

// Nova Elite, as HeadsetControl issue #481 reads a real base station: the 01 b0 reply holds the Bluetooth mode and
// link at 4 and 5 (as on the Nova Pro Wireless), the headset battery in percent at 6, the 2.4 GHz link at 14
// (8: headset connected) and the charging state at 15 (2: charging, 8: on battery); 07 b7 [headset %] [spare %]
// [charging state] reports battery changes, where state 1 means no headset.
constexpr uint8_t kEliteCommandReportId = 0x01;
constexpr uint8_t kEliteEventReportId = 0x07;
constexpr uint8_t kEliteBatteryEvent = 0xB7;
constexpr uint8_t kEliteLinkConnected = 0x08;
constexpr uint8_t kEliteCharging = 0x02;
constexpr uint8_t kEliteNoHeadset = 0x01;

// The 01 b0 reply also holds settings: Bluetooth at power-on 2, during calls 3, transparency 8, noise control 10,
// mute LED 11, auto-off 12 and the noise cancelling level 16. The 01 80 reply holds the screen: dimming 2,
// brightness 3, home screen 5. Events are 07 [command] [value], the sidetones 07 [command] [on] [level] and
// the stream mix 07 47 [main] 00 [aux] [mic].
std::vector<HeadsetUpdate> ParseEliteReport(const uint8_t* data, size_t size) {
    using Kind = HeadsetUpdate::Kind;
    auto setting = [](uint8_t command, int value) {
        return HeadsetUpdate{.kind = Kind::Setting, .command = command, .value = value};
    };
    if (size >= 17 && data[0] == kEliteCommandReportId && data[1] == kStatusRequest) {
        return {{.kind = Kind::Battery,
                 .value = std::min<int>(data[6], 100),
                 .charging = data[15] == kEliteCharging,
                 .headsetOff = data[14] != kEliteLinkConnected,
                 .spare = std::min<int>(data[7], 100)},
                {.kind = Kind::Bluetooth, .value = data[4], .chat = data[5]},
                setting(0xB2, data[2]),
                setting(0xB3, data[3]),
                setting(0xB9, data[8]),
                setting(kNoiseMode, data[10]),
                setting(0xBF, data[11]),
                setting(0xC1, data[12]),
                setting(0xB8, data[16])};
    }
    if (size >= 6 && data[0] == kEliteCommandReportId && data[1] == kEliteDisplayQuery) {
        return {setting(0x83, data[2]), setting(0x85, data[3]), setting(0x89, data[5])};
    }
    if (size < 3 || data[0] != kEliteEventReportId) {
        return {};
    }
    const uint8_t command = data[1];
    if (command == kEliteBatteryEvent && size >= 5) {
        return {{.kind = Kind::Battery,
                 .value = std::min<int>(data[2], 100),
                 .charging = data[4] == kEliteCharging,
                 .headsetOff = data[4] == kEliteNoHeadset,
                 .spare = std::min<int>(data[3], 100)}};
    }
    if ((command == kEliteBoomSidetone || command == kEliteOnEarSidetone) && size >= 4) {
        return {setting(command, data[2] ? data[3] : 0)};
    }
    if (command == kStreamMain && size >= 6) {
        return {setting(kStreamMain, data[2]), setting(kStreamAux, data[4]), setting(kStreamMic, data[5])};
    }
    if (kNovaElite.FindSetting(command)) {
        HeadsetUpdate update = setting(command, data[2]);
        update.fromEvent = true;
        return {update};
    }
    return {};
}

// The Nova Elite's report up to its bands: the command, then the built-in preset's index, short name and name, or
// those of the custom setting.
std::vector<uint8_t> EliteReportHead(const EqualizerSpec& spec, int preset) {
    const bool builtin = preset >= 0 && preset < spec.CustomIndex();
    std::vector<uint8_t> report{spec.bandsCommand, static_cast<uint8_t>(builtin ? spec.SlotOf(preset) : spec.customSlot)};
    // Fixed-size text fields, padded with zeros; the preset names are ASCII.
    auto text = [&report](auto value, size_t length) {
        size_t index = 0;
        for (; index < length && value[index]; ++index) {
            report.push_back(static_cast<uint8_t>(value[index] & 0x7F));
        }
        report.insert(report.end(), length - index, 0x00);
    };
    if (builtin && spec.presets[preset].alias) {
        text(spec.presets[preset].alias, kEliteEqAliasLength);
        text(spec.presets[preset].name, kEliteEqNameLength);
    } else {
        text(kEliteEqAlias, kEliteEqAliasLength);
        text(kGen2PresetName, kEliteEqNameLength);
    }
    return report;
}

}  // namespace

const SettingSpec* HeadsetFeatures::FindSetting(uint8_t command) const {
    for (const auto& section : sections) {
        for (const auto& setting : section.settings) {
            if (setting.command == command) {
                return &setting;
            }
        }
    }
    return nullptr;
}

// Arctis 7 of 2017 and 2019, as HeadsetControl documents it: auto-off 06 51 [minutes, 0-90], the battery in percent
// in the answer to 06 18, and 06 09 to keep a setting.
constexpr SettingSpec kArctis7Power[] = {
    {0x51, AutoOff, SettingKind::Choice, 0, 90, kMinuteTimeouts},
};
constexpr SettingSection kArctis7Sections[] = {
    {Power, kArctis7Power},
};
constexpr uint8_t kArctis7Battery = 0x18;
const HeadsetFeatures kArctis7{
    .sections = kArctis7Sections,
    .save = kSaveState,
    .batteryQuery = kArctis7Battery,
    .experimental = true,
};

const HeadsetFeatures& FeaturesFor(DeviceFamily family) {
    switch (family) {
    case DeviceFamily::NovaProWired: return kNovaProWired;
    case DeviceFamily::NovaProWireless: return kNovaProWireless;
    case DeviceFamily::Nova7: return kNova7;
    case DeviceFamily::Nova7Gen2: return kNova7Gen2;
    case DeviceFamily::Arctis7Plus: return kArctis7Plus;
    case DeviceFamily::NovaElite: return kNovaElite;
    case DeviceFamily::Arctis7: return kArctis7;
    case DeviceFamily::Nova5X: break;
    }
    return kNova5X;
}

std::vector<HeadsetUpdate> ParseHeadsetReport(const DeviceModel& model, const uint8_t* data, size_t size) {
    std::vector<HeadsetUpdate> updates;
    if (model.protocol == DeviceProtocol::Elite) {
        return ParseEliteReport(data, size);
    }
    if (model.protocol == DeviceProtocol::NovaStatus) {
        if (const auto battery = ParseStatusBattery(model, data, size)) {
            updates.push_back(*battery);
        }
        return updates;
    }
    if (model.protocol == DeviceProtocol::ArctisLegacy) {
        // 06 18 [battery 0-100]; the answer tells nothing about charging.
        if (size >= 3 && data[0] == 0x06 && data[1] == kArctis7Battery) {
            updates.push_back({.kind = HeadsetUpdate::Kind::Battery, .value = std::min<int>(data[2], 100)});
        }
        return updates;
    }
    if (model.protocol != DeviceProtocol::NovaPro || size < 3) {
        return updates;
    }
    const HeadsetFeatures& features = FeaturesFor(model.family);
    const EqualizerSpec& eq = features.equalizer;
    using Kind = HeadsetUpdate::Kind;

    if (data[0] == kNovaProEventReportId) {
        // Changes made on the GameDAC / base station menu: 07 <command> <value...>.
        const uint8_t command = data[1];
        if (command == kNovaProBatteryEvent && features.batteryQuery == kNovaProBattery && size >= 5) {
            // 07 b7 [headset 0-8] [spare 0-8] [state], sent whenever either battery changes.
            updates.push_back({.kind = Kind::Battery,
                               .value = std::min(100, data[2] * 100 / 8),
                               .charging = data[4] == kNovaProCharging,
                               .headsetOff = data[4] == kNovaProHeadsetOff,
                               .spare = std::min(100, data[3] * 100 / 8)});
        } else if (command == eq.presetCommand) {
            updates.push_back({.kind = Kind::EqPreset, .value = data[2]});
        } else if (command == eq.bandsCommand && size >= 2 + kEqBands) {
            updates.push_back({.kind = Kind::EqCurve, .curve = CurveFromBytes(data + 2, eq.baseline, eq.range)});
        } else if (features.FindSetting(command)) {
            updates.push_back({.kind = Kind::Setting, .command = command, .value = data[2], .fromEvent = true});
        }
        return updates;
    }

    if (data[0] != kNovaProCommandReportId) {
        return updates;
    }
    if (data[1] == kNovaProState && features.stateQuery == kNovaProState && size >= 19) {
        // 06 20 .. .. [gain] .. [preset] [10 bands] [mic volume] [sidetone], verified on a Nova Pro Wired.
        updates.push_back({.kind = Kind::EqPreset, .value = data[6]});
        updates.push_back({.kind = Kind::EqCurve, .curve = CurveFromBytes(data + 7, eq.baseline, eq.range)});
        updates.push_back({.kind = Kind::Setting, .command = kMicVolumeCommand, .value = data[17]});
        updates.push_back({.kind = Kind::Setting, .command = kSidetoneCommand, .value = data[18]});
        updates.push_back({.kind = Kind::Setting, .command = kGainCommand, .value = data[4]});
        if (size >= 22) {
            updates.push_back({.kind = Kind::ChatMix, .value = data[20], .chat = data[21]});
        }
        // ... [line out mode] [game] [chat] [stream main] 00 [stream aux] [stream mic], as Linux-Arctis-Manager
        // documents; matches a Nova Pro Wired set to Speakers with the stream mix at 100%.
        if (size >= 26) {
            updates.push_back({.kind = Kind::Setting, .command = kLineOutCommand, .value = data[19]});
            updates.push_back({.kind = Kind::Setting, .command = kStreamMain, .value = data[22]});
            updates.push_back({.kind = Kind::Setting, .command = kStreamAux, .value = data[24]});
            updates.push_back({.kind = Kind::Setting, .command = kStreamMic, .value = data[25]});
        }
    } else if (data[1] == kNovaProBattery && features.batteryQuery == kNovaProBattery && size >= 16) {
        // 06 b0: Bluetooth at power-on 2, during calls 3, its mode and link 4-5, battery 0-8 at 6, the spare
        // battery 0-8 at 7, transparency level 8, noise control 10, mute LED 11, auto-off 12, wireless mode 13,
        // headset state 15 (1 off, 2 charging, 8 on).
        updates.push_back({.kind = Kind::Battery,
                           .value = std::min(100, data[6] * 100 / 8),
                           .charging = data[15] == kNovaProCharging,
                           .headsetOff = data[15] == kNovaProHeadsetOff,
                           .spare = std::min(100, data[7] * 100 / 8)});
        updates.push_back({.kind = Kind::Bluetooth, .value = data[4], .chat = data[5]});
        updates.push_back({.kind = Kind::Setting, .command = 0xB2, .value = data[2]});
        updates.push_back({.kind = Kind::Setting, .command = 0xB3, .value = data[3]});
        updates.push_back({.kind = Kind::Setting, .command = kTransparencyLevel, .value = data[8]});
        updates.push_back({.kind = Kind::Setting, .command = kNoiseMode, .value = data[10]});
        updates.push_back({.kind = Kind::Setting, .command = 0xBF, .value = data[11]});
        updates.push_back({.kind = Kind::Setting, .command = 0xC1, .value = data[12]});
        updates.push_back({.kind = Kind::Setting, .command = 0xC3, .value = data[13]});
    }
    return updates;
}

void ApplyUpdate(const HeadsetFeatures& features, HeadsetState& state, const HeadsetUpdate& update) {
    using Kind = HeadsetUpdate::Kind;
    switch (update.kind) {
    case Kind::Setting:
        // Out-of-range values mean the byte layout differs on this model; keep the last known value.
        if (const SettingSpec* spec = features.FindSetting(update.command); spec && IsValidValue(*spec, update.value)) {
            state.values[update.command] = static_cast<int16_t>(update.value);
        }
        break;
    case Kind::EqPreset:
        if (update.value >= 0 && update.value <= features.equalizer.CustomIndex()) {
            state.eqPreset = update.value;
        }
        break;
    case Kind::EqCurve:
        state.eqCurve = update.curve;
        state.eqCurveKnown = true;
        break;
    case Kind::ChatMix:
        // Handled by the balance logic, not part of the settings.
        break;
    case Kind::Battery:
        state.battery = update.value;
        state.spareBattery = update.spare;
        state.charging = update.charging;
        state.headsetOff = update.headsetOff;
        break;
    case Kind::Bluetooth:
        // Mode 1 off, 2 pairing, otherwise on (4 with a link); link 1 when a device is connected, as
        // Linux-Arctis-Manager reads the reply.
        state.bluetooth = update.value == 1   ? BluetoothState::Off
                          : update.value == 2 ? BluetoothState::Pairing
                          : update.chat == 1  ? BluetoothState::Connected
                                              : BluetoothState::On;
        break;
    }
}

std::vector<uint8_t> SettingCommand(const HeadsetFeatures& features, const HeadsetState& state, uint8_t command,
                                    int value) {
    if (const SettingSpec* spec = features.FindSetting(command); spec && spec->wire == SettingWire::SwitchedLevel) {
        // Off keeps the last level, so switching it on again restores it.
        const int last = state.values[command] > 0 ? state.values[command] : 1;
        return {command, static_cast<uint8_t>(value > 0 ? 1 : 0), static_cast<uint8_t>(value > 0 ? value : last)};
    }
    if (command != kStreamMain && command != kStreamAux && command != kStreamMic) {
        return {command, static_cast<uint8_t>(value)};
    }
    // The other two sliders keep their level; an unknown one counts as full.
    auto level = [&](uint8_t id) {
        const int known = id == command ? value : state.values[id];
        return static_cast<uint8_t>(known < 0 ? 100 : std::clamp(known, 0, 100));
    };
    return {kStreamMain, level(kStreamMain), 0x00, level(kStreamAux), level(kStreamMic)};
}

std::vector<std::vector<uint8_t>> EqualizerCommands(const EqualizerSpec& spec, const EqCurve& curve, int preset) {
    if (spec.format == EqFormat::Graphic) {
        std::vector<uint8_t> command{spec.bandsCommand};
        for (const int8_t band : curve) {
            command.push_back(static_cast<uint8_t>(spec.baseline + std::clamp<int>(band, -spec.range, spec.range)));
        }
        if (spec.terminator) {
            command.push_back(0x00);
        }
        return {command};
    }
    if (spec.format == EqFormat::FeatureGains) {
        std::vector<uint8_t> report = EliteReportHead(spec, preset);
        for (const int8_t band : curve) {
            report.push_back(static_cast<uint8_t>(static_cast<int8_t>(std::clamp<int>(band, -spec.range, spec.range) * 5)));
        }
        return {report};
    }
    const bool builtin = preset >= 0 && preset < spec.CustomIndex();
    return EqualizerCommands(spec, builtin ? BuiltinPresetFilters(spec, preset) : CurveFilters(curve, spec.range), preset);
}

std::vector<std::vector<uint8_t>> EqualizerCommands(const EqualizerSpec& spec, const EqFilters& filters, int preset) {
    if (!spec.Parametric()) {
        // Not used by the app: these equalizers take curves. The closest curve stands in.
        ParametricEq eq;
        eq.bands = filters;
        const auto gains = GraphicEqGains(eq, spec.range / 2.0f);
        EqCurve curve{};
        for (int band = 0; band < kEqBands; ++band) {
            curve[band] = static_cast<int8_t>(std::lround(gains[band] * 2.0f));
        }
        return EqualizerCommands(spec, curve);
    }
    if (spec.format == EqFormat::Feature) {
        std::vector<uint8_t> report = EliteReportHead(spec, preset);
        for (const EqBand& band : filters) {
            AppendFilter(report, band, false, spec.notch);
        }
        return {report};
    }
    std::vector<uint8_t> name{kGen2EqName, kGen2Radio, kGen2CustomPreset};
    name.insert(name.end(), std::begin(kGen2PresetName), std::end(kGen2PresetName) - 1);
    std::vector<uint8_t> bands{spec.bandsCommand, kGen2Radio};
    for (const EqBand& band : filters) {
        AppendFilter(bands, band, true, spec.notch);
    }
    return {name, bands, {kGen2EqCommit}};
}

EqFilters CurveFilters(const EqCurve& curve, int8_t range, bool shelves) {
    EqFilters filters{};
    for (int band = 0; band < kEqBands; ++band) {
        filters[band] = {.type = ::FilterType::Peaking,
                         .frequency = kOctaveFrequencies[band],
                         .gain = std::clamp<int>(curve[band], -range, range) / 2.0f,
                         .q = kOctaveQ};
    }
    if (shelves) {
        filters.front().type = ::FilterType::LowShelf;
        filters.back().type = ::FilterType::HighShelf;
    }
    return filters;
}

EqFilters BuiltinPresetFilters(const EqualizerSpec& spec, int index) {
    const EqCurve& curve = spec.presets[index].curve;
    const bool flat = std::all_of(curve.begin(), curve.end(), [](int8_t band) { return band == 0; });
    return CurveFilters(curve, spec.range, !flat);
}

EqFilters PresetFilters(const ParametricEq& eq, bool notch) {
    EqFilters filters = eq.bands;
    for (EqBand& band : filters) {
        band.frequency = std::clamp(band.frequency, kEqMinFrequency, kEqMaxFrequency);
        band.gain = std::clamp(band.gain, -kHeadsetFilterMaxGain, kHeadsetFilterMaxGain);
        band.q = std::clamp(band.q, kEqMinQ, kEqMaxQ);
        if (band.type == ::FilterType::Notch && !notch) {
            band.type = ::FilterType::Peaking;
            band.gain = -kHeadsetFilterMaxGain;
        }
    }
    return filters;
}

EqFilters ActiveFilters(const EqualizerSpec& spec, const HeadsetState& state) {
    if (state.eqPreset >= 0 && state.eqPreset < spec.CustomIndex()) {
        return BuiltinPresetFilters(spec, state.eqPreset);
    }
    if (state.eqFiltersKnown) {
        return state.eqFilters;
    }
    return CurveFilters(state.eqCurveKnown ? state.eqCurve : EqCurve{}, spec.range);
}

std::vector<uint8_t> SettingValues(const SettingSpec& spec) {
    std::vector<uint8_t> values;
    switch (spec.kind) {
    case SettingKind::Slider:
        for (int value = spec.min; value <= spec.max; ++value) {
            values.push_back(static_cast<uint8_t>(value));
        }
        break;
    case SettingKind::Toggle:
        values = {spec.min, spec.max};
        break;
    case SettingKind::Choice:
        for (const auto& option : spec.options) {
            values.push_back(option.value);
        }
        break;
    }
    return values;
}
