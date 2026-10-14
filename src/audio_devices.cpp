#include "audio_devices.h"

#include <devicetopology.h>
#include <wrl/client.h>

#include <algorithm>
#include <cwctype>
#include <string_view>

#include "devices.h"

using Microsoft::WRL::ComPtr;

namespace {

// PKEY_Device_FriendlyName, defined locally to avoid pulling initguid.h into this unit.
constexpr PROPERTYKEY kFriendlyNameKey = {
    {0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

// Undocumented interface behind the Windows sound settings, unchanged since Windows 7. Only the
// slot order matters for the methods that are not called here.
MIDL_INTERFACE("f8679f50-850a-41cf-9c72-430f290290c8")
IPolicyConfig : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR, void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR, INT, void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR, void*, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR, INT, PINT64, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR deviceId, ERole role) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR deviceId, INT visible) = 0;
};

class DECLSPEC_UUID("870af99c-171d-4f9e-af0d-e63df40c2bc9") CPolicyConfigClient;

ComPtr<IPolicyConfig> PolicyConfig() {
    ComPtr<IPolicyConfig> config;
    if (FAILED(CoCreateInstance(__uuidof(CPolicyConfigClient), nullptr, CLSCTX_ALL, __uuidof(IPolicyConfig), &config))) {
        return nullptr;
    }
    return config;
}

}  // namespace

std::wstring EndpointFriendlyName(IMMDevice* device) {
    ComPtr<IPropertyStore> store;
    if (FAILED(device->OpenPropertyStore(STGM_READ, &store))) {
        return {};
    }
    std::wstring name;
    PROPVARIANT value;
    PropVariantInit(&value);
    if (SUCCEEDED(store->GetValue(kFriendlyNameKey, &value)) && value.vt == VT_LPWSTR) {
        name = value.pwszVal;
    }
    PropVariantClear(&value);
    return name;
}

std::wstring EndpointIdOf(IMMDevice* device) {
    LPWSTR id = nullptr;
    if (FAILED(device->GetId(&id))) {
        return {};
    }
    std::wstring result = id;
    CoTaskMemFree(id);
    return result;
}

std::wstring EndpointAdapterId(IMMDevice* device) {
    ComPtr<IDeviceTopology> endpointTopology;
    if (FAILED(device->Activate(__uuidof(IDeviceTopology), CLSCTX_ALL, nullptr, &endpointTopology))) {
        return {};
    }
    ComPtr<IConnector> endpointConnector;
    ComPtr<IConnector> adapterConnector;
    ComPtr<IPart> adapterPart;
    ComPtr<IDeviceTopology> adapterTopology;
    if (FAILED(endpointTopology->GetConnector(0, &endpointConnector)) ||
        FAILED(endpointConnector->GetConnectedTo(&adapterConnector)) || FAILED(adapterConnector.As(&adapterPart)) ||
        FAILED(adapterPart->GetTopologyObject(&adapterTopology))) {
        return {};
    }
    LPWSTR id = nullptr;
    if (FAILED(adapterTopology->GetDeviceId(&id))) {
        return {};
    }
    std::wstring result = id;
    CoTaskMemFree(id);
    return result;
}

std::vector<AudioEndpoint> ListRenderEndpoints(bool includeDisabled) {
    std::vector<AudioEndpoint> endpoints;
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDeviceCollection> devices;
    UINT count = 0;
    const DWORD states = DEVICE_STATE_ACTIVE | (includeDisabled ? DEVICE_STATE_DISABLED : 0);
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))) ||
        FAILED(enumerator->EnumAudioEndpoints(eRender, states, &devices)) || FAILED(devices->GetCount(&count))) {
        return endpoints;
    }
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> device;
        if (SUCCEEDED(devices->Item(i, &device))) {
            DWORD state = 0;
            device->GetState(&state);
            endpoints.push_back({EndpointIdOf(device.Get()), EndpointFriendlyName(device.Get()),
                                 ParseSteelSeriesProductId(EndpointAdapterId(device.Get())),
                                 state == DEVICE_STATE_ACTIVE});
        }
    }
    return endpoints;
}

std::wstring DefaultRenderEndpoint(ERole role) {
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice> device;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))) ||
        FAILED(enumerator->GetDefaultAudioEndpoint(eRender, role, &device))) {
        return {};
    }
    return EndpointIdOf(device.Get());
}

bool SetDefaultRenderEndpoint(const std::wstring& id, ERole role) {
    const ComPtr<IPolicyConfig> config = PolicyConfig();
    return config && !id.empty() && SUCCEEDED(config->SetDefaultEndpoint(id.c_str(), role));
}

bool SetEndpointEnabled(const std::wstring& id, bool enabled) {
    const ComPtr<IPolicyConfig> config = PolicyConfig();
    return config && !id.empty() && SUCCEEDED(config->SetEndpointVisibility(id.c_str(), enabled ? TRUE : FALSE));
}

std::optional<VirtualDevicePair> FindVirtualDevices(const std::vector<AudioEndpoint>& endpoints) {
    auto startsWith = [](std::wstring_view text, std::wstring_view prefix) {
        return text.substr(0, prefix.size()) == prefix;
    };
    // "CABLE-A Input (VB-Audio Cable A)": a letter after the dash, then " Input".
    auto isCable = [&](std::wstring_view name) {
        constexpr std::wstring_view kPrefix = L"CABLE-";
        constexpr std::wstring_view kSuffix = L" Input";
        return name.size() > kPrefix.size() + kSuffix.size() && startsWith(name, kPrefix) &&
               std::iswalpha(name[kPrefix.size()]) && name.substr(kPrefix.size() + 1, kSuffix.size()) == kSuffix;
    };
    // Lower rank first; names order cables of the same kind.
    auto rank = [&](std::wstring_view name) {
        if (isCable(name)) {
            return 0;
        }
        if (startsWith(name, L"Hi-Fi Cable Input")) {
            return 1;
        }
        // "Line 1 (Virtual Audio Cable)"
        if (startsWith(name, L"Line ") && name.find(L"(Virtual Audio Cable)") != std::wstring_view::npos) {
            return 2;
        }
        return -1;
    };
    std::vector<const AudioEndpoint*> cables;
    for (const auto& endpoint : endpoints) {
        if (rank(endpoint.name) >= 0) {
            cables.push_back(&endpoint);
        }
    }
    if (cables.size() < 2) {
        return std::nullopt;
    }
    std::sort(cables.begin(), cables.end(), [&](const AudioEndpoint* a, const AudioEndpoint* b) {
        const int rankA = rank(a->name);
        const int rankB = rank(b->name);
        return rankA != rankB ? rankA < rankB : a->name < b->name;
    });
    return VirtualDevicePair{*cables[0], *cables[1]};
}

bool IsSonarEndpoint(const AudioEndpoint& endpoint) {
    return endpoint.name.find(L"SteelSeries Sonar") != std::wstring::npos;
}
