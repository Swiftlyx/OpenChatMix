#pragma once

#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

constexpr USHORT kVendorUsagePageMin = 0xFF00;

// One HID collection of a SteelSeries USB device.
struct HidInterface {
    std::wstring path;
    uint16_t productId = 0;
    int interfaceNumber = -1;
    USHORT usagePage = 0;
    USHORT usage = 0;
    USHORT inputLength = 0;
    USHORT outputLength = 0;
    USHORT featureLength = 0;
};

// Present HID collections of SteelSeries devices whose product ID passes accept, with their report lengths.
std::vector<HidInterface> EnumerateSteelSeriesInterfaces(const std::function<bool(uint16_t productId)>& accept);

// "0a 1b 2c" for the log.
std::string HexBytes(const uint8_t* data, size_t length);
