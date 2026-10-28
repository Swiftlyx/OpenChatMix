#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "balance.h"

constexpr uint16_t kSteelSeriesVendorId = 0x1038;

enum class DeviceProtocol {
    // GameDAC / Nova Pro base station: commands use report ID 0x06, events report ID 0x07.
    // The dial pushes 07 45 [game] [chat] once ChatMix mode is enabled with 06 49 01.
    NovaPro,
    // Nova 7 / 7X / 7+ / 5X dongles: no report IDs, status polled with 00 b0.
    // Gen 2 firmware additionally pushes 45 [game] [chat] events.
    NovaStatus,
    // Nova Elite base station (experimental): like the Nova Pro, but on interface 3 and with report ID 0x01 for
    // commands and their replies; events still arrive as report ID 0x07.
    Elite,
    // Arctis 7 of 2017 and 2019, Arctis Pro and Arctis Pro + GameDAC (experimental): 31-byte reports with report ID
    // 0x06 on interface 5, answered on the same interface. The dial mixes the Game and Chat devices in the headset;
    // 06 24 asks where it stands.
    ArctisLegacy,
    // Arctis 1 Wireless, Arctis 1 Xbox, Arctis 7X and Arctis 7P (experimental): the same reports on interface 3
    // (page FF43); no dial.
    Arctis1Legacy,
    // Arctis 9 (experimental): unnumbered reports on interface 0. 20 asks for the status: the dial, the battery and
    // whether it charges. Its dial mixes the Game and Chat devices in the headset.
    Arctis9Legacy,
    // Arctis Pro Wireless base station (experimental): unnumbered reports on interface 0, each command followed by
    // AA, its replies without a header. The base station's knob mixes the Game and Chat devices; it is not read.
    ProWirelessLegacy,
};

// Selects the set of native headset settings (see headset.h).
enum class DeviceFamily {
    NovaProWired,
    NovaProWireless,
    Nova7,
    Nova7Gen2,  // Gen 2 hardware, and Nova 7 models on the Gen 2 firmware: parametric equalizer.
    Arctis7Plus,
    Nova5X,
    NovaElite,
    Arctis7,
    Arctis7Rev2019,  // The Arctis 7 of 2019, whose link reply HeadsetControl confirms; no 0% guess needed.
    ArctisPro,       // Arctis Pro and Arctis Pro + GameDAC, wired: no battery.
    Arctis1,
    Arctis7XP,       // Arctis 7X and 7P: the Arctis 1 Wireless' commands, without what only its description has.
    Arctis9,
    ArctisProWireless,
};

struct DeviceModel {
    uint16_t productId;
    const wchar_t* name;
    DeviceProtocol protocol;
    DeviceFamily family;
    uint8_t statusGameOffset;  // NovaStatus only: offsets inside the b0 status report.
    uint8_t statusChatOffset;
    uint8_t statusBatteryMax = 100;  // NovaStatus only: full battery in the b0 report; 4 on five-step models.
};

std::span<const DeviceModel> SupportedDevices();
const DeviceModel* FindDeviceModel(uint16_t productId);
// The headset mixes Game and Chat itself by its dial, so the app adds no balance of its own.
bool MixesInHeadset(const DeviceModel& model);
// The position of that dial can be read; the Arctis Pro Wireless keeps its knob to itself.
bool ReportsDial(const DeviceModel& model);

// Parses "vid_1038&pid_xxxx" and "&mi_xx" from a device path or hardware ID (case-insensitive).
std::optional<uint16_t> ParseSteelSeriesProductId(std::wstring_view path);
std::optional<int> ParseInterfaceNumber(std::wstring_view path);

// Extracts ChatMix levels from an input report. Data starts with the report ID for
// numbered reports; the 0x00 prefix of unnumbered reports must already be stripped.
std::optional<Levels> ParseChatMixReport(const DeviceModel& model, const uint8_t* data, size_t size,
                                         bool& pushedEvent);
