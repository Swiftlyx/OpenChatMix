#include "audio_devices.h"

#include <cfgmgr32.h>
#include <devicetopology.h>
#include <devpropdef.h>
#include <setupapi.h>
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

// PKEY_AudioEndpoint_FormFactor, an EndpointFormFactor value.
constexpr PROPERTYKEY kFormFactorKey = {
    {0x1da5d803, 0xd492, 0x4edd, {0x8c, 0x23, 0xe0, 0xc0, 0xff, 0xee, 0x7f, 0x0e}}, 0};

// PKEY_Device_ContainerId / DEVPKEY_Device_ContainerId: every device node of one physical device shares it,
// its audio endpoints included.
constexpr GUID kContainerIdFormat = {0x8c7ed206, 0x3f8a, 0x4827, {0xb3, 0xab, 0xae, 0x9e, 0x1f, 0xae, 0xfc, 0x6c}};
constexpr PROPERTYKEY kContainerIdKey = {kContainerIdFormat, 2};
constexpr DEVPROPKEY kDeviceContainerIdKey = {kContainerIdFormat, 2};

// The battery level Windows keeps for a Bluetooth device that reports one, a byte in percent, on one of the
// device's nodes under the Bluetooth enumerators.
constexpr DEVPROPKEY kBluetoothBatteryKey = {
    {0x104ea319, 0x6ee2, 0x4701, {0xbd, 0x47, 0x8d, 0xdb, 0xf4, 0x25, 0xbb, 0xe5}}, 2};

// The adapter's connector that an endpoint is wired to.
ComPtr<IConnector> AdapterConnector(IMMDevice* device) {
    ComPtr<IDeviceTopology> endpointTopology;
    ComPtr<IConnector> endpointConnector;
    ComPtr<IConnector> adapterConnector;
    if (FAILED(device->Activate(__uuidof(IDeviceTopology), CLSCTX_ALL, nullptr, &endpointTopology)) ||
        FAILED(endpointTopology->GetConnector(0, &endpointConnector)) ||
        FAILED(endpointConnector->GetConnectedTo(&adapterConnector))) {
        return nullptr;
    }
    return adapterConnector;
}

std::optional<UINT> FormFactorOf(IMMDevice* device) {
    ComPtr<IPropertyStore> store;
    if (FAILED(device->OpenPropertyStore(STGM_READ, &store))) {
        return std::nullopt;
    }
    std::optional<UINT> formFactor;
    PROPVARIANT value;
    PropVariantInit(&value);
    if (SUCCEEDED(store->GetValue(kFormFactorKey, &value)) && value.vt == VT_UI4) {
        formFactor = value.ulVal;
    }
    PropVariantClear(&value);
    return formFactor;
}

// The jack behind an endpoint as its sound card describes it: Unknown for built-in speakers, nothing when
// the card describes no jack.
std::optional<EndpointLink> JackLinkOf(IMMDevice* device) {
    ComPtr<IConnector> connector = AdapterConnector(device);
    ComPtr<IPart> part;
    ComPtr<IKsJackDescription> jacks;
    UINT count = 0;
    if (!connector || FAILED(connector.As(&part)) ||
        FAILED(part->Activate(CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&jacks))) || FAILED(jacks->GetJackCount(&count)) ||
        count == 0) {
        return std::nullopt;
    }
    for (UINT i = 0; i < count; ++i) {
        KSJACK_DESCRIPTION jack{};
        if (FAILED(jacks->GetJackDescription(i, &jack)) || jack.PortConnection == ePortConnIntegratedDevice ||
            jack.GenLocation == eGenLocInternal) {
            continue;
        }
        // Laptops share one endpoint between the speakers and the headphone jack.
        if (jack.PortConnection == ePortConnBothIntegratedAndJack && !jack.IsConnected) {
            continue;
        }
        switch (jack.ConnectionType) {
        case eConnType3Point5mm:
        case eConnTypeQuarter:
        case eConnTypeCombination:
        case eConnTypeOtherAnalog:
            return EndpointLink::Jack;
        case eConnTypeOptical:
            return EndpointLink::Spdif;
        default:
            break;
        }
    }
    return EndpointLink::Unknown;
}

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
    ComPtr<IConnector> adapterConnector = AdapterConnector(device);
    ComPtr<IPart> adapterPart;
    ComPtr<IDeviceTopology> adapterTopology;
    if (!adapterConnector || FAILED(adapterConnector.As(&adapterPart)) ||
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

EndpointLink EndpointLinkOf(const std::wstring& endpointId) {
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice> device;
    if (endpointId.empty() ||
        FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))) ||
        FAILED(enumerator->GetDevice(endpointId.c_str(), &device))) {
        return EndpointLink::Unknown;
    }
    std::wstring adapter = EndpointAdapterId(device.Get());
    std::transform(adapter.begin(), adapter.end(), adapter.begin(), [](wchar_t c) { return std::towlower(c); });
    // The bus starts the adapter's device path: usb#, bthenum# (bthhfenum#, bthledevice#), hdaudio# and
    // the like for sound cards, root# or swd# for virtual devices.
    if (adapter.find(L"\\usb#") != std::wstring::npos) {
        return EndpointLink::Usb;
    }
    if (adapter.find(L"\\bth") != std::wstring::npos || adapter.find(L"#bth") != std::wstring::npos) {
        return EndpointLink::Bluetooth;
    }
    if (adapter.empty() || adapter.find(L"\\root#") != std::wstring::npos ||
        adapter.find(L"\\swd#") != std::wstring::npos) {
        return EndpointLink::Unknown;
    }
    const std::optional<UINT> formFactor = FormFactorOf(device.Get());
    auto is = [&formFactor](EndpointFormFactor kind) { return formFactor == static_cast<UINT>(kind); };
    if (is(DigitalAudioDisplayDevice)) {
        return EndpointLink::Hdmi;
    }
    if (is(SPDIF)) {
        return EndpointLink::Spdif;
    }
    if (const std::optional<EndpointLink> jack = JackLinkOf(device.Get())) {
        return *jack;
    }
    // Without a jack description, only headphones and headsets surely plug into one.
    return is(Headphones) || is(Headset) ? EndpointLink::Jack : EndpointLink::Unknown;
}

std::optional<int> BluetoothBatteryOf(const std::wstring& endpointId) {
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice> device;
    ComPtr<IPropertyStore> store;
    if (endpointId.empty() ||
        FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))) ||
        FAILED(enumerator->GetDevice(endpointId.c_str(), &device)) ||
        FAILED(device->OpenPropertyStore(STGM_READ, &store))) {
        return std::nullopt;
    }
    GUID container{};
    PROPVARIANT value;
    PropVariantInit(&value);
    const bool known = SUCCEEDED(store->GetValue(kContainerIdKey, &value)) && value.vt == VT_CLSID && value.puuid;
    if (known) {
        container = *value.puuid;
    }
    PropVariantClear(&value);
    if (!known) {
        return std::nullopt;
    }
    // Classic Bluetooth devices sit under BTHENUM, Bluetooth LE devices under BTHLE.
    for (const wchar_t* bus : {L"BTHENUM", L"BTHLE"}) {
        HDEVINFO set = SetupDiGetClassDevsW(nullptr, bus, nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
        if (set == INVALID_HANDLE_VALUE) {
            continue;
        }
        std::optional<int> battery;
        SP_DEVINFO_DATA node{};
        node.cbSize = sizeof(node);
        for (DWORD index = 0; !battery && SetupDiEnumDeviceInfo(set, index, &node); ++index) {
            GUID nodeContainer{};
            DEVPROPTYPE type = DEVPROP_TYPE_EMPTY;
            if (!SetupDiGetDevicePropertyW(set, &node, &kDeviceContainerIdKey, &type,
                                           reinterpret_cast<PBYTE>(&nodeContainer), sizeof(nodeContainer), nullptr, 0) ||
                type != DEVPROP_TYPE_GUID || !IsEqualGUID(nodeContainer, container)) {
                continue;
            }
            BYTE level = 0;
            if (SetupDiGetDevicePropertyW(set, &node, &kBluetoothBatteryKey, &type, &level, sizeof(level), nullptr, 0) &&
                type == DEVPROP_TYPE_BYTE && level <= 100) {
                battery = level;
            }
        }
        SetupDiDestroyDeviceInfoList(set);
        if (battery) {
            return battery;
        }
    }
    return std::nullopt;
}

bool IsHeadphonesEndpoint(const std::wstring& endpointId) {
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice> device;
    if (endpointId.empty() ||
        FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))) ||
        FAILED(enumerator->GetDevice(endpointId.c_str(), &device))) {
        return false;
    }
    const std::optional<UINT> formFactor = FormFactorOf(device.Get());
    return formFactor == static_cast<UINT>(Headphones) || formFactor == static_cast<UINT>(Headset);
}

namespace {

// The container of the computer itself, which its sound cards and virtual devices belong to.
constexpr GUID kComputerContainer = {0x00000000, 0x0000, 0x0000, {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}};

std::wstring LowerInstanceId(DEVINST node) {
    wchar_t id[MAX_DEVICE_ID_LEN] = {};
    if (CM_Get_Device_IDW(node, id, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS) {
        return {};
    }
    std::wstring text = id;
    CharLowerBuffW(text.data(), static_cast<DWORD>(text.size()));
    return text;
}

std::wstring GuidText(const GUID& guid) {
    wchar_t text[64] = {};
    StringFromGUID2(guid, text, ARRAYSIZE(text));
    std::wstring lower = text;
    CharLowerBuffW(lower.data(), static_cast<DWORD>(lower.size()));
    return lower.size() > 2 ? lower.substr(1, lower.size() - 2) : lower;
}

}  // namespace

std::wstring SoundDeviceKey(const std::wstring& endpointId) {
    std::wstring instance = L"SWD\\MMDEVAPI\\" + endpointId;
    DEVINST endpoint = 0;
    if (endpointId.empty() || CM_Locate_DevNodeW(&endpoint, instance.data(), CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS) {
        return {};
    }
    // A USB device by its IDs: the endpoint's parent is the device, or one interface of it ("&mi_xx").
    DEVINST parent = 0;
    if (CM_Get_Parent(&parent, endpoint, 0) == CR_SUCCESS) {
        std::wstring id = LowerInstanceId(parent);
        if (id.rfind(L"usb\\", 0) == 0 && id.find(L"&mi_") != std::wstring::npos &&
            CM_Get_Parent(&parent, parent, 0) == CR_SUCCESS) {
            id = LowerInstanceId(parent);
        }
        // usb\vid_xxxx&pid_xxxx\<serial number, or an ID from the port that holds '&'>
        constexpr size_t kVid = 8;
        constexpr size_t kPid = 17;
        constexpr size_t kInstance = 22;
        if (id.rfind(L"usb\\vid_", 0) == 0 && id.size() > kInstance && id.compare(kPid - 5, 5, L"&pid_") == 0) {
            std::wstring key = L"usb-" + id.substr(kVid, 4) + L"-" + id.substr(kPid, 4);
            const std::wstring serial = id.substr(kInstance);
            if (serial.find(L'&') == std::wstring::npos) {
                key += L"-" + serial;
            }
            return key;
        }
    }
    // Other devices outside the computer, such as Bluetooth headphones, keep their container.
    GUID container{};
    DEVPROPTYPE type = DEVPROP_TYPE_EMPTY;
    ULONG size = sizeof(container);
    if (CM_Get_DevNode_PropertyW(endpoint, &kDeviceContainerIdKey, &type, reinterpret_cast<PBYTE>(&container), &size, 0) ==
            CR_SUCCESS &&
        type == DEVPROP_TYPE_GUID && !IsEqualGUID(container, kComputerContainer) && !IsEqualGUID(container, GUID{})) {
        return L"dev-" + GuidText(container);
    }
    // "{0.0.0.00000000}.{guid}": the endpoint itself.
    const size_t dot = endpointId.rfind(L"}.{");
    std::wstring own = dot == std::wstring::npos ? endpointId : endpointId.substr(dot + 3);
    if (!own.empty() && own.back() == L'}') {
        own.pop_back();
    }
    CharLowerBuffW(own.data(), static_cast<DWORD>(own.size()));
    return L"ep-" + own;
}

uint16_t SteelSeriesProductOf(const std::wstring& key) {
    // "usb-1038-xxxx", optionally followed by "-<serial>".
    constexpr std::wstring_view kPrefix = L"usb-1038-";
    constexpr size_t kDigits = 4;
    if (key.size() < kPrefix.size() + kDigits || key.compare(0, kPrefix.size(), kPrefix) != 0 ||
        (key.size() > kPrefix.size() + kDigits && key[kPrefix.size() + kDigits] != L'-')) {
        return 0;
    }
    return static_cast<uint16_t>(wcstoul(key.substr(kPrefix.size(), kDigits).c_str(), nullptr, 16));
}

namespace {

std::vector<AudioEndpoint> ListEndpoints(EDataFlow flow, bool includeDisabled) {
    std::vector<AudioEndpoint> endpoints;
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDeviceCollection> devices;
    UINT count = 0;
    const DWORD states = DEVICE_STATE_ACTIVE | (includeDisabled ? DEVICE_STATE_DISABLED : 0);
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))) ||
        FAILED(enumerator->EnumAudioEndpoints(flow, states, &devices)) || FAILED(devices->GetCount(&count))) {
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

std::wstring DefaultEndpoint(EDataFlow flow, ERole role) {
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice> device;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))) ||
        FAILED(enumerator->GetDefaultAudioEndpoint(flow, role, &device))) {
        return {};
    }
    return EndpointIdOf(device.Get());
}

// Called on audio service threads; only posts to the window.
class VolumeNotifier final : public IAudioEndpointVolumeCallback {
public:
    VolumeNotifier(HWND window, UINT message) : window_(window), message_(message) {}

    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG refs = InterlockedDecrement(&refs_);
        if (refs == 0) {
            delete this;
        }
        return refs;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IAudioEndpointVolumeCallback)) {
            *out = static_cast<IAudioEndpointVolumeCallback*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA) override {
        PostMessageW(window_, message_, 0, 0);
        return S_OK;
    }

private:
    LONG refs_ = 1;
    HWND window_;
    UINT message_;
};

}  // namespace

std::vector<AudioEndpoint> ListRenderEndpoints(bool includeDisabled) {
    return ListEndpoints(eRender, includeDisabled);
}

std::vector<AudioEndpoint> ListCaptureEndpoints() {
    return ListEndpoints(eCapture, false);
}

std::wstring DefaultCaptureEndpoint(ERole role) {
    return DefaultEndpoint(eCapture, role);
}

IAudioEndpointVolumeCallback* CreateVolumeNotifier(HWND window, UINT message) {
    return new VolumeNotifier(window, message);
}

std::optional<AudioEndpoint> FindMicCable(const std::vector<AudioEndpoint>& endpoints) {
    for (const auto& endpoint : endpoints) {
        if (endpoint.name.rfind(L"CABLE Input", 0) == 0) {
            return endpoint;
        }
    }
    return std::nullopt;
}

namespace {

// The device interface of an endpoint's adapter without the filter's reference string, e.g.
// "{2}.\\?\root#media#0001#{6994ad04-...}": the render and capture filters of one cable driver differ only after it.
std::wstring AdapterInterfaceOf(IMMDeviceEnumerator* enumerator, const std::wstring& endpointId) {
    ComPtr<IMMDevice> device;
    if (FAILED(enumerator->GetDevice(endpointId.c_str(), &device))) {
        return {};
    }
    const std::wstring adapter = EndpointAdapterId(device.Get());
    const size_t slash = adapter.find_last_of(L'\\');
    if (slash == std::wstring::npos || slash == 0 || adapter[slash - 1] != L'}') {
        return adapter;
    }
    return adapter.substr(0, slash);
}

}  // namespace

std::wstring PairedCaptureEndpoint(const AudioEndpoint& render, const std::vector<AudioEndpoint>& captures) {
    std::wstring wanted = render.name;
    if (const size_t at = wanted.find(L" Input"); at != std::wstring::npos) {
        wanted.replace(at, 6, L" Output");
    } else if (const size_t in = wanted.find(L" In "); in != std::wstring::npos) {
        wanted.replace(in, 4, L" Out ");
    }
    for (const auto& capture : captures) {
        if (capture.name == wanted) {
            return capture.id;
        }
    }
    // An end renamed in the Windows sound settings no longer pairs by name; then the other end is the one
    // recording endpoint of the same driver device. Drivers with several cables (Virtual Audio Cable lines) give
    // no answer this way.
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)))) {
        return {};
    }
    const std::wstring adapter = AdapterInterfaceOf(enumerator.Get(), render.id);
    if (adapter.empty()) {
        return {};
    }
    std::wstring found;
    for (const auto& capture : captures) {
        if (AdapterInterfaceOf(enumerator.Get(), capture.id) == adapter) {
            if (!found.empty()) {
                return {};
            }
            found = capture.id;
        }
    }
    return found;
}

std::wstring DefaultRenderEndpoint(ERole role) {
    return DefaultEndpoint(eRender, role);
}

bool SetDefaultRenderEndpoint(const std::wstring& id, ERole role) {
    const ComPtr<IPolicyConfig> config = PolicyConfig();
    return config && !id.empty() && SUCCEEDED(config->SetDefaultEndpoint(id.c_str(), role));
}

// The endpoint ID tells the data flow, so the same call serves recording devices.
bool SetDefaultCaptureEndpoint(const std::wstring& id, ERole role) {
    return SetDefaultRenderEndpoint(id, role);
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

bool IsVirtualEndpoint(const AudioEndpoint& endpoint) {
    constexpr std::wstring_view kMarks[] = {L"CABLE", L"VB-Audio", L"Virtual Audio Cable", L"Voicemeeter", L"VoiceMeeter"};
    return IsSonarEndpoint(endpoint) || std::any_of(std::begin(kMarks), std::end(kMarks), [&](std::wstring_view mark) {
               return endpoint.name.find(mark) != std::wstring::npos;
           });
}
