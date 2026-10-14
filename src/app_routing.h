#pragma once

#include <windows.h>

#include <string>

// Per-app playback device, as set in Settings > System > Sound > Volume mixer. Goes through the
// undocumented IAudioPolicyConfigFactory of Windows 10 1803 and later. Windows keeps the choice for
// the app's executable, also across restarts, until it is cleared.
class AppRouting {
public:
    AppRouting() = default;
    ~AppRouting();
    AppRouting(const AppRouting&) = delete;
    AppRouting& operator=(const AppRouting&) = delete;

    // Needs COM on the calling thread.
    bool Init();
    bool Available() const { return factory_ != nullptr; }
    // Sends the app that owns processId to a render endpoint; an empty ID returns it to the default device.
    bool Route(DWORD processId, const std::wstring& endpointId);

private:
    struct IFactory;
    IFactory* factory_ = nullptr;
};
