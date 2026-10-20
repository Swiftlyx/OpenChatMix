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

// Parses "vid_1038&pid_xxxx" and "&mi_xx" from a device path or hardware ID (case-insensitive).
std::optional<uint16_t> ParseSteelSeriesProductId(std::wstring_view path);
std::optional<int> ParseInterfaceNumber(std::wstring_view path);

// Extracts ChatMix levels from an input report. Data starts with the report ID for
// numbered reports; the 0x00 prefix of unnumbered reports must already be stripped.
std::optional<Levels> ParseChatMixReport(const DeviceModel& model, const uint8_t* data, size_t size,
                                         bool& pushedEvent);
