#pragma once

#include <windows.h>

#include <audiopolicy.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "app_info.h"
#include "balance.h"
#include "settings.h"

// Applies ChatMix levels as per-application session volume on the target render endpoints.
// Must be used from a COM MTA thread: session notifications are not delivered to STA threads.
class SessionMixer {
public:
    SessionMixer() = default;
    SessionMixer(const SessionMixer&) = delete;
    SessionMixer& operator=(const SessionMixer&) = delete;

    // Posts WM_APP_SESSIONS_CHANGED and WM_APP_ENDPOINTS_CHANGED to notifyWindow.
    bool Init(HWND notifyWindow);
    void Shutdown();

    // Targets the render endpoints of a SteelSeries product, or the default output when empty
    // or when the product exposes no endpoint.
    void SetTarget(std::optional<uint16_t> productId);
    // Further endpoints whose apps are listed and counted, e.g. the virtual Game and Chat devices.
    void SetExtraEndpoints(std::vector<std::wstring> ids);
    // Re-resolves endpoints after audio devices change.
    void Rebind();
    bool HasEndpoint() const { return !endpoints_.empty(); }

    void Apply(Levels levels, const Settings& settings);
    // Returns one app to full volume, e.g. after it moved to the Media group.
    void RestoreApp(const std::wstring& exe);
    // Returns every app changed by this mixer to full volume.
    void ResetVolumes();

    // Unique applications with live sessions on the target endpoints.
    std::vector<ProcessApp> ActiveApps() const;
    // Current peak sample (0-1) of everything played on the target endpoints.
    float PeakLevel() const;
    // Current peak of the apps in one group, after their volume.
    float GroupPeak(const Settings& settings, AppGroup group) const;

private:
    struct Endpoint {
        std::wstring id;
        std::wstring name;
        Microsoft::WRL::ComPtr<IAudioSessionManager2> manager;
        Microsoft::WRL::ComPtr<IAudioSessionNotification> notifier;
        Microsoft::WRL::ComPtr<IAudioMeterInformation> meter;
    };

    struct Session {
        std::wstring exe;
        std::wstring path;
        DWORD pid;
        ISimpleAudioVolume* volume;
        IAudioSessionControl* control;
    };
    template <class Fn>
    static void ForEachSession(const std::vector<Endpoint>& endpoints, Fn&& fn);
    std::vector<Endpoint> ResolveEndpoints();
    void Unregister(std::vector<Endpoint>& endpoints);

    HWND notify_ = nullptr;
    std::optional<uint16_t> target_;
    std::vector<std::wstring> extraIds_;
    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator_;
    Microsoft::WRL::ComPtr<IMMNotificationClient> endpointNotifier_;
    std::vector<Endpoint> endpoints_;
    std::set<std::wstring, NoCaseLess> touched_;  // Apps whose volume this mixer changed.
};
