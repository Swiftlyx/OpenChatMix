#pragma once

#include <windows.h>

#include <endpointvolume.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "audio_bridge.h"
#include "mic_dsp.h"
#include "plugin_host.h"

// Takes the microphone, runs it through the built-in effects and the plugins, and plays the result
// into a virtual cable, whose recording side apps use as their microphone. Runs on its own MMCSS
// "Pro Audio" thread at MicProcessor's rate, block by block; Windows converts the devices' formats.
class MicChain {
public:
    MicChain() = default;
    MicChain(const MicChain&) = delete;
    MicChain& operator=(const MicChain&) = delete;
    ~MicChain();

    // An empty inputId takes the default communications microphone. A monitorId also plays the result
    // there, to listen to it; empty for none. Posts WM_APP_MIC_STOPPED to notifyWindow if the streams
    // stop on their own later.
    bool Start(const std::wstring& inputId, const std::wstring& outputId, const std::wstring& monitorId,
               HWND notifyWindow);
    void Stop();
    bool IsRunning() const { return running_; }
    BridgeError Error() const { return error_; }
    void SetSettings(MicDspSettings settings);
    // The plugins to run, in order; the caller keeps them alive. Returns once the audio thread no
    // longer uses the previous list, so plugins left out of it may be destroyed.
    void SetPlugins(PluginList plugins) { plugins_.Set(std::move(plugins)); }
    int LatencyMs() const { return latencyMs_; }
    // Largest output sample since the last call.
    float TakePeak() { return peak_.exchange(0.0f); }

private:
    static DWORD WINAPI ThreadProc(void* self);
    void Run();
    void Fail(BridgeError error);

    std::wstring input_;
    std::wstring output_;
    std::wstring monitor_;
    HWND notify_ = nullptr;
    HANDLE thread_ = nullptr;
    HANDLE stopEvent_ = nullptr;
    HANDLE readyEvent_ = nullptr;
    std::atomic<std::shared_ptr<const MicDspSettings>> settings_{std::make_shared<const MicDspSettings>()};
    PluginHandoff plugins_;
    std::atomic<float> peak_{0.0f};
    std::atomic<bool> running_{false};
    std::atomic<BridgeError> error_{BridgeError::None};
    std::atomic<int> latencyMs_{0};
};

// Holds the volume of the given recording and playback devices at 100%: Windows and communication
// apps lower microphone levels on their own. Mute stays the user's.
class LevelKeeper {
public:
    LevelKeeper() = default;
    LevelKeeper(const LevelKeeper&) = delete;
    LevelKeeper& operator=(const LevelKeeper&) = delete;
    ~LevelKeeper();

    // Posts message to window when a level changes; the owner then calls Enforce().
    void Start(const std::vector<std::wstring>& endpointIds, HWND window, UINT message);
    void Stop();
    void Enforce();

private:
    struct Watched {
        Microsoft::WRL::ComPtr<IAudioEndpointVolume> control;
        Microsoft::WRL::ComPtr<IAudioEndpointVolumeCallback> callback;
    };
    std::vector<Watched> watched_;
};
