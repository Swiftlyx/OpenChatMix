#pragma once

#include <windows.h>

#include <endpointvolume.h>
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

// How a playback device reaches the computer.
enum class EndpointLink { Unknown, Usb, Bluetooth, Jack, Hdmi, Spdif };
// From the adapter's bus, and for a sound card from the endpoint's form factor and jack. Unknown for
// built-in speakers, virtual devices and adapters that describe no jack. Requires COM on the calling thread.
EndpointLink EndpointLinkOf(const std::wstring& endpointId);
// Battery of a Bluetooth playback device in percent, as Windows keeps it for the device; nothing for other
// devices or while the device reports none. Requires COM on the calling thread.
std::optional<int> BluetoothBatteryOf(const std::wstring& endpointId);

// Active playback devices, and disabled ones on request. Requires COM on the calling thread.
std::vector<AudioEndpoint> ListRenderEndpoints(bool includeDisabled = false);
// Active recording devices.
std::vector<AudioEndpoint> ListCaptureEndpoints();

// ID of the Windows default playback device for a role; empty if there is none.
std::wstring DefaultRenderEndpoint(ERole role);
std::wstring DefaultCaptureEndpoint(ERole role);
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

// Virtual devices nobody listens to directly: VB-Audio and Virtual Audio Cable lines, Voicemeeter and Sonar.
bool IsVirtualEndpoint(const AudioEndpoint& endpoint);

// The playback side of the VB-Audio "CABLE" that is left for a microphone chain, if installed.
std::optional<AudioEndpoint> FindMicCable(const std::vector<AudioEndpoint>& endpoints);
// The recording side of a virtual cable: "CABLE-A Output" for "CABLE-A Input", or the line of the same
// name (Virtual Audio Cable). Empty if there is none.
std::wstring PairedCaptureEndpoint(const AudioEndpoint& render, const std::vector<AudioEndpoint>& captures);

// Posts message to window whenever the volume or mute of an endpoint changes; for
// IAudioEndpointVolume::RegisterControlChangeNotify. The caller owns the one reference.
IAudioEndpointVolumeCallback* CreateVolumeNotifier(HWND window, UINT message);
