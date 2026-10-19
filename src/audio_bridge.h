#pragma once

#include <windows.h>

#include <endpointvolume.h>
#include <wrl/client.h>

#include <atomic>
#include <memory>
#include <string>

#include "balance.h"
#include "parametric_eq.h"

enum class BridgeError {
    None,
    DeviceMissing,  // An endpoint ID no longer resolves to an active device.
    Unsupported,    // The engine format is not 32-bit float or a stream could not be initialized.
    Stopped,        // The stream failed while running, e.g. the device was unplugged.
};

// Plays everything rendered to one playback device (captured through WASAPI loopback) on another
// playback device with an adjustable volume. Runs on its own MMCSS "Pro Audio" thread.
class AudioBridge {
public:
    AudioBridge() = default;
    AudioBridge(const AudioBridge&) = delete;
    AudioBridge& operator=(const AudioBridge&) = delete;
    ~AudioBridge();

    // Returns once the streams are running or failed. Posts WM_APP_ROUTING to notifyWindow if
    // the stream stops on its own later.
    bool Start(const std::wstring& sourceId, const std::wstring& targetId, HWND notifyWindow);
    void Stop();
    bool IsRunning() const { return running_; }
    BridgeError Error() const { return error_; }
    void SetVolume(float volume) { volume_ = volume; }
    // Equalizer for the stream; null bypasses it. Takes effect within one engine period.
    void SetEqualizer(std::shared_ptr<const ParametricEq> eq) { eq_.store(std::move(eq)); }
    // Largest output sample since the last call, after the volume.
    float TakePeak() { return peak_.exchange(0.0f); }
    // The same for the level meters in the panel, kept apart so they don't take ducking's peaks.
    float TakeMeterPeak() { return meterPeak_.exchange(0.0f); }
    // Approximate source-to-output delay of the bridge itself.
    int LatencyMs() const { return latencyMs_; }

private:
    static DWORD WINAPI ThreadProc(void* self);
    void Run();
    void Fail(BridgeError error);

    std::wstring source_;
    std::wstring target_;
    HWND notify_ = nullptr;
    HANDLE thread_ = nullptr;
    HANDLE stopEvent_ = nullptr;
    HANDLE readyEvent_ = nullptr;
    std::atomic<float> volume_{1.0f};
    std::atomic<std::shared_ptr<const ParametricEq>> eq_;
    std::atomic<float> peak_{0.0f};
    std::atomic<float> meterPeak_{0.0f};
    std::atomic<bool> running_{false};
    std::atomic<BridgeError> error_{BridgeError::None};
    std::atomic<int> latencyMs_{0};
};

// The devices of virtual device mode: where Game, Chat and optionally Media come from and where each
// plays. Every output is resolved, never empty; media is empty when Media has no device of its own.
struct RoutingPlan {
    std::wstring game;
    std::wstring chat;
    std::wstring media;
    std::wstring gameOutput;
    std::wstring chatOutput;
    std::wstring mediaOutput;
};

// Bridges that implement ChatMix with virtual devices: Game and Chat, and Media when it has a device,
// each played into its output. Loopback capture takes the sound before the Windows volume of a
// device, so the router applies it: each device scales its own channel, and the Game device (the
// default output, so the taskbar slider and volume keys) also scales the channels that play on the
// same output as Game. ChatMix never changes Media. Captures of the devices by other apps keep the
// full level.
class AudioRouter {
public:
    // Posts WM_APP_DEVICE_VOLUME to notifyWindow when the volume of a routed device changes.
    bool Start(const RoutingPlan& plan, HWND notifyWindow);
    void Stop();
    bool IsRunning() const { return game_.IsRunning() && chat_.IsRunning() && (!mediaRouted_ || media_.IsRunning()); }
    BridgeError Error() const;
    void SetLevels(Levels levels);
    // The Media channel follows no balance; 0 mutes it.
    void SetMediaLevel(float level);
    // Reads the Windows volume and mute of the Game and Chat devices again.
    void UpdateDeviceVolumes();
    void SetEqualizers(std::shared_ptr<const ParametricEq> game, std::shared_ptr<const ParametricEq> chat,
                       std::shared_ptr<const ParametricEq> media);
    // Largest Chat output sample since the last call.
    float TakeChatPeak() { return chat_.TakePeak(); }
    // Largest Game and Chat output samples since the last call, for the level meters.
    float TakeGameMeter() { return game_.TakeMeterPeak(); }
    float TakeChatMeter() { return chat_.TakeMeterPeak(); }
    int LatencyMs() const;

private:
    struct DeviceVolume {
        Microsoft::WRL::ComPtr<IAudioEndpointVolume> control;
        Microsoft::WRL::ComPtr<IAudioEndpointVolumeCallback> callback;
        float gain = 1.0f;
    };

    static void WatchVolume(const std::wstring& id, HWND notifyWindow, DeviceVolume& volume);
    void ApplyVolumes();

    AudioBridge game_;
    AudioBridge chat_;
    AudioBridge media_;
    DeviceVolume gameVolume_;
    DeviceVolume chatVolume_;
    DeviceVolume mediaVolume_;
    bool mediaRouted_ = false;
    bool chatWithGame_ = true;   // Chat plays on the Game output, so the Game device's volume applies.
    bool mediaWithGame_ = true;
    Levels levels_;
    float mediaLevel_ = 1.0f;
};
