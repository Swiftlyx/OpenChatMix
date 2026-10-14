#include "app_routing.h"

#include <inspectable.h>
#include <mmdeviceapi.h>
#include <roapi.h>
#include <winstring.h>

#include <iterator>

#include "log.h"

// Slot order of IAudioPolicyConfigFactory after IInspectable; only the last three methods are called.
// Parameters of the other methods do not matter for the layout.
struct AppRouting::IFactory : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE AddCtxVolumeChange(void*, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE RemoveCtxVolumeChanged(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE AddRingerVibrateStateChanged(void*, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE RemoveRingerVibrateStateChanged(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetVolumeGroupGainForId(void*, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetVolumeGroupGainForId(void*, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetActiveVolumeGroupForEndpointId(void*, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetVolumeGroupsForEndpoint(void*, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentVolumeContext(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetVolumeGroupMuteForId(void*, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetVolumeGroupMuteForId(void*, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetRingerVibrateState(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetRingerVibrateState(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPreferredChatApplication(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetPreferredChatApplication() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPreferredChatApplication(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentChatApplications(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE AddChatContextChanged(void*, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE RemoveChatContextChanged(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPersistedDefaultAudioEndpoint(UINT processId, EDataFlow flow, ERole role,
                                                                       HSTRING deviceId) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPersistedDefaultAudioEndpoint(UINT processId, EDataFlow flow, int role,
                                                                       HSTRING* deviceId) = 0;
    virtual HRESULT STDMETHODCALLTYPE ClearAllPersistedApplicationDefaultEndpoints() = 0;
};

namespace {

// The interface ID changed with Windows 10 build 21390.
constexpr IID kFactoryIdModern = {0xab3d4648, 0xe242, 0x459f, {0xb0, 0x2f, 0x54, 0x1c, 0x70, 0x30, 0x63, 0x24}};
constexpr IID kFactoryIdLegacy = {0x2a59116d, 0x6c4f, 0x45e0, {0xa7, 0x4f, 0x70, 0x7e, 0x3f, 0xef, 0x92, 0x58}};
constexpr DWORD kModernBuild = 21390;
// Device interface path around an MMDevice endpoint ID.
constexpr wchar_t kDevicePrefix[] = L"\\\\?\\SWD#MMDEVAPI#";
constexpr wchar_t kRenderSuffix[] = L"#{e6327cad-dcec-4949-ae8a-991e976a79d2}";

DWORD WindowsBuild() {
    // RtlGetVersion is not subject to the application compatibility version lie.
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
        if (auto get = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"))) {
            get(&version);
        }
    }
    return version.dwBuildNumber;
}

}  // namespace

bool AppRouting::Init() {
    if (factory_) {
        return true;
    }
    constexpr wchar_t kClass[] = L"Windows.Media.Internal.AudioPolicyConfig";
    HSTRING name = nullptr;
    if (FAILED(WindowsCreateString(kClass, static_cast<UINT32>(std::size(kClass) - 1), &name)) || !name) {
        return false;
    }
    const IID& iid = WindowsBuild() >= kModernBuild ? kFactoryIdModern : kFactoryIdLegacy;
    const HRESULT hr = RoGetActivationFactory(name, iid, reinterpret_cast<void**>(&factory_));
    WindowsDeleteString(name);
    if (FAILED(hr)) {
        factory_ = nullptr;
        Log("app routing unavailable, hr 0x%08lx", static_cast<unsigned long>(hr));
    }
    return factory_ != nullptr;
}

AppRouting::~AppRouting() {
    if (factory_) {
        factory_->Release();
    }
}

bool AppRouting::Route(DWORD processId, const std::wstring& endpointId) {
    if (!factory_ || processId == 0) {
        return false;
    }
    HSTRING device = nullptr;
    if (!endpointId.empty()) {
        const std::wstring path = kDevicePrefix + endpointId + kRenderSuffix;
        if (FAILED(WindowsCreateString(path.c_str(), static_cast<UINT32>(path.size()), &device))) {
            return false;
        }
    }
    // Both roles, as the Windows volume mixer does.
    const HRESULT console = factory_->SetPersistedDefaultAudioEndpoint(processId, eRender, eConsole, device);
    const HRESULT multimedia = factory_->SetPersistedDefaultAudioEndpoint(processId, eRender, eMultimedia, device);
    if (device) {
        WindowsDeleteString(device);
    }
    return SUCCEEDED(console) && SUCCEEDED(multimedia);
}
