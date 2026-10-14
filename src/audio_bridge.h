#pragma once

#include <windows.h>

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
    std::atomic<bool> running_{false};
    std::atomic<BridgeError> error_{BridgeError::None};
    std::atomic<int> latencyMs_{0};
};

// Two bridges that implement ChatMix with virtual devices: Game and Chat play into one output.
class AudioRouter {
public:
    bool Start(const std::wstring& gameId, const std::wstring& chatId, const std::wstring& outputId, HWND notifyWindow);
    void Stop();
    bool IsRunning() const { return game_.IsRunning() && chat_.IsRunning(); }
    BridgeError Error() const;
    void SetLevels(Levels levels);
    void SetEqualizers(std::shared_ptr<const ParametricEq> game, std::shared_ptr<const ParametricEq> chat);
    // Largest Chat output sample since the last call.
    float TakeChatPeak() { return chat_.TakePeak(); }
    int LatencyMs() const;

private:
    AudioBridge game_;
    AudioBridge chat_;
};
