#include "devices.h"

#include <windows.h>

#include <algorithm>
#include <string>

namespace {

using enum DeviceProtocol;
using enum DeviceFamily;

// Product IDs from HeadsetControl, Linux-Arctis-Manager and the Linux hid-steelseries driver.
// Only the Nova Pro Wired has been verified with this app. Battery steps follow HeadsetControl: older
// Nova 7 firmware and the Arctis 7+ report 0-4, newer models a percentage. The Gen 2 family follows
// Linux-Arctis-Manager: Gen 2 hardware and the IDs Nova 7 models take on the Gen 2 firmware.
constexpr DeviceModel kModels[] = {
    {0x12CB, L"Arctis Nova Pro Wired", NovaPro, NovaProWired, 0, 0},
    {0x12CD, L"Arctis Nova Pro Wired", NovaPro, NovaProWired, 0, 0},
    {0x12E0, L"Arctis Nova Pro Wireless", NovaPro, NovaProWireless, 0, 0},
    {0x12E5, L"Arctis Nova Pro Wireless", NovaPro, NovaProWireless, 0, 0},
    {0x2202, L"Arctis Nova 7", NovaStatus, Nova7, 4, 5, 4},
    {0x22A1, L"Arctis Nova 7", NovaStatus, Nova7Gen2, 4, 5},
    {0x227E, L"Arctis Nova 7 Gen 2", NovaStatus, Nova7Gen2, 4, 5},
    {0x2206, L"Arctis Nova 7X", NovaStatus, Nova7, 4, 5, 4},
    {0x2258, L"Arctis Nova 7X", NovaStatus, Nova7, 4, 5},
    {0x229E, L"Arctis Nova 7X", NovaStatus, Nova7Gen2, 4, 5},
    {0x22AD, L"Arctis Nova 7X", NovaStatus, Nova7Gen2, 4, 5},
    {0x22A4, L"Arctis Nova 7X", NovaStatus, Nova7, 4, 5, 4},
    {0x22A5, L"Arctis Nova 7X", NovaStatus, Nova7Gen2, 4, 5},
    {0x223A, L"Arctis Nova 7 Diablo IV", NovaStatus, Nova7, 4, 5, 4},
    {0x22A9, L"Arctis Nova 7 Diablo IV", NovaStatus, Nova7Gen2, 4, 5},
    {0x227A, L"Arctis Nova 7 WoW Edition", NovaStatus, Nova7, 4, 5, 4},
    {0x22AB, L"Arctis Nova 7 WoW Edition", NovaStatus, Nova7Gen2, 4, 5},
    {0x220E, L"Arctis 7+", NovaStatus, Arctis7Plus, 4, 5, 4},
    {0x2212, L"Arctis 7P+", NovaStatus, Arctis7Plus, 4, 5, 4},
    {0x2216, L"Arctis 7X+", NovaStatus, Arctis7Plus, 4, 5, 4},
    {0x2236, L"Arctis 7+ Destiny", NovaStatus, Arctis7Plus, 4, 5, 4},
    // The Linux driver reads chat before game on the Nova 5X.
    {0x2253, L"Arctis Nova 5X", NovaStatus, Nova5X, 6, 5},
    {0x2264, L"Arctis Nova 5X", NovaStatus, Nova5X, 6, 5},
    // Experimental: from HeadsetControl issue #481 (captures of a real base station) and Linux-Arctis-Manager.
    {0x2244, L"Arctis Nova Elite", Elite, NovaElite, 0, 0},
    {0x2270, L"Arctis Nova Elite", Elite, NovaElite, 0, 0},
    // Experimental: the protocols as HeadsetControl and the Linux hid-steelseries-arctis driver document them.
    {0x1260, L"Arctis 7", ArctisLegacy, Arctis7, 0, 0},
    {0x12AD, L"Arctis 7 (2019)", ArctisLegacy, Arctis7Rev2019, 0, 0},
    {0x1252, L"Arctis Pro", ArctisLegacy, ArctisPro, 0, 0},
    {0x1280, L"Arctis Pro + GameDAC", ArctisLegacy, ArctisPro, 0, 0},
    {0x12B3, L"Arctis 1 Wireless", Arctis1Legacy, Arctis1, 0, 0},
    {0x12B6, L"Arctis 1 Wireless Xbox", Arctis1Legacy, Arctis1, 0, 0},
    {0x12D7, L"Arctis 7X", Arctis1Legacy, Arctis1, 0, 0},
    {0x12D5, L"Arctis 7P", Arctis1Legacy, Arctis1, 0, 0},
    {0x12C2, L"Arctis 9", Arctis9Legacy, Arctis9, 0, 0},
    {0x12C4, L"Arctis 9", Arctis9Legacy, Arctis9, 0, 0},
    {0x1290, L"Arctis Pro Wireless", ProWirelessLegacy, ArctisProWireless, 0, 0},
};

// The Arctis 7 reports each side's cut by its dial as a signed step count: 0 is full, about -64 silent.
int Arctis7Level(uint8_t value) {
    const int steps = -static_cast<int8_t>(value);
    return std::clamp(100 - steps * 100 / 64, 0, 100);
}

// The Arctis 9 reports each side's level by its dial in steps of 0-19.
constexpr int kArctis9DialSteps = 19;
constexpr size_t kArctis9GameOffset = 9;
constexpr size_t kArctis9ChatOffset = 10;

int Arctis9Level(uint8_t value) {
    return std::min<int>(value, kArctis9DialSteps) * 100 / kArctis9DialSteps;
}

std::optional<unsigned> ParseHexAfter(std::wstring_view path, std::wstring_view marker, size_t digits) {
    std::wstring lower(path);
    CharLowerBuffW(lower.data(), static_cast<DWORD>(lower.size()));
    const size_t at = lower.find(marker);
    if (at == std::wstring::npos || at + marker.size() + digits > lower.size()) {
        return std::nullopt;
    }
    unsigned value = 0;
    for (size_t i = 0; i < digits; ++i) {
        const wchar_t c = lower[at + marker.size() + i];
        value <<= 4;
        if (c >= L'0' && c <= L'9') {
            value |= c - L'0';
        } else if (c >= L'a' && c <= L'f') {
            value |= c - L'a' + 10;
        } else {
            return std::nullopt;
        }
    }
    return value;
}

uint8_t Level(uint8_t raw) {
    return std::min<uint8_t>(raw, 100);
}

}  // namespace

std::span<const DeviceModel> SupportedDevices() {
    return kModels;
}

const DeviceModel* FindDeviceModel(uint16_t productId) {
    for (const auto& model : kModels) {
        if (model.productId == productId) {
            return &model;
        }
    }
    return nullptr;
}

std::optional<uint16_t> ParseSteelSeriesProductId(std::wstring_view path) {
    const auto pid = ParseHexAfter(path, L"vid_1038&pid_", 4);
    return pid ? std::optional<uint16_t>(static_cast<uint16_t>(*pid)) : std::nullopt;
}

std::optional<int> ParseInterfaceNumber(std::wstring_view path) {
    const auto number = ParseHexAfter(path, L"&mi_", 2);
    return number ? std::optional<int>(static_cast<int>(*number)) : std::nullopt;
}

std::optional<Levels> ParseChatMixReport(const DeviceModel& model, const uint8_t* data, size_t size,
                                         bool& pushedEvent) {
    pushedEvent = false;
    switch (model.protocol) {
    case NovaPro:
    case Elite:
        if (size >= 4 && data[0] == 0x07 && data[1] == 0x45) {
            pushedEvent = true;
            return Levels{Level(data[2]), Level(data[3])};
        }
        break;
    case NovaStatus: {
        const size_t needed = std::max(model.statusGameOffset, model.statusChatOffset) + 1u;
        if (size >= needed && data[0] == 0xB0) {
            return Levels{Level(data[model.statusGameOffset]), Level(data[model.statusChatOffset])};
        }
        if (size >= 3 && data[0] == 0x45) {
            pushedEvent = true;
            return Levels{Level(data[1]), Level(data[2])};
        }
        break;
    }
    case ArctisLegacy:
        // 06 24 [game] [chat], the answer to the poll.
        if (size >= 4 && data[0] == 0x06 && data[1] == 0x24) {
            return Levels{Arctis7Level(data[2]), Arctis7Level(data[3])};
        }
        break;
    case Arctis9Legacy:
        // aa 01 ... [game] [chat] at 9 and 10 while the headset is on.
        if (size > kArctis9ChatOffset && data[0] == 0xAA && data[1] == 0x01) {
            return Levels{Arctis9Level(data[kArctis9GameOffset]), Arctis9Level(data[kArctis9ChatOffset])};
        }
        break;
    case Arctis1Legacy:
    case ProWirelessLegacy:
        break;
    }
    return std::nullopt;
}

bool MixesInHeadset(const DeviceModel& model) {
    return model.protocol == ArctisLegacy || model.protocol == Arctis9Legacy || model.protocol == ProWirelessLegacy;
}

bool ReportsDial(const DeviceModel& model) {
    return model.protocol != Arctis1Legacy && model.protocol != ProWirelessLegacy;
}
