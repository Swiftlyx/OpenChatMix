#include "headset.h"

#include <algorithm>

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
constexpr SettingSection kNovaProWiredSections[] = {
    {Microphone, kNovaProMicrophone},
    {Sound, kNovaProSound},
    {Display, kNovaProDisplay},
};

// Nova Pro Wireless base station.
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
constexpr SettingSection kNovaProWirelessSections[] = {
    {Microphone, kNovaProWirelessMicrophone},
    {Sound, kNovaProSound},
    {Display, kNovaProDisplay},
    {Power, kNovaProWirelessPower},
    {Wireless, kNovaProWirelessRadio},
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

// SteelSeries Engine EQ presets. The Nova Pro stores them on the device (indices 0-3).
constexpr EqPreset kNova7Presets[] = {
    {L"Flat", {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
    {L"Bass Boost", {7, 11, 8, 2, -3, -3, -2, -2, -2, -2}},
    {L"Focus", {-10, -7, -2, -7, -5, 8, 12, -7, 0, 0}},
    {L"Smiley", {6, 7, 3, -3, -8, -8, -5, 3, 6, 8}},
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
};
const HeadsetFeatures kNovaProWireless{
    .sections = kNovaProWirelessSections,
    .equalizer = {.bandsCommand = 0x33, .baseline = 0x14, .range = 20, .presetCommand = 0x2E, .presets = kNova7Presets},
    .save = kSaveState,
    .batteryQuery = 0xB0,
    .oled = true,
};
const HeadsetFeatures kNova7{
    .sections = kNova7Sections,
    .equalizer = {.bandsCommand = 0x33, .baseline = 0x14, .range = 20, .terminator = true, .presets = kNova7Presets},
};
const HeadsetFeatures kNova7Gen2{
    .sections = kNova7Sections,
    .equalizer = {.bandsCommand = 0x33, .baseline = 0x14, .range = 20, .terminator = true, .presets = kNova7Presets},
    .save = kSaveState,
};
const HeadsetFeatures kArctis7Plus{
    .sections = kArctis7PlusSections,
    .equalizer = {.bandsCommand = 0x33, .baseline = 0x18, .range = 24, .terminator = true, .presets = kArctis7PlusPresets},
    .save = kSaveState,
};
const HeadsetFeatures kNova5X{
    .sections = kNova5XSections,
    .save = kNova5XSave,
};

constexpr uint8_t kNovaProEventReportId = 0x07;
constexpr uint8_t kNovaProCommandReportId = 0x06;
constexpr uint8_t kNovaProState = 0x20;
constexpr uint8_t kNovaProBattery = 0xB0;
constexpr uint8_t kGainCommand = 0x27;
constexpr uint8_t kMicVolumeCommand = 0x37;
constexpr uint8_t kSidetoneCommand = 0x39;

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

const HeadsetFeatures& FeaturesFor(DeviceFamily family) {
    switch (family) {
    case DeviceFamily::NovaProWired: return kNovaProWired;
    case DeviceFamily::NovaProWireless: return kNovaProWireless;
    case DeviceFamily::Nova7: return kNova7;
    case DeviceFamily::Nova7Gen2: return kNova7Gen2;
    case DeviceFamily::Arctis7Plus: return kArctis7Plus;
    case DeviceFamily::Nova5X: break;
    }
    return kNova5X;
}

std::vector<HeadsetUpdate> ParseHeadsetReport(const DeviceModel& model, const uint8_t* data, size_t size) {
    std::vector<HeadsetUpdate> updates;
    if (model.protocol != DeviceProtocol::NovaPro || size < 3) {
        return updates;
    }
    const HeadsetFeatures& features = FeaturesFor(model.family);
    const EqualizerSpec& eq = features.equalizer;
    using Kind = HeadsetUpdate::Kind;

    if (data[0] == kNovaProEventReportId) {
        // Changes made on the GameDAC / base station menu: 07 <command> <value...>.
        const uint8_t command = data[1];
        if (command == eq.presetCommand) {
            updates.push_back({.kind = Kind::EqPreset, .value = data[2]});
        } else if (command == eq.bandsCommand && size >= 2 + kEqBands) {
            updates.push_back({.kind = Kind::EqCurve, .curve = CurveFromBytes(data + 2, eq.baseline, eq.range)});
        } else if (features.FindSetting(command)) {
            updates.push_back({.kind = Kind::Setting, .command = command, .value = data[2]});
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
    } else if (data[1] == kNovaProBattery && features.batteryQuery == kNovaProBattery && size >= 16) {
        // 06 b0: battery 0-8 at byte 6, headset state at byte 15 (1 off, 2 charging, 8 on).
        updates.push_back({.kind = Kind::Battery,
                           .value = std::min(100, data[6] * 100 / 8),
                           .charging = data[15] == 0x02,
                           .headsetOff = data[15] == 0x01});
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
        state.charging = update.charging;
        state.headsetOff = update.headsetOff;
        break;
    }
}

std::vector<uint8_t> EqualizerBandsCommand(const EqualizerSpec& spec, const EqCurve& curve) {
    std::vector<uint8_t> command{spec.bandsCommand};
    for (const int8_t band : curve) {
        command.push_back(static_cast<uint8_t>(spec.baseline + std::clamp<int>(band, -spec.range, spec.range)));
    }
    if (spec.terminator) {
        command.push_back(0x00);
    }
    return command;
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
