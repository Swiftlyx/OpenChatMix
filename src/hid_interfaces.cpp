#include "hid_interfaces.h"

#include <hidsdi.h>
#include <setupapi.h>

#include "devices.h"

namespace {

bool QueryCaps(HidInterface& item) {
    // Zero access rights are enough for HidD_GetPreparsedData and never conflict with other owners.
    HANDLE device = CreateFileW(item.path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
                                nullptr);
    if (device == INVALID_HANDLE_VALUE) {
        return false;
    }
    bool ok = false;
    PHIDP_PREPARSED_DATA preparsed = nullptr;
    if (HidD_GetPreparsedData(device, &preparsed)) {
        HIDP_CAPS caps{};
        if (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS) {
            item.usagePage = caps.UsagePage;
            item.usage = caps.Usage;
            item.inputLength = caps.InputReportByteLength;
            item.outputLength = caps.OutputReportByteLength;
            item.featureLength = caps.FeatureReportByteLength;
            ok = true;
        }
        HidD_FreePreparsedData(preparsed);
    }
    CloseHandle(device);
    return ok;
}

}  // namespace

std::vector<HidInterface> EnumerateSteelSeriesInterfaces(const std::function<bool(uint16_t productId)>& accept) {
    std::vector<HidInterface> result;
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);
    HDEVINFO set = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) {
        return result;
    }
    SP_DEVICE_INTERFACE_DATA item{};
    item.cbSize = sizeof(item);
    for (DWORD index = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &hidGuid, index, &item); ++index) {
        DWORD size = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &item, nullptr, 0, &size, nullptr);
        if (size == 0) {
            continue;
        }
        std::vector<BYTE> buffer(size);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &item, detail, size, nullptr, nullptr)) {
            continue;
        }
        HidInterface hid;
        hid.path = detail->DevicePath;
        const auto productId = ParseSteelSeriesProductId(hid.path);
        const auto interfaceNumber = ParseInterfaceNumber(hid.path);
        if (!productId || !interfaceNumber || !accept(*productId)) {
            continue;
        }
        hid.productId = *productId;
        hid.interfaceNumber = *interfaceNumber;
        if (QueryCaps(hid)) {
            result.push_back(std::move(hid));
        }
    }
    SetupDiDestroyDeviceInfoList(set);
    return result;
}

std::string HexBytes(const uint8_t* data, size_t length) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string text;
    for (size_t i = 0; i < length; ++i) {
        if (i) {
            text += ' ';
        }
        text += kDigits[data[i] >> 4];
        text += kDigits[data[i] & 0x0F];
    }
    return text;
}
