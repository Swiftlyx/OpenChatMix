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

std::wstring FileName(const std::wstring& path) {
    return path.substr(path.find_last_of(L"\\/") + 1);
}

void SetLevel(ISimpleAudioVolume* volume, float level) {
    float current = 0.0f;
    if (SUCCEEDED(volume->GetMasterVolume(&current)) && std::fabs(current - level) < kLevelEpsilon) {
        return;
    }
    volume->SetMasterVolume(level, &kEventContext);
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
    std::vector<Endpoint> dropped;
    for (auto& endpoint : endpoints_) {
        const bool kept = std::any_of(next.begin(), next.end(),
                                      [&](const Endpoint& candidate) { return candidate.id == endpoint.id; });
        if (!kept) {
            dropped.push_back(endpoint);
        }
    }
    ForEachSession(dropped, [&](const Session& session) {
        if (touched_.count(session.exe)) {
            SetLevel(session.volume, 1.0f);
        }
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
            ComPtr<ISimpleAudioVolume> volume;
            AudioSessionState state = AudioSessionStateExpired;
            if (FAILED(sessions->GetSession(i, &control)) || FAILED(control->GetState(&state)) ||
                state == AudioSessionStateExpired || FAILED(control.As(&control2)) || FAILED(control.As(&volume))) {
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
                fn(Session{exe, path, pid, volume.Get(), control.Get()});
            }
        }
    }
}

void SessionMixer::Apply(Levels levels, const Settings& settings) {
    const float game = levels.game / 100.0f;
    const float chat = levels.chat / 100.0f;
    ForEachSession(endpoints_, [&](const Session& session) {
        switch (settings.EffectiveGroupOf(session.exe)) {
        case AppGroup::Game:
            SetLevel(session.volume, game);
            touched_.insert(session.exe);
            break;
        case AppGroup::Chat:
            SetLevel(session.volume, chat);
            touched_.insert(session.exe);
            break;
        case AppGroup::Media:
            break;
        }
    });
}

void SessionMixer::RestoreApp(const std::wstring& exe) {
    ForEachSession(endpoints_, [&](const Session& session) {
        if (_wcsicmp(session.exe.c_str(), exe.c_str()) == 0) {
            SetLevel(session.volume, 1.0f);
        }
    });
    touched_.erase(exe);
}

void SessionMixer::ResetVolumes() {
    ForEachSession(endpoints_, [&](const Session& session) {
        if (touched_.count(session.exe)) {
            SetLevel(session.volume, 1.0f);
        }
    });
    touched_.clear();
    Log("session volumes reset");
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

float SessionMixer::GroupPeak(const Settings& settings, AppGroup group) const {
    float peak = 0.0f;
    ForEachSession(endpoints_, [&](const Session& session) {
        if (settings.EffectiveGroupOf(session.exe) != group) {
            return;
        }
        ComPtr<IAudioMeterInformation> meter;
        float value = 0.0f;
        if (SUCCEEDED(session.control->QueryInterface(IID_PPV_ARGS(&meter))) && SUCCEEDED(meter->GetPeakValue(&value))) {
            peak = std::max(peak, value);
        }
    });
    return peak;
}
