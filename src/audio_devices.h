#pragma once

#include <windows.h>

#include <mmdeviceapi.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct AudioEndpoint {
    std::wstring id;
    std::wstring name;
    std::optional<uint16_t> steelSeriesProductId;  // Set for endpoints of a SteelSeries USB device.
    bool active = true;                            // False while the device is disabled in Windows.
};

std::wstring EndpointFriendlyName(IMMDevice* device);
std::wstring EndpointIdOf(IMMDevice* device);
// Device ID of the adapter behind an endpoint, e.g. "{2}.\\?\usb#vid_1038&pid_12cb&mi_00#...".
std::wstring EndpointAdapterId(IMMDevice* device);

// Active playback devices, and disabled ones on request. Requires COM on the calling thread.
std::vector<AudioEndpoint> ListRenderEndpoints(bool includeDisabled = false);

// ID of the Windows default playback device for a role; empty if there is none.
std::wstring DefaultRenderEndpoint(ERole role);
// Both go through the undocumented IPolicyConfig interface of the Windows sound settings and
// need no elevation. They return false when Windows rejects the change.
bool SetDefaultRenderEndpoint(const std::wstring& id, ERole role);
bool SetEndpointEnabled(const std::wstring& id, bool enabled);

struct VirtualDevicePair {
    AudioEndpoint game;
    AudioEndpoint chat;
};

// The first two virtual cables that suit the Game and Chat roles: VB-Audio Virtual Cables A-D, then
// VB-Audio Hi-Fi Cable and Virtual Audio Cable lines (both have free editions). The plain VB-Audio
// "CABLE Input" and its "CABLE In 16ch" are never chosen: they typically carry a microphone chain.
std::optional<VirtualDevicePair> FindVirtualDevices(const std::vector<AudioEndpoint>& endpoints);

// SteelSeries Sonar devices (installed with SteelSeries GG). Even with GG closed, Sonar's processing in
// the Windows audio engine forwards whatever plays on them to the headset, ahead of the device volume,
// so they cannot serve as Game or Chat devices: bridging them plays everything twice.
bool IsSonarEndpoint(const AudioEndpoint& endpoint);
