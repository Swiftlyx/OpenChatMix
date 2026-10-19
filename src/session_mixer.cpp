#include "session_mixer.h"

#include <algorithm>
#include <cmath>

#include "app.h"
#include "audio_devices.h"
#include "devices.h"
#include "log.h"

using Microsoft::WRL::ComPtr;

namespace {

// Tags volume changes made by this app.
constexpr GUID kEventContext = {0x6f1d3c2a, 0x8b4e, 0x4f57, {0x9a, 0x61, 0x2c, 0x7e, 0x13, 0x5d, 0x80, 0x4b}};

constexpr float kLevelEpsilon = 0.001f;

// audiodg.exe hosts system audio effects; Sonar renders its already mixed output to the device.
// Scaling either would scale every app at once.
constexpr const wchar_t* kIgnoredApps[] = {L"audiodg.exe", L"SteelSeriesSonar.exe"};

bool IsIgnoredApp(const std::wstring& exe) {
    return std::any_of(std::begin(kIgnoredApps), std::end(kIgnoredApps),
                       [&](const wchar_t* app) { return _wcsicmp(app, exe.c_str()) == 0; });
}

// COM callbacks arrive on audio service worker threads; they only post to the UI thread.
template <class Interface>
class Notifier : public Interface {
public:
    explicit Notifier(HWND target) : target_(target) {}
    virtual ~Notifier() = default;

    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG refs = InterlockedDecrement(&refs_);
        if (refs == 0) {
            delete this;
        }
        return refs;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(Interface)) {
            *out = static_cast<Interface*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }

protected:
    HRESULT Post(UINT message) {
        PostMessageW(target_, message, 0, 0);
        return S_OK;
    }

private:
    LONG refs_ = 1;
    HWND target_;
};

class SessionNotifier final : public Notifier<IAudioSessionNotification> {
public:
    using Notifier::Notifier;
    HRESULT STDMETHODCALLTYPE OnSessionCreated(IAudioSessionControl*) override {
        return Post(WM_APP_SESSIONS_CHANGED);
    }
};

// Reports the end of a session whose level was changed, see SessionMixer::ReleaseExpired().
class SessionEndNotifier final : public Notifier<IAudioSessionEvents> {
public:
    using Notifier::Notifier;
    HRESULT STDMETHODCALLTYPE OnDisplayNameChanged(LPCWSTR, LPCGUID) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnIconPathChanged(LPCWSTR, LPCGUID) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnSimpleVolumeChanged(float, BOOL, LPCGUID) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnChannelVolumeChanged(DWORD, float[], DWORD, LPCGUID) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnGroupingParamChanged(LPCGUID, LPCGUID) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnStateChanged(AudioSessionState state) override {
        return state == AudioSessionStateExpired ? Post(WM_APP_SESSIONS_CHANGED) : S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnSessionDisconnected(AudioSessionDisconnectReason) override {
        return Post(WM_APP_SESSIONS_CHANGED);
    }
};

class EndpointNotifier final : public Notifier<IMMNotificationClient> {
public:
    using Notifier::Notifier;
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { return Post(WM_APP_ENDPOINTS_CHANGED); }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return Post(WM_APP_ENDPOINTS_CHANGED); }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return Post(WM_APP_ENDPOINTS_CHANGED); }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override {
        return flow == eRender && role == eConsole ? Post(WM_APP_ENDPOINTS_CHANGED) : S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }
};

std::wstring ProcessPath(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) {
        return {};
    }
    std::wstring path(MAX_PATH * 2, L'\0');
    DWORD length = static_cast<DWORD>(path.size());
    if (QueryFullProcessImageNameW(process, 0, path.data(), &length)) {
        path.resize(length);
    } else {
        path.clear();
    }
    CloseHandle(process);
    return path;
}

// Sets every channel of a session to level; a session that already has it is left alone.
void SetChannels(IChannelAudioVolume* channels, float level) {
    UINT32 count = 0;
    if (FAILED(channels->GetChannelCount(&count)) || count == 0) {
        return;
    }
    std::vector<float> values(count);
    if (SUCCEEDED(channels->GetAllVolumes(count, values.data())) &&
        std::all_of(values.begin(), values.end(), [&](float value) { return std::fabs(value - level) < kLevelEpsilon; })) {
        return;
    }
    std::fill(values.begin(), values.end(), level);
    channels->SetAllVolumes(count, values.data(), &kEventContext);
}

std::wstring InstanceId(IAudioSessionControl2* control) {
    LPWSTR id = nullptr;
    if (FAILED(control->GetSessionInstanceIdentifier(&id)) || !id) {
        return {};
    }
    std::wstring result(id);
    CoTaskMemFree(id);
    return result;
}

}  // namespace

bool SessionMixer::Init(HWND notifyWindow) {
    notify_ = notifyWindow;
    const HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator_));
    if (FAILED(hr)) {
        Log("MMDeviceEnumerator failed, hr 0x%08lx", static_cast<unsigned long>(hr));
        return false;
    }
    endpointNotifier_.Attach(new EndpointNotifier(notifyWindow));
    enumerator_->RegisterEndpointNotificationCallback(endpointNotifier_.Get());
    Rebind();
    return true;
}

void SessionMixer::Shutdown() {
    for (auto& [id, changed] : changed_) {
        if (changed.events) {
            changed.control->UnregisterAudioSessionNotification(changed.events.Get());
        }
    }
    changed_.clear();
    Unregister(endpoints_);
    endpoints_.clear();
    if (enumerator_ && endpointNotifier_) {
        enumerator_->UnregisterEndpointNotificationCallback(endpointNotifier_.Get());
    }
    endpointNotifier_.Reset();
    enumerator_.Reset();
}

void SessionMixer::SetTarget(std::optional<uint16_t> productId) {
    target_ = productId;
    Rebind();
}

void SessionMixer::SetExtraEndpoints(std::vector<std::wstring> ids) {
    if (ids == extraIds_) {
        return;
    }
    extraIds_ = std::move(ids);
    Rebind();
}

std::vector<SessionMixer::Endpoint> SessionMixer::ResolveEndpoints() {
    std::vector<Endpoint> result;
    if (!enumerator_) {
        return result;
    }

    auto add = [&](IMMDevice* device, const std::wstring& name) {
        Endpoint endpoint;
        endpoint.id = EndpointIdOf(device);
        endpoint.name = name;
        if (SUCCEEDED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, &endpoint.manager))) {
            device->Activate(__uuidof(IAudioMeterInformation), CLSCTX_ALL, nullptr, &endpoint.meter);
            result.push_back(std::move(endpoint));
        }
    };

    if (target_) {
        ComPtr<IMMDeviceCollection> devices;
        UINT count = 0;
        if (SUCCEEDED(enumerator_->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices))) {
            devices->GetCount(&count);
        }
        for (UINT i = 0; i < count; ++i) {
            ComPtr<IMMDevice> device;
            if (FAILED(devices->Item(i, &device))) {
                continue;
            }
            const auto productId = ParseSteelSeriesProductId(EndpointAdapterId(device.Get()));
            if (productId && *productId == *target_) {
                add(device.Get(), EndpointFriendlyName(device.Get()));
            }
        }
    }
    if (result.empty()) {
        ComPtr<IMMDevice> device;
        if (SUCCEEDED(enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &device))) {
            add(device.Get(), EndpointFriendlyName(device.Get()));
        }
    }
    for (const auto& id : extraIds_) {
        ComPtr<IMMDevice> device;
        const bool known = std::any_of(result.begin(), result.end(), [&](const Endpoint& endpoint) {
            return endpoint.id == id;
        });
        if (!id.empty() && !known && SUCCEEDED(enumerator_->GetDevice(id.c_str(), &device))) {
            DWORD state = 0;
            if (SUCCEEDED(device->GetState(&state)) && state == DEVICE_STATE_ACTIVE) {
                add(device.Get(), EndpointFriendlyName(device.Get()));
            }
        }
    }
    return result;
}

void SessionMixer::Rebind() {
    std::vector<Endpoint> next = ResolveEndpoints();

    // Endpoints that are no longer targeted keep no ChatMix attenuation.
    ReleaseWhere([&](const Changed& changed) {
        return std::none_of(next.begin(), next.end(),
                            [&](const Endpoint& candidate) { return candidate.id == changed.endpointId; });
    });
    Unregister(endpoints_);

    endpoints_ = std::move(next);
    for (auto& endpoint : endpoints_) {
        endpoint.notifier.Attach(new SessionNotifier(notify_));
        if (FAILED(endpoint.manager->RegisterSessionNotification(endpoint.notifier.Get()))) {
            endpoint.notifier.Reset();
        }
        // Session-created notifications start only after the first enumerator request.
        ComPtr<IAudioSessionEnumerator> sessions;
        endpoint.manager->GetSessionEnumerator(&sessions);
        Log("audio endpoint: %s", Utf8(endpoint.name).c_str());
    }
}

void SessionMixer::Unregister(std::vector<Endpoint>& endpoints) {
    for (auto& endpoint : endpoints) {
        if (endpoint.notifier) {
            endpoint.manager->UnregisterSessionNotification(endpoint.notifier.Get());
            endpoint.notifier.Reset();
        }
    }
}

template <class Fn>
void SessionMixer::ForEachSession(const std::vector<Endpoint>& endpoints, Fn&& fn) {
    for (const auto& endpoint : endpoints) {
        ComPtr<IAudioSessionEnumerator> sessions;
        if (FAILED(endpoint.manager->GetSessionEnumerator(&sessions))) {
            continue;
        }
        int count = 0;
        sessions->GetCount(&count);
        for (int i = 0; i < count; ++i) {
            ComPtr<IAudioSessionControl> control;
            ComPtr<IAudioSessionControl2> control2;
            ComPtr<IChannelAudioVolume> channels;
            AudioSessionState state = AudioSessionStateExpired;
            if (FAILED(sessions->GetSession(i, &control)) || FAILED(control->GetState(&state)) ||
                state == AudioSessionStateExpired || FAILED(control.As(&control2)) || FAILED(control.As(&channels))) {
                continue;
            }
            if (control2->IsSystemSoundsSession() == S_OK) {
                continue;
            }
            // AUDCLNT_S_NO_SINGLE_PROCESS is a success code; the initial process ID is still usable.
            // Own sessions belong to the virtual device bridges.
            DWORD pid = 0;
            if (FAILED(control2->GetProcessId(&pid)) || pid == 0 || pid == GetCurrentProcessId()) {
                continue;
            }
            const std::wstring path = ProcessPath(pid);
            const std::wstring exe = FileNameOf(path);
            if (!exe.empty() && !IsIgnoredApp(exe)) {
                fn(Session{exe, path, pid, channels.Get(), control.Get(), control2.Get(), &endpoint.id});
            }
        }
    }
}

void SessionMixer::Apply(Levels levels, const Settings& settings, float media) {
    const float game = levels.game / 100.0f;
    const float chat = levels.chat / 100.0f;
    ForEachSession(endpoints_, [&](const Session& session) {
        switch (settings.EffectiveGroupOf(session.exe)) {
        case AppGroup::Game:
            SetLevel(session, game);
            break;
        case AppGroup::Chat:
            SetLevel(session, chat);
            break;
        case AppGroup::Media:
            // Untouched unless muted; once lowered, a session is set back to full when unmuted.
            if (media < 1.0f || changed_.count(InstanceId(session.control2))) {
                SetLevel(session, media);
            }
            break;
        }
    });
}

void SessionMixer::SetLevel(const Session& session, float level) {
    SetChannels(session.channels, level);
    std::wstring id = InstanceId(session.control2);
    if (id.empty() || changed_.count(id)) {
        return;
    }
    Changed changed{session.exe, *session.endpointId, session.control, session.channels, nullptr};
    changed.events.Attach(new SessionEndNotifier(notify_));
    if (FAILED(session.control->RegisterAudioSessionNotification(changed.events.Get()))) {
        changed.events.Reset();
    }
    changed_.emplace(std::move(id), std::move(changed));
}

void SessionMixer::Release(Changed& changed) {
    SetChannels(changed.channels.Get(), 1.0f);
    if (changed.events) {
        changed.control->UnregisterAudioSessionNotification(changed.events.Get());
        changed.events.Reset();
    }
}

template <class Pred>
void SessionMixer::ReleaseWhere(Pred&& pred) {
    for (auto it = changed_.begin(); it != changed_.end();) {
        if (pred(it->second)) {
            Release(it->second);
            it = changed_.erase(it);
        } else {
            ++it;
        }
    }
}

void SessionMixer::RestoreApp(const std::wstring& exe) {
    ReleaseWhere([&](const Changed& changed) { return _wcsicmp(changed.exe.c_str(), exe.c_str()) == 0; });
    // Also covers a level left by an earlier run that did not end cleanly.
    ForEachSession(endpoints_, [&](const Session& session) {
        if (_wcsicmp(session.exe.c_str(), exe.c_str()) == 0) {
            SetChannels(session.channels, 1.0f);
        }
    });
}

void SessionMixer::ResetVolumes() {
    ReleaseWhere([](const Changed&) { return true; });
    Log("session volumes reset");
}

void SessionMixer::ReleaseExpired() {
    ReleaseWhere([](const Changed& changed) {
        AudioSessionState state = AudioSessionStateExpired;
        return FAILED(changed.control->GetState(&state)) || state == AudioSessionStateExpired;
    });
}

std::vector<ProcessApp> SessionMixer::ActiveApps() const {
    std::vector<ProcessApp> apps;
    ForEachSession(endpoints_, [&](const Session& session) {
        const bool known = std::any_of(apps.begin(), apps.end(), [&](const ProcessApp& app) {
            return _wcsicmp(app.exe.c_str(), session.exe.c_str()) == 0;
        });
        if (!known) {
            apps.push_back({session.exe, session.path, session.pid});
        }
    });
    return apps;
}

float SessionMixer::PeakLevel() const {
    float peak = 0.0f;
    for (const auto& endpoint : endpoints_) {
        float value = 0.0f;
        if (endpoint.meter && SUCCEEDED(endpoint.meter->GetPeakValue(&value))) {
            peak = std::max(peak, value);
        }
    }
    return peak;
}

float SessionMixer::AppPeak(bool (*match)(const std::wstring& exe)) const {
    float peak = 0.0f;
    ForEachSession(endpoints_, [&](const Session& session) {
        ComPtr<IAudioMeterInformation> meter;
        float value = 0.0f;
        if (match(session.exe) && SUCCEEDED(session.control->QueryInterface(IID_PPV_ARGS(&meter))) &&
            SUCCEEDED(meter->GetPeakValue(&value))) {
            peak = std::max(peak, value);
        }
    });
    return peak;
}

float SessionMixer::GroupPeak(const Settings& settings, AppGroup group, bool heard) const {
    float peak = 0.0f;
    ForEachSession(endpoints_, [&](const Session& session) {
        if (settings.EffectiveGroupOf(session.exe) != group) {
            return;
        }
        ComPtr<IAudioMeterInformation> meter;
        float value = 0.0f;
        if (FAILED(session.control->QueryInterface(IID_PPV_ARGS(&meter))) || FAILED(meter->GetPeakValue(&value))) {
            return;
        }
        // The session meter reads the app's sound before both its volume and its channel volume.
        if (heard) {
            ComPtr<ISimpleAudioVolume> volume;
            float master = 1.0f;
            if (SUCCEEDED(session.control->QueryInterface(IID_PPV_ARGS(&volume)))) {
                volume->GetMasterVolume(&master);
            }
            UINT32 count = 0;
            float channel = 1.0f;
            if (SUCCEEDED(session.channels->GetChannelCount(&count)) && count > 0) {
                std::vector<float> values(count);
                if (SUCCEEDED(session.channels->GetAllVolumes(count, values.data()))) {
                    channel = *std::max_element(values.begin(), values.end());
                }
            }
            value *= master * channel;
        }
        peak = std::max(peak, value);
    });
    return peak;
}
