#pragma once

#include <windows.h>

#include <audiopolicy.h>
#include <wrl/client.h>

#include <map>
#include <string>

// Tells whether apps other than this one have a running stream on a device: play on a playback device or record from
// a recording device, as their audio sessions' state shows. Opens no stream itself. Each device keeps its session
// manager with a notification registered, since Windows lists the sessions created later only then. Use from one COM
// thread.
class StreamWatch {
public:
    StreamWatch() = default;
    StreamWatch(const StreamWatch&) = delete;
    StreamWatch& operator=(const StreamWatch&) = delete;
    ~StreamWatch();

    // False as well for an empty ID or a device that is not present.
    bool InUse(const std::wstring& endpointId);
    // Lets go of every device, e.g. after devices came or went.
    void Clear();

private:
    struct Watched {
        Microsoft::WRL::ComPtr<IAudioSessionManager2> manager;
        Microsoft::WRL::ComPtr<IAudioSessionNotification> notification;
    };

    Watched* Open(const std::wstring& endpointId);

    std::map<std::wstring, Watched> devices_;
};
