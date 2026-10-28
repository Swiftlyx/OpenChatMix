#include "stream_watch.h"

#include <mmdeviceapi.h>

using Microsoft::WRL::ComPtr;

namespace {

// Registered only so that the session enumerator includes sessions created after the manager was opened.
class SessionNotification final : public IAudioSessionNotification {
public:
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG refs = InterlockedDecrement(&refs_);
        if (refs == 0) {
            delete this;
        }
        return refs;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IAudioSessionNotification)) {
            *out = static_cast<IAudioSessionNotification*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE OnSessionCreated(IAudioSessionControl*) override { return S_OK; }

private:
    LONG refs_ = 1;
};

}  // namespace

StreamWatch::~StreamWatch() {
    Clear();
}

void StreamWatch::Clear() {
    for (auto& [id, watched] : devices_) {
        if (watched.manager && watched.notification) {
            watched.manager->UnregisterSessionNotification(watched.notification.Get());
        }
    }
    devices_.clear();
}

StreamWatch::Watched* StreamWatch::Open(const std::wstring& endpointId) {
    if (const auto found = devices_.find(endpointId); found != devices_.end()) {
        return &found->second;
    }
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice> device;
    Watched watched;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))) ||
        FAILED(enumerator->GetDevice(endpointId.c_str(), &device)) ||
        FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, &watched.manager))) {
        return nullptr;
    }
    watched.notification.Attach(new SessionNotification());
    if (FAILED(watched.manager->RegisterSessionNotification(watched.notification.Get()))) {
        watched.notification.Reset();
    }
    return &devices_.emplace(endpointId, std::move(watched)).first->second;
}

bool StreamWatch::InUse(const std::wstring& endpointId) {
    Watched* watched = endpointId.empty() ? nullptr : Open(endpointId);
    ComPtr<IAudioSessionEnumerator> sessions;
    int count = 0;
    if (!watched || FAILED(watched->manager->GetSessionEnumerator(&sessions)) || FAILED(sessions->GetCount(&count))) {
        return false;
    }
    const DWORD self = GetCurrentProcessId();
    for (int i = 0; i < count; ++i) {
        ComPtr<IAudioSessionControl> control;
        ComPtr<IAudioSessionControl2> control2;
        AudioSessionState state = AudioSessionStateInactive;
        DWORD process = 0;
        if (SUCCEEDED(sessions->GetSession(i, &control)) && SUCCEEDED(control->GetState(&state)) &&
            state == AudioSessionStateActive && SUCCEEDED(control.As(&control2)) &&
            SUCCEEDED(control2->GetProcessId(&process)) && process != self) {
            return true;
        }
    }
    return false;
}
