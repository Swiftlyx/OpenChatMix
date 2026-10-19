#pragma once

#include <windows.h>

#include <audiopolicy.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "app_info.h"
#include "balance.h"
#include "settings.h"

// Applies ChatMix levels to the apps on the target render endpoints through the per-channel volume of
// their audio sessions. Windows multiplies it with the session volume, which the volume mixer shows
// and which stays the user's own. Windows also persists the channel volume per app, so a session keeps
// its ChatMix level only while it lives: the level is reset once the app exits.
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
    // The headset's own playback device when it has one, else the default output; empty when there is none.
    std::wstring OutputEndpointId() const { return endpoints_.empty() ? std::wstring{} : endpoints_.front().id; }

    // Posts WM_APP_SESSIONS_CHANGED when a session it changed expires.
    // media: the level of the Media group, which follows no balance: 1, or 0 while it is muted.
    void Apply(Levels levels, const Settings& settings, float media = 1.0f);
    // Returns one app to full volume, e.g. after it moved to the Media group.
    void RestoreApp(const std::wstring& exe);
    // Returns every app changed by this mixer to full volume.
    void ResetVolumes();
    // Resets the sessions of apps that exited, before Windows keeps their level for the next start.
    void ReleaseExpired();

    // Unique applications with live sessions on the target endpoints.
    std::vector<ProcessApp> ActiveApps() const;
    // Current peak sample (0-1) of everything played on the target endpoints.
    float PeakLevel() const;
    // Current peak of the apps in one group as they play; heard: after their own volume and ChatMix
    // level, as it reaches the device.
    float GroupPeak(const Settings& settings, AppGroup group, bool heard = false) const;
    // Current peak of the apps whose executable matches, before their volume.
    float AppPeak(bool (*match)(const std::wstring& exe)) const;

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
        IChannelAudioVolume* channels;
        IAudioSessionControl* control;
        IAudioSessionControl2* control2;
        const std::wstring* endpointId;
    };

    // A session whose channel volume this mixer changed. The reference keeps an expired session
    // reachable, so its level can still be reset after the app exited.
    struct Changed {
        std::wstring exe;
        std::wstring endpointId;
        Microsoft::WRL::ComPtr<IAudioSessionControl> control;
        Microsoft::WRL::ComPtr<IChannelAudioVolume> channels;
        Microsoft::WRL::ComPtr<IAudioSessionEvents> events;
    };

    template <class Fn>
    static void ForEachSession(const std::vector<Endpoint>& endpoints, Fn&& fn);
    std::vector<Endpoint> ResolveEndpoints();
    void Unregister(std::vector<Endpoint>& endpoints);
    void SetLevel(const Session& session, float level);
    // Resets the session to full volume and stops watching it.
    static void Release(Changed& changed);
    template <class Pred>
    void ReleaseWhere(Pred&& pred);

    HWND notify_ = nullptr;
    std::optional<uint16_t> target_;
    std::vector<std::wstring> extraIds_;
    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator_;
    Microsoft::WRL::ComPtr<IMMNotificationClient> endpointNotifier_;
    std::vector<Endpoint> endpoints_;
    std::map<std::wstring, Changed> changed_;  // Keyed by session instance identifier.
};
