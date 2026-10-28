#include "headset.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>
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
// Nova 7P: without the sidetone, which its dial sets and the command does not (HeadsetControl #450).
constexpr SettingSpec kNova7PMicrophone[] = {
    kNova7Microphone[0],
    kNova7Microphone[2],
};
constexpr SettingSection kNova7PSections[] = {
    {Microphone, kNova7PMicrophone},
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

// Nova 3P and 3X Wireless, as HeadsetControl sends them: microphone 37 [0-15], sidetone 39 [0-10], volume limiter
// 27, auto-off a3 [minutes]; the status as on the Nova 5X.
constexpr SettingSpec kNova3WirelessMicrophone[] = {
    {0x37, MicVolume, SettingKind::Slider, 0, 15},
    {0x39, Sidetone, SettingKind::Slider, 0, 10},
};
constexpr SettingSection kNova3WirelessSections[] = {
    {Microphone, kNova3WirelessMicrophone},
    {Sound, kNova5XSound},
    {Power, kMinutePower},
};

// Arctis GameBuds and GameBuds X, as Linux-Arctis-Manager describes them: microphone 37 [1-10], sidetone 39 [0, 3,
// 6 or 10 for off to high], noise control bd with the ANC level b8 [1-3] and the transparency level b9 [3, 6 or 10],
// volume limiter 27, in-ear detection c5. The b0 status holds each bud's link at 3 and 4 (03 out of the case) and
// battery at 5 and 6, as HeadsetControl reads them. The equalizer: 33 and ten filters, the frequency in SteelSeries'
// usual little-endian (Linux-Arctis-Manager writes it big-endian, against the description's default and the other
// sources), peaks only until a GameBuds owner confirms the rest.
constexpr SettingOption kGameBudsSidetone[] = {{0, OptOff}, {3, OptLow}, {6, OptMedium}, {10, OptHigh}};
constexpr SettingSpec kGameBudsMicrophone[] = {
    {0x37, MicVolume, SettingKind::Slider, 1, 10},
    {0x39, Sidetone, SettingKind::Choice, 0, 10, kGameBudsSidetone},
};
constexpr SettingOption kGameBudsLevels[] = {{1, OptLow}, {2, OptMedium}, {3, OptHigh}};
constexpr SettingOption kGameBudsTransparency[] = {{3, OptLow}, {6, OptMedium}, {10, OptHigh}};
constexpr SettingSpec kGameBudsNoise[] = {
    {kNoiseMode, NoiseMode, SettingKind::Choice, 0, 2, kNoiseModes},
    {0xB8, NoiseCancellingLevel, SettingKind::Choice, 1, 3, kGameBudsLevels, kNoiseMode, kNoiseCancelling},
    {kTransparencyLevel, TransparencyLevel, SettingKind::Choice, 3, 10, kGameBudsTransparency, kNoiseMode,
     kNoiseTransparent},
};
constexpr SettingSpec kGameBudsSound[] = {
    {0x27, VolumeLimiter, SettingKind::Toggle, 0, 1},
    {0xC5, InEarDetection, SettingKind::Toggle, 0, 1},
};
constexpr SettingSection kGameBudsSections[] = {
    {Microphone, kGameBudsMicrophone},
    {NoiseControl, kGameBudsNoise},
    {Sound, kGameBudsSound},
};
constexpr uint8_t kBudOutOfCase = 0x03;

// Nova 4 and 4X, as Linux-Arctis-Manager describes them: the Nova 7's commands, the mute light as ae [0-3] 01 14 and
// the volume limiter as 3a; the status as on the Nova 7, with the battery in five steps.
constexpr SettingSpec kNova4Microphone[] = {
    {0x37, MicVolume, SettingKind::Slider, 0, 7},
    {0x39, Sidetone, SettingKind::Choice, 0, 3, kFourLevels},
    {0xAE, MicLed, SettingKind::Choice, 0, 3, kFourLevels, 0, 0, SettingWire::Trailing, 0x01, 0x14},
};
constexpr SettingSection kNova4Sections[] = {
    {Microphone, kNova4Microphone},
    {Sound, kNova7Sound},
    {Power, kMinutePower},
};

// Nova 3, wired, as HeadsetControl and Linux-Arctis-Manager describe it: feature reports 06 [command]; microphone 37
// [0-10], sidetone 39 [0-3], mute light ae [0-3], six equalizer bands as 20 plus half decibels, ±6 dB.
constexpr SettingSpec kNova3Microphone[] = {
    {0x37, MicVolume, SettingKind::Slider, 0, 10},
    {0x39, Sidetone, SettingKind::Choice, 0, 3, kFourLevels},
    {0xAE, MicLed, SettingKind::Choice, 0, 3, kFourLevels},
};
constexpr SettingSection kNova3Sections[] = {
    {Microphone, kNova3Microphone},
};
// The frequencies of its bands are in neither description, so they are numbered.
constexpr const wchar_t* kNova3BandLabels[] = {L"1", L"2", L"3", L"4", L"5", L"6"};
// HeadsetControl's presets, in half decibels from 0x14.
constexpr EqPreset kNova3Presets[] = {
    {L"Flat", {0, 0, 0, 0, 0, 0}},
    {L"Bass Boost", {8, 5, -3, 0, 0, 0}},
    {L"Smiley", {6, 3, -5, -2, 3, 6}},
    {L"Focus", {-8, -7, -3, 4, 8, 0}},
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
// Dongles without a dial are not polled for it, so their status is asked for this often, for the output to follow a
// switch-off.
constexpr unsigned kNoDialStatusPollMs = 5000;

constexpr EqualizerSpec kNova7Equalizer{
    .bandsCommand = 0x33, .baseline = 0x14, .range = 20, .terminator = true, .presets = kNova7Presets};
constexpr EqualizerSpec kNova7Gen2Equalizer{.bandsCommand = 0x33,
                                            .range = 20,
                                            .presets = kNova7Presets,
                                            .format = EqFormat::Parametric,
                                            .experimental = true,
                                            .peaksOnly = true,
                                            .nameCommand = 0xA7,
                                            .applyCommand = 0x27,
                                            .linkByte = true,
                                            .bigEndianFrequency = true};
const HeadsetFeatures kNova7{
    .sections = kNova7Sections,
    .equalizer = kNova7Equalizer,
    .batteryQuery = kStatusRequest,
};
const HeadsetFeatures kNova7Gen2{
    .sections = kNova7Sections,
    .equalizer = kNova7Gen2Equalizer,
    .save = kSaveState,
    // The sound pops while these headsets save (issue #7), so a run of changes is saved once, after it ends.
    .saveDelayMs = 3000,
    .batteryQuery = kStatusRequest,
};
const HeadsetFeatures kNova7P{
    .sections = kNova7PSections,
    .equalizer = kNova7Equalizer,
    .batteryQuery = kStatusRequest,
    .batteryPollMs = kNoDialStatusPollMs,
    .experimental = true,
};
const HeadsetFeatures kNova7PGen2{
    .sections = kNova7PSections,
    .equalizer = kNova7Gen2Equalizer,
    .save = kSaveState,
    .saveDelayMs = 3000,
    .batteryQuery = kStatusRequest,
    .batteryPollMs = kNoDialStatusPollMs,
    .experimental = true,
};
const HeadsetFeatures kArctis7Plus{
    .sections = kArctis7PlusSections,
    .equalizer = {.bandsCommand = 0x33, .baseline = 0x18, .range = 24, .terminator = true, .presets = kArctis7PlusPresets},
    .save = kSaveState,
    .batteryQuery = kStatusRequest,
};
// The Nova 5's parametric equalizer as HeadsetControl sends it (experimental here).
const HeadsetFeatures kNova5X{
    .sections = kNova5XSections,
    .equalizer = {.bandsCommand = 0x33,
                  .range = 20,
                  .presets = kNova7Presets,
                  .format = EqFormat::Parametric,
                  .experimental = true,
                  .filterGain = FilterGain::HalfDecibels},
    .save = kNova5XSave,
    .batteryQuery = kStatusRequest,
};
const HeadsetFeatures kNova3Wireless{
    .sections = kNova3WirelessSections,
    .equalizer = {.bandsCommand = 0x33,
                  .range = 20,
                  .presets = kNova7Presets,
                  .format = EqFormat::Parametric,
                  .experimental = true,
                  .notch = true},
    .save = kSaveState,
    .batteryQuery = kStatusRequest,
    .batteryPollMs = kNoDialStatusPollMs,
    .experimental = true,
};
// The GameBuds' charging case, as Linux-Arctis-Manager describes it: b7 answered (and sent on its own) with the left
// and right bud's battery, the case's, then their charging states (1 charging).
constexpr uint8_t kCaseBatteryQuery = 0xB7;
const HeadsetFeatures kGameBudsCase{
    .batteryQuery = kCaseBatteryQuery,
    .experimental = true,
};
const HeadsetFeatures kGameBuds{
    .sections = kGameBudsSections,
    .equalizer = {.bandsCommand = 0x33,
                  .range = 20,
                  .presets = kNova7Presets,
                  .format = EqFormat::Parametric,
                  .experimental = true,
                  .peaksOnly = true},
    .save = kSaveState,
    .batteryQuery = kStatusRequest,
    .batteryPollMs = kNoDialStatusPollMs,
    .experimental = true,
    .chargingCase = true,
};
const HeadsetFeatures kNova4{
    .sections = kNova4Sections,
    .equalizer = {.bandsCommand = 0x33, .baseline = 0x18, .range = 24, .presets = kNova7Presets, .experimental = true},
    .save = kSaveState,
    .batteryQuery = kStatusRequest,
    .experimental = true,
};
const HeadsetFeatures kNova3{
    .sections = kNova3Sections,
    .equalizer = {.bandsCommand = 0x33,
                  .baseline = 0x14,
                  .range = 12,
                  .presets = kNova3Presets,
                  .experimental = true,
                  .bandLabels = kNova3BandLabels},
    .save = kSaveState,
    .experimental = true,
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
// The Nova 5 and Nova 3 Wireless, as HeadsetControl sends them: 33 and the ten filters alone, frequency
// little-endian (the spec's parametric fields).
constexpr uint8_t kGen2Radio = 0x00;        // The 2.4 GHz link; 1 is Bluetooth, 2 the microphone.
constexpr uint8_t kGen2CustomPreset = 0x01;  // 0 is a SteelSeries preset.
constexpr char kGen2PresetName[] = "OpenChatMix";
constexpr float kHalfDecibelsMaxGain = 10.0f;  // FilterGain::HalfDecibels: 0 is -10 dB, 20 flat, 40 +10 dB.
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

// The sound chip of the Arctis 7 and Arctis 1 Wireless dongles (Avnera AV6X02), as SteelSeries' own description
// gives it (through Linux-Arctis-Manager): six fixed bands whose biquad filters the computer works out at 48 kHz
// (Audio EQ Cookbook, Q per band) and writes as 28 [channel] [address] [value, float32 big-endian] [filter number],
// for the left channel 3 and the right channel 4 (filter numbers 42 higher): b0, b1, b2, a0, -a1 and -a2 with a0 one,
// at the band's address plus 08 plus their index, then the band's address with zeros. Not confirmed on a headset.
struct ChipBand {
    ::FilterType type;
    double frequency;
    double q;
    uint8_t filterNumber;  // Of the left channel's first coefficient.
};
constexpr ChipBand kChipBands[] = {
    {::FilterType::LowShelf, 64.0, 1.1, 1},     {::FilterType::Peaking, 180.0, 0.8, 8},
    {::FilterType::Peaking, 500.0, 0.8, 15},    {::FilterType::Peaking, 1400.0, 0.8, 22},
    {::FilterType::Peaking, 3900.0, 0.7, 29},   {::FilterType::HighShelf, 11000.0, 1.1, 36},
};
constexpr const wchar_t* kChipBandLabels[] = {L"64", L"180", L"500", L"1.4k", L"3.9k", L"11k"};
static_assert(std::size(kChipBands) == std::size(kChipBandLabels));
constexpr uint8_t kChipCommand = 0x28;
constexpr uint8_t kChipLeft = 3;
constexpr uint8_t kChipRight = 4;
constexpr uint8_t kChipRightFilters = 42;  // Added to the filter numbers of the right channel.
constexpr uint8_t kChipCoefficientAddress = 0x08;
constexpr double kChipSampleRate = 48000.0;
constexpr EqPreset kChipPresets[] = {{L"Flat", {}}};
// ±12 dB in half-decibel steps, as SteelSeries Engine offered.
constexpr EqualizerSpec kChipEqualizer{.bandsCommand = kChipCommand,
                                       .range = 24,
                                       .presets = kChipPresets,
                                       .format = EqFormat::Chip,
                                       .experimental = true,
                                       .bandLabels = kChipBandLabels};

// b0, b1, b2, a1, a2 of a band at a gain, divided by a0; no gain gives the filter that changes nothing.
std::array<double, 5> ChipCoefficients(const ChipBand& band, double gainDb) {
    if (gainDb == 0.0) {
        return {1.0, 0.0, 0.0, 0.0, 0.0};
    }
    const double w0 = 2.0 * std::numbers::pi * band.frequency / kChipSampleRate;
    const double cosW0 = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * band.q);
    const double a = std::pow(10.0, gainDb / 40.0);
    const double shelfAlpha = 2.0 * std::sqrt(a) * alpha;
    double b0 = 0.0, b1 = 0.0, b2 = 0.0, a0 = 0.0, a1 = 0.0, a2 = 0.0;
    switch (band.type) {
    case ::FilterType::LowShelf:
        b0 = a * ((a + 1.0) - (a - 1.0) * cosW0 + shelfAlpha);
        b1 = 2.0 * a * ((a - 1.0) - (a + 1.0) * cosW0);
        b2 = a * ((a + 1.0) - (a - 1.0) * cosW0 - shelfAlpha);
        a0 = (a + 1.0) + (a - 1.0) * cosW0 + shelfAlpha;
        a1 = -2.0 * ((a - 1.0) + (a + 1.0) * cosW0);
        a2 = (a + 1.0) + (a - 1.0) * cosW0 - shelfAlpha;
        break;
    case ::FilterType::HighShelf:
        b0 = a * ((a + 1.0) + (a - 1.0) * cosW0 + shelfAlpha);
        b1 = -2.0 * a * ((a - 1.0) + (a + 1.0) * cosW0);
        b2 = a * ((a + 1.0) + (a - 1.0) * cosW0 - shelfAlpha);
        a0 = (a + 1.0) - (a - 1.0) * cosW0 + shelfAlpha;
        a1 = 2.0 * ((a - 1.0) - (a + 1.0) * cosW0);
        a2 = (a + 1.0) - (a - 1.0) * cosW0 - shelfAlpha;
        break;
    default:
        b0 = 1.0 + alpha * a;
        b1 = -2.0 * cosW0;
        b2 = 1.0 - alpha * a;
        a0 = 1.0 + alpha / a;
        a1 = -2.0 * cosW0;
        a2 = 1.0 - alpha / a;
        break;
    }
    return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

// The Arctis 5's sound chip (Conexant CX20892), from the same description: five fixed bands written as 40 0b 10
// [address] b0 b1 b2 a1 a2 03, each coefficient 16-bit big-endian in Q15 with a0 = 0.5 (b halved, a1 and a2 halved
// and negated), rounded half up; for a1 and a2 the first pair of five candidates whose poles lie inside the unit
// circle, as SteelSeries' own code picks them. 40 01 11 54 9b then commits what was written.
struct CxBand {
    ::FilterType type;
    double frequency;
    double q;
    uint8_t address;
};
constexpr CxBand kCxBands[] = {
    {::FilterType::LowShelf, 62.5, 1.1, 0x20},   {::FilterType::Peaking, 250.0, 0.8, 0x2B},
    {::FilterType::Peaking, 1000.0, 0.8, 0x36},  {::FilterType::Peaking, 3600.0, 0.7, 0x41},
    {::FilterType::HighShelf, 12000.0, 1.1, 0x4C},
};
constexpr const wchar_t* kCxBandLabels[] = {L"62", L"250", L"1k", L"3.6k", L"12k"};
static_assert(std::size(kCxBands) == std::size(kCxBandLabels));
constexpr uint8_t kCxWrite = 0x40;
constexpr uint8_t kCxBandLength = 0x0B;
constexpr uint8_t kCxAddressHigh = 0x10;
constexpr uint8_t kCxBandEnd = 0x03;
constexpr uint8_t kCxCommit[] = {kCxWrite, 0x01, 0x11, 0x54, 0x9B};
constexpr EqualizerSpec kCxEqualizer{.bandsCommand = kCxWrite,
                                     .range = 24,
                                     .presets = kChipPresets,
                                     .format = EqFormat::Chip,
                                     .experimental = true,
                                     .bandLabels = kCxBandLabels,
                                     .chip = SoundChip::Cx20892};

int16_t CxRounded(double value) {
    return static_cast<int16_t>(std::clamp(std::floor(value * 32768.0 + 0.5), -32767.0, 32767.0));
}

int16_t CxTruncated(double value) {
    return static_cast<int16_t>(std::clamp(std::floor(value * 32768.0), -32767.0, 32767.0));
}

// Both roots of 0.5 x² - a1 x - a2 (in Q15) inside the unit circle.
bool CxStable(int16_t a1, int16_t a2) {
    constexpr double a = 0x4000;
    const double b = -static_cast<double>(a1);
    const double c = -static_cast<double>(a2);
    const double d = b * b - 4.0 * a * c;
    if (d >= 0.0) {
        const double r1 = (std::sqrt(d) - b) / (2.0 * a);
        const double r2 = (-std::sqrt(d) - b) / (2.0 * a);
        return r1 * r1 < 1.0 && r2 * r2 < 1.0;
    }
    const double real = -b / (2.0 * a);
    const double imaginary = std::sqrt(-d) / (2.0 * a);
    return real * real + imaginary * imaginary < 1.0;
}

std::vector<uint8_t> CxBandWrite(const CxBand& band, double gainDb) {
    const ChipBand shape{band.type, band.frequency, band.q, 0};
    const auto [b0, b1, b2, a1, a2] = ChipCoefficients(shape, gainDb);
    const double a1Half = a1 / -2.0;
    const double a2Half = a2 / -2.0;
    // Wrapping, as on the 16-bit wire.
    auto next = [](int16_t value) { return static_cast<int16_t>(static_cast<uint16_t>(value) + 1u); };
    const int16_t t1 = CxTruncated(a1Half);
    const int16_t t2 = CxTruncated(a2Half);
    const std::pair<int16_t, int16_t> candidates[] = {
        {CxRounded(a1Half), CxRounded(a2Half)}, {next(t1), t2}, {t1, next(t2)}, {t1, t2}, {next(t1), next(t2)},
    };
    std::pair<int16_t, int16_t> chosen = candidates[std::size(candidates) - 1];
    for (const auto& candidate : candidates) {
        if (CxStable(candidate.first, candidate.second)) {
            chosen = candidate;
            break;
        }
    }
    std::vector<uint8_t> write{kCxWrite, kCxBandLength, kCxAddressHigh, band.address};
    for (const int16_t value : {CxRounded(b0 / 2.0), CxRounded(b1 / 2.0), CxRounded(b2 / 2.0), chosen.first,
                                chosen.second}) {
        write.push_back(static_cast<uint8_t>(static_cast<uint16_t>(value) >> 8));
        write.push_back(static_cast<uint8_t>(value & 0xFF));
    }
    write.push_back(kCxBandEnd);
    return write;
}

std::vector<uint8_t> ChipWrite(uint8_t channel, uint8_t address, float value, uint8_t filterNumber) {
    const uint32_t bits = std::bit_cast<uint32_t>(value);
    return {kChipCommand,
            channel,
            address,
            static_cast<uint8_t>(bits >> 24),
            static_cast<uint8_t>(bits >> 16),
            static_cast<uint8_t>(bits >> 8),
            static_cast<uint8_t>(bits),
            filterNumber};
}

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

// One filter in six bytes: frequency (u16, in the spec's byte order), type, gain (in the spec's form) and Q (x1000,
// u16 little-endian). A headset with a notch type of its own gets it at the deepest cut, as GG sends it; without one,
// like PresetFilters, the deepest peak cut stands in for it.
void AppendFilter(std::vector<uint8_t>& out, EqBand band, const EqualizerSpec& spec) {
    const bool notch = band.type == ::FilterType::Notch;
    if (notch && !spec.notch) {
        band.type = ::FilterType::Peaking;
    }
    const float maxGain = spec.filterGain == FilterGain::HalfDecibels ? kHalfDecibelsMaxGain : kHeadsetFilterMaxGain;
    const float gain = band.enabled ? std::clamp(notch ? -maxGain : UsesGain(band.type) ? band.gain : 0.0f,
                                                 -maxGain, maxGain)
                                    : 0.0f;
    const uint16_t frequency =
        band.enabled ? static_cast<uint16_t>(std::lround(std::clamp(band.frequency, kEqMinFrequency, kEqMaxFrequency)))
                     : kWireBandOff;
    const uint16_t q = static_cast<uint16_t>(std::lround(std::clamp(band.q, kEqMinQ, kEqMaxQ) * 1000.0f));
    const uint8_t frequencyHigh = static_cast<uint8_t>(frequency >> 8);
    const uint8_t frequencyLow = static_cast<uint8_t>(frequency & 0xFF);
    out.push_back(spec.bigEndianFrequency ? frequencyHigh : frequencyLow);
    out.push_back(spec.bigEndianFrequency ? frequencyLow : frequencyHigh);
    out.push_back(band.enabled ? WireType(band.type) : kWirePeak);
    out.push_back(spec.filterGain == FilterGain::HalfDecibels
                      ? static_cast<uint8_t>(std::lround((gain + kHalfDecibelsMaxGain) * 2.0f))
                      : static_cast<uint8_t>(static_cast<int8_t>(std::lround(gain * 10.0f))));
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
    case DeviceFamily::Nova7P:
    case DeviceFamily::Nova7PGen2:
    case DeviceFamily::Nova4:
        level = data[2];
        update.headsetOff = data[3] == 0x00;
        update.charging = data[3] == 0x01 || data[3] == 0x02;
        break;
    case DeviceFamily::Arctis7Plus:
        level = data[2];
        update.headsetOff = data[1] == 0x01;
        update.charging = data[3] == 0x01;
        break;
    case DeviceFamily::GameBuds: {
        // The lower battery of the buds out of the case; in it, a bud reports 0%.
        if (size < 7) {
            return std::nullopt;
        }
        level = -1;
        for (const size_t bud : {size_t{0}, size_t{1}}) {
            if (data[3 + bud] == kBudOutOfCase) {
                level = level < 0 ? data[5 + bud] : std::min<int>(level, data[5 + bud]);
            }
        }
        update.headsetOff = level < 0;
        level = std::max(level, 0);
        break;
    }
    case DeviceFamily::Nova5X:
    case DeviceFamily::Nova3Wireless:
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

// The Arctis of before the Nova, as HeadsetControl and the Linux hid-steelseries-arctis driver document them; none
// is confirmed on a headset yet.

// Arctis 7 of 2017 and 2019, Arctis Pro and Arctis Pro + GameDAC: sidetone 06 35 [on] 00 [0-18], lights 06 55 01
// [02 on / 00 off], auto-off 06 51 [minutes, 0-90], the battery in percent in the answer to 06 18, and 06 09 to keep
// a setting. Whether the headset is on: 06 14, answered with 03 when it is and 01 when it is not (both Arctis 7
// models, as SteelSeries describes them). Also from that description, on the Arctis 7: microphone volume
// 06 33 00 [0-100] and the mute light 06 45 [1 steady, 2 breathing].
constexpr SettingSpec kLegacySidetone[] = {
    {0x35, Sidetone, SettingKind::Slider, 0, 18, {}, 0, 0, SettingWire::PaddedLevel},
};
constexpr SettingSpec kLegacyLights[] = {
    {0x55, EarcupLights, SettingKind::Toggle, 0, 2, {}, 0, 0, SettingWire::Prefixed, 0x01},
};
constexpr SettingOption kMuteLightStyles[] = {{1, OptSteady}, {2, OptBreathing}};
constexpr SettingSpec kArctis7Microphone[] = {
    {0x33, MicVolume, SettingKind::Slider, 0, 100, {}, 0, 0, SettingWire::Prefixed, 0x00},
    kLegacySidetone[0],
    {0x45, MuteLightStyle, SettingKind::Choice, 1, 2, kMuteLightStyles},
};
constexpr SettingSpec kArctis7Power[] = {
    {0x51, AutoOff, SettingKind::Choice, 0, 90, kMinuteTimeouts},
};
constexpr SettingSection kArctis7Sections[] = {
    {Microphone, kArctis7Microphone},
    {Lighting, kLegacyLights},
    {Power, kArctis7Power},
};
constexpr SettingSection kArctisProSections[] = {
    {Microphone, kLegacySidetone},
    {Lighting, kLegacyLights},
};
constexpr uint8_t kArctis7Battery = 0x18;
constexpr uint8_t kArctis7Link = 0x14;
constexpr uint8_t kArctis7HeadsetOn = 0x03;
constexpr uint8_t kArctis7HeadsetOff = 0x01;
// The dongle stays plugged in while the headset is off; asked this often, the output follows within seconds.
constexpr unsigned kArctis7BatteryPollMs = 5000;
// 0% replies in a row that count as switched off (offAtZeroBattery), so a single one does not move the sound away.
constexpr int kZeroBatteryReadsOff = 2;
// The original Arctis 7 answers 0% while it is off, which tells it until its link reply has been seen.
const HeadsetFeatures kArctis7{
    .sections = kArctis7Sections,
    .equalizer = kChipEqualizer,
    .save = kSaveState,
    .batteryQuery = kArctis7Battery,
    .linkQuery = kArctis7Link,
    .offAtZeroBattery = true,
    .batteryPollMs = kArctis7BatteryPollMs,
    .experimental = true,
};
const HeadsetFeatures kArctis7Rev2019{
    .sections = kArctis7Sections,
    .equalizer = kChipEqualizer,
    .save = kSaveState,
    .batteryQuery = kArctis7Battery,
    .linkQuery = kArctis7Link,
    .batteryPollMs = kArctis7BatteryPollMs,
    .experimental = true,
};
const HeadsetFeatures kArctisPro{
    .sections = kArctisProSections,
    .save = kSaveState,
    .experimental = true,
};

// Arctis 1 Wireless, Arctis 1 Xbox, Arctis 7X and Arctis 7P: sidetone as on the Arctis 7, auto-off 06 53 [minutes],
// 06 12 answered with [01 when the headset is off] [battery in percent]. SteelSeries' description of the Arctis 1
// Wireless (not of the 7X and 7P) adds the microphone volume and the sound chip of the Arctis 7.
constexpr SettingSpec kArctis1Microphone[] = {
    {0x33, MicVolume, SettingKind::Slider, 0, 100, {}, 0, 0, SettingWire::Prefixed, 0x00},
    kLegacySidetone[0],
};
constexpr SettingSpec kArctis1Power[] = {
    {0x53, AutoOff, SettingKind::Choice, 0, 90, kMinuteTimeouts},
};
constexpr SettingSection kArctis1Sections[] = {
    {Microphone, kArctis1Microphone},
    {Power, kArctis1Power},
};
constexpr SettingSection kArctis7XPSections[] = {
    {Microphone, kLegacySidetone},
    {Power, kArctis1Power},
};
constexpr uint8_t kArctis1Battery = 0x12;
constexpr uint8_t kArctis1HeadsetOff = 0x01;
const HeadsetFeatures kArctis1{
    .sections = kArctis1Sections,
    .equalizer = kChipEqualizer,
    .save = kSaveState,
    .batteryQuery = kArctis1Battery,
    .experimental = true,
};
const HeadsetFeatures kArctis7XP{
    .sections = kArctis7XPSections,
    .save = kSaveState,
    .batteryQuery = kArctis1Battery,
    .experimental = true,
};

// Arctis 5 (2017, 2019 and its editions), as SteelSeries describes it through Linux-Arctis-Manager: report ID 04 on
// interface 3 (page 000C), writes to registers of its chip as 40 [length] [address, 2 bytes] [value], then
// 40 01 11 54 9b to commit them. Sidetone at 12 c6 (0-7fff). Its dial mixes the Game and Chat devices in the headset
// and is not read; there is no battery.
constexpr uint8_t kRegisterWrite = 0x40;
constexpr SettingSpec kArctis5Microphone[] = {
    {0xC6, Sidetone, SettingKind::Slider, 0, 10, {}, 0, 0, SettingWire::Register16, 0x12},
};
constexpr SettingSection kArctis5Sections[] = {
    {Microphone, kArctis5Microphone},
};
constexpr SaveCommand kArctis5Commit[] = {{{kRegisterWrite, 0x01, 0x11, 0x54, 0x9B}, 5}};
const HeadsetFeatures kArctis5{
    .sections = kArctis5Sections,
    .equalizer = kCxEqualizer,
    .save = kArctis5Commit,
    .experimental = true,
};

// Arctis 9: sidetone 06 00 [c0 off up to fd], auto-off 04 00 [seconds, 16 bits], 90 00 to keep a setting. Its status
// 20 is answered with aa 01 while the headset is on, the battery as 64-9a at 3, 01 at 4 while it charges and the dial
// at 9 and 10; 55 or 03 first while it is off.
constexpr SettingOption kArctis9Sidetone[] = {{0xC0, OptOff}, {0xD4, OptLow}, {0xE8, OptMedium}, {0xFD, OptHigh}};
constexpr SettingSpec kArctis9Microphone[] = {
    {0x06, Sidetone, SettingKind::Choice, 0xC0, 0xFD, kArctis9Sidetone, 0, 0, SettingWire::Prefixed, 0x00},
};
constexpr SettingSpec kArctis9Power[] = {
    {0x04, AutoOff, SettingKind::Choice, 0, 90, kMinuteTimeouts, 0, 0, SettingWire::Seconds, 0x00},
};
// From SteelSeries' description through Linux-Arctis-Manager: surround 07 00 [0-1], one of the headset's five
// equalizer presets 0b 00 [0-4] (their curves and names are described nowhere, so they are numbered), Bluetooth with
// the headset 2b 00 [0-1] and during calls 2c 00 [0 as is, 1 mute, 2 12 dB lower].
constexpr SettingOption kArctis9CallMute[] = {{0, OptOff}, {1, OptMute}, {2, OptOff, 0, L"−12 dB"}};
constexpr SettingOption kArctis9EqPresets[] = {
    {0, OptOff, 0, L"1"}, {1, OptOff, 0, L"2"}, {2, OptOff, 0, L"3"}, {3, OptOff, 0, L"4"}, {4, OptOff, 0, L"5"},
};
constexpr SettingSpec kArctis9Sound[] = {
    {0x0B, EqualizerPreset, SettingKind::Choice, 0, 4, kArctis9EqPresets, 0, 0, SettingWire::Prefixed, 0x00},
    {0x07, SurroundSound, SettingKind::Toggle, 0, 1, {}, 0, 0, SettingWire::Prefixed, 0x00},
};
constexpr SettingSpec kArctis9Bluetooth[] = {
    {0x2B, BluetoothPowerOn, SettingKind::Toggle, 0, 1, {}, 0, 0, SettingWire::Prefixed, 0x00},
    {0x2C, BluetoothAutoMute, SettingKind::Choice, 0, 2, kArctis9CallMute, 0, 0, SettingWire::Prefixed, 0x00},
};
constexpr SettingSection kArctis9Sections[] = {
    {Microphone, kArctis9Microphone},
    {Sound, kArctis9Sound},
    {Power, kArctis9Power},
    {Bluetooth, kArctis9Bluetooth},
};
constexpr SaveCommand kArctis9Save[] = {{{0x90, 0x00}, 2}};
constexpr uint8_t kArctis9Status = 0x20;
constexpr uint8_t kArctis9BatteryEmpty = 0x64;
constexpr uint8_t kArctis9BatteryFull = 0x9A;
const HeadsetFeatures kArctis9{
    .sections = kArctis9Sections,
    .save = kArctis9Save,
    .batteryQuery = kArctis9Status,
    .experimental = true,
};

// Arctis Pro Wireless base station: sidetone 39 AA [0-9], auto-off 3c AA [tens of minutes], 90 AA to keep a setting.
// 41 AA is answered with 02 while the headset is off and 04 while it is on, 40 AA with the battery in steps of 0-4.
constexpr SettingOption kTenMinuteTimeouts[] = {{0, OptNever},     {1, OptNever, 10}, {2, OptNever, 20},
                                                {3, OptNever, 30}, {6, OptNever, 60}, {9, OptNever, 90}};
constexpr SettingSpec kProWirelessMicrophone[] = {
    {0x39, Sidetone, SettingKind::Slider, 0, 9, {}, 0, 0, SettingWire::Prefixed, 0xAA},
};
constexpr SettingSpec kProWirelessPower[] = {
    {0x3C, AutoOff, SettingKind::Choice, 0, 9, kTenMinuteTimeouts, 0, 0, SettingWire::Prefixed, 0xAA},
};
constexpr SettingSection kProWirelessSections[] = {
    {Microphone, kProWirelessMicrophone},
    {Power, kProWirelessPower},
};
constexpr SaveCommand kProWirelessSave[] = {{{0x90, 0xAA}, 2}};
constexpr uint8_t kProWirelessArgument = 0xAA;
constexpr uint8_t kProWirelessStatus = 0x41;
constexpr uint8_t kProWirelessBattery = 0x40;
constexpr uint8_t kProWirelessHeadsetOff = 0x02;
constexpr uint8_t kProWirelessHeadsetOn = 0x04;
constexpr int kProWirelessBatterySteps = 4;
const HeadsetFeatures kArctisProWireless{
    .sections = kProWirelessSections,
    .save = kProWirelessSave,
    .batteryQuery = kProWirelessBattery,
    .linkQuery = kProWirelessStatus,
    .queryArgument = kProWirelessArgument,
    .batteryAfterLink = true,
    .experimental = true,
};

const HeadsetFeatures& FeaturesFor(DeviceFamily family) {
    switch (family) {
    case DeviceFamily::NovaProWired: return kNovaProWired;
    case DeviceFamily::NovaProWireless: return kNovaProWireless;
    case DeviceFamily::Nova7: return kNova7;
    case DeviceFamily::Nova7Gen2: return kNova7Gen2;
    case DeviceFamily::Nova7P: return kNova7P;
    case DeviceFamily::Nova7PGen2: return kNova7PGen2;
    case DeviceFamily::Nova3: return kNova3;
    case DeviceFamily::Nova3Wireless: return kNova3Wireless;
    case DeviceFamily::Nova4: return kNova4;
    case DeviceFamily::GameBuds: return kGameBuds;
    case DeviceFamily::Arctis5: return kArctis5;
    case DeviceFamily::GameBudsCase: return kGameBudsCase;
    case DeviceFamily::Arctis7Plus: return kArctis7Plus;
    case DeviceFamily::NovaElite: return kNovaElite;
    case DeviceFamily::Arctis7: return kArctis7;
    case DeviceFamily::Arctis7Rev2019: return kArctis7Rev2019;
    case DeviceFamily::ArctisPro: return kArctisPro;
    case DeviceFamily::Arctis1: return kArctis1;
    case DeviceFamily::Arctis7XP: return kArctis7XP;
    case DeviceFamily::Arctis9: return kArctis9;
    case DeviceFamily::ArctisProWireless: return kArctisProWireless;
    case DeviceFamily::Nova5X: break;
    }
    return kNova5X;
}

std::vector<HeadsetUpdate> ParseHeadsetReport(const DeviceModel& model, const uint8_t* data, size_t size,
                                              uint8_t lastQuery) {
    using Kind = HeadsetUpdate::Kind;
    std::vector<HeadsetUpdate> updates;
    switch (model.protocol) {
    case DeviceProtocol::Elite:
        return ParseEliteReport(data, size);
    case DeviceProtocol::ChargingCase:
        // b7 [left] [right] [case] [left, right and case charging]: the case's battery.
        if (size >= 7 && data[0] == kCaseBatteryQuery) {
            updates.push_back({.kind = Kind::Battery, .value = std::min<int>(data[3], 100), .charging = data[6] == 1});
        }
        return updates;
    case DeviceProtocol::NovaStatus:
        if (const auto battery = ParseStatusBattery(model, data, size)) {
            updates.push_back(*battery);
        }
        return updates;
    case DeviceProtocol::ArctisLegacy:
        // 06 18 [battery 0-100] tells nothing about charging or whether the headset is on; 06 14 [03 while it is on]
        // does the latter.
        // A link reply with another value than the two described is left alone.
        if (size >= 3 && data[0] == 0x06 && data[1] == kArctis7Battery) {
            updates.push_back({.kind = Kind::Battery, .value = std::min<int>(data[2], 100), .linkKnown = false});
        } else if (size >= 3 && data[0] == 0x06 && data[1] == kArctis7Link &&
                   FeaturesFor(model.family).linkQuery == kArctis7Link &&
                   (data[2] == kArctis7HeadsetOn || data[2] == kArctis7HeadsetOff)) {
            updates.push_back({.kind = Kind::Link, .headsetOff = data[2] == kArctis7HeadsetOff});
        }
        return updates;
    case DeviceProtocol::Arctis1Legacy:
        // 06 12 [01 while the headset is off] [battery 0-100].
        if (size >= 4 && data[0] == 0x06 && data[1] == kArctis1Battery) {
            if (data[2] == kArctis1HeadsetOff) {
                updates.push_back({.kind = Kind::Link, .headsetOff = true});
            } else {
                updates.push_back({.kind = Kind::Battery, .value = std::min<int>(data[3], 100)});
            }
        }
        return updates;
    case DeviceProtocol::Arctis9Legacy:
        // aa 01 [?] [battery 64-9a] [01 while charging] while the headset is on; 55 or 03 first while it is off.
        if (size >= 5 && data[0] == 0xAA && data[1] == 0x01) {
            const int battery = (std::clamp(data[3], kArctis9BatteryEmpty, kArctis9BatteryFull) - kArctis9BatteryEmpty) *
                                100 / (kArctis9BatteryFull - kArctis9BatteryEmpty);
            updates.push_back({.kind = Kind::Battery, .value = battery, .charging = data[4] == 0x01});
        } else if (size >= 1 && (data[0] == 0x55 || data[0] == 0x03)) {
            updates.push_back({.kind = Kind::Link, .headsetOff = true});
        }
        return updates;
    case DeviceProtocol::ProWirelessLegacy:
        // The replies hold only the value, so the query sent last tells which one this is.
        if (size >= 1 && lastQuery == kProWirelessStatus &&
            (data[0] == kProWirelessHeadsetOff || data[0] == kProWirelessHeadsetOn)) {
            updates.push_back({.kind = Kind::Link, .headsetOff = data[0] == kProWirelessHeadsetOff});
        } else if (size >= 1 && lastQuery == kProWirelessBattery && data[0] <= kProWirelessBatterySteps) {
            updates.push_back({.kind = Kind::Battery, .value = data[0] * 100 / kProWirelessBatterySteps});
        }
        return updates;
    case DeviceProtocol::NovaPro:
        break;
    }
    if (size < 3) {
        return updates;
    }
    const HeadsetFeatures& features = FeaturesFor(model.family);
    const EqualizerSpec& eq = features.equalizer;

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
        // Until a link reply tells it, a 0% that may only mean the headset is off: off once it repeats, while the last
        // real battery stays.
        if (features.offAtZeroBattery && !state.linkSeen && update.value == 0) {
            if (++state.zeroBatteryReads >= kZeroBatteryReadsOff) {
                state.headsetOff = true;
            }
            break;
        }
        if (features.offAtZeroBattery && !state.linkSeen) {
            state.zeroBatteryReads = 0;
            state.headsetOff = false;
        }
        state.battery = update.value;
        state.spareBattery = update.spare;
        state.charging = update.charging;
        if (update.linkKnown) {
            state.headsetOff = update.headsetOff;
        }
        break;
    case Kind::Link:
        state.headsetOff = update.headsetOff;
        state.linkSeen = true;
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
    if (const SettingSpec* spec = features.FindSetting(command)) {
        const uint8_t byte = static_cast<uint8_t>(value);
        switch (spec->wire) {
        case SettingWire::SwitchedLevel: {
            // Off keeps the last level, so switching it on again restores it.
            const int last = state.values[command] > 0 ? state.values[command] : 1;
            return {command, static_cast<uint8_t>(value > 0 ? 1 : 0), static_cast<uint8_t>(value > 0 ? value : last)};
        }
        case SettingWire::Prefixed:
            return {command, spec->prefix, byte};
        case SettingWire::PaddedLevel:
            return {command, static_cast<uint8_t>(value > 0 ? 1 : 0), 0x00, byte};
        case SettingWire::Seconds: {
            const int seconds = std::max(value, 0) * 60;
            return {command, spec->prefix, static_cast<uint8_t>(seconds >> 8), static_cast<uint8_t>(seconds & 0xFF)};
        }
        case SettingWire::Trailing:
            return {command, byte, spec->prefix, spec->suffix};
        case SettingWire::Register16: {
            const int scaled = std::clamp(value, 0, static_cast<int>(spec->max)) * 0x7FFF / std::max<int>(spec->max, 1);
            return {kRegisterWrite, 0x02, spec->prefix, command, static_cast<uint8_t>(scaled >> 8),
                    static_cast<uint8_t>(scaled & 0xFF)};
        }
        case SettingWire::Byte:
            break;
        }
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
    if (spec.format == EqFormat::Chip) {
        std::vector<std::vector<uint8_t>> commands;
        for (int band = 0; band < spec.Bands(); ++band) {
            const int steps = std::clamp<int>(curve[band], -spec.range, spec.range);
            for (auto& command : ChipBandCommands(spec, band, steps / 2.0f)) {
                commands.push_back(std::move(command));
            }
        }
        if (std::vector<uint8_t> commit = ChipCommit(spec); !commit.empty()) {
            commands.push_back(std::move(commit));
        }
        return commands;
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

// On the AV6X02 a band without gain gets the filter that changes nothing written too, rather than only its closing
// write, as whether that write alone turns the band off is not known.
std::vector<std::vector<uint8_t>> ChipBandCommands(const EqualizerSpec& equalizer, int band, float gainDb) {
    std::vector<std::vector<uint8_t>> commands;
    if (equalizer.chip == SoundChip::Cx20892) {
        if (band >= 0 && band < static_cast<int>(std::size(kCxBands))) {
            commands.push_back(CxBandWrite(kCxBands[band], gainDb));
        }
        return commands;
    }
    if (band < 0 || band >= static_cast<int>(std::size(kChipBands))) {
        return commands;
    }
    const ChipBand& spec = kChipBands[band];
    const auto [b0, b1, b2, a1, a2] = ChipCoefficients(spec, gainDb);
    const float values[] = {static_cast<float>(b0), static_cast<float>(b1), static_cast<float>(b2), 1.0f,
                            static_cast<float>(-a1), static_cast<float>(-a2)};
    const uint8_t address = static_cast<uint8_t>(band << 4);
    const uint8_t rightFilters = static_cast<uint8_t>(spec.filterNumber + kChipRightFilters);
    for (const auto [channel, filters] : {std::pair{kChipLeft, spec.filterNumber}, std::pair{kChipRight, rightFilters}}) {
        for (uint8_t index = 0; index < std::size(values); ++index) {
            commands.push_back(ChipWrite(channel, static_cast<uint8_t>(address | kChipCoefficientAddress | index),
                                         values[index], static_cast<uint8_t>(filters + index)));
        }
        commands.push_back(ChipWrite(channel, address, 0.0f, static_cast<uint8_t>(filters + std::size(values))));
    }
    return commands;
}

std::vector<uint8_t> ChipCommit(const EqualizerSpec& spec) {
    if (spec.chip == SoundChip::Cx20892) {
        return {std::begin(kCxCommit), std::end(kCxCommit)};
    }
    return {};
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
            AppendFilter(report, band, spec);
        }
        return {report};
    }
    std::vector<std::vector<uint8_t>> commands;
    if (spec.nameCommand) {
        std::vector<uint8_t> name{spec.nameCommand, kGen2Radio, kGen2CustomPreset};
        name.insert(name.end(), std::begin(kGen2PresetName), std::end(kGen2PresetName) - 1);
        commands.push_back(std::move(name));
    }
    std::vector<uint8_t> bands{spec.bandsCommand};
    if (spec.linkByte) {
        bands.push_back(kGen2Radio);
    }
    for (const EqBand& band : filters) {
        AppendFilter(bands, band, spec);
    }
    commands.push_back(std::move(bands));
    if (spec.applyCommand) {
        commands.push_back({spec.applyCommand});
    }
    return commands;
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
    return CurveFilters(curve, spec.range, !flat && !spec.peaksOnly);
}

EqFilters HeadsetFilters(const EqFilters& bands, const EqualizerSpec& spec) {
    ParametricEq eq;
    eq.bands = bands;
    EqFilters filters = PresetFilters(eq, spec.notch);
    if (spec.peaksOnly) {
        for (EqBand& band : filters) {
            if (band.type == ::FilterType::LowPass || band.type == ::FilterType::HighPass) {
                band.gain = 0.0f;
            }
            band.type = ::FilterType::Peaking;
        }
    }
    return filters;
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
