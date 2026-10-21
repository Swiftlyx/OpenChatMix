#pragma once

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Settings of the DeepFilterNet plugin. It has no window of its own, so the panel shows them.
struct DeepFilterSettings {
    static constexpr int kUnlimited = 100;
    int maxReductionDb = kUnlimited;  // How far noise is lowered at most; kUnlimited removes all it can.
    bool postFilter = false;          // Lowers what is left of the noise between words a little more.

    bool operator==(const DeepFilterSettings&) const = default;
};

// An effect plugin: VST2 (.dll), VST3 (.vst3) or OpenChatMix's DeepFilterNet DLL. The microphone chain runs
// plugins as mono, the virtual device channels as stereo.
// Everything but Process runs on the plugin thread (PluginThread); Process runs on the audio thread.
class AudioPlugin {
public:
    virtual ~AudioPlugin() = default;

    virtual std::wstring Name() const = 0;
    virtual bool HasEditor() const = 0;
    // Shows the plugin's own window, or brings it to the front when it is open.
    virtual void OpenEditor() = 0;
    virtual void CloseEditor() = 0;
    // The plugin's settings as an opaque blob, for the next start.
    virtual std::vector<uint8_t> SaveState() = 0;
    virtual void LoadState(const std::vector<uint8_t>& state) = 0;

    // Processes frames (at most the block size it was loaded with) of interleaved samples in place. The plugin
    // takes the first two channels; further channels pass unchanged.
    virtual void Process(float* samples, size_t frames, int channels) = 0;

    // Samples the plugin delays the sound by, beyond the block it is handed.
    virtual int LatencySamples() const { return 0; }
    // Samples the output keeps sounding after the input fell silent (reverb), beyond the latency.
    virtual int TailSamples() const { return 0; }
    // The DeepFilterNet plugin's settings; nothing for other plugins.
    virtual std::optional<DeepFilterSettings> DeepFilter() const { return std::nullopt; }
    virtual void SetDeepFilter(const DeepFilterSettings& settings) { (void)settings; }
};

// Loads a plugin and prepares it for sampleRate, blocks of up to blockSize frames and the layout the caller
// processes: 1 for mono, 2 for stereo. A .vst3 path may be the bundle folder or the binary. Returns null when the
// file is no usable effect plugin. Call on the plugin thread.
std::unique_ptr<AudioPlugin> LoadAudioPlugin(const std::wstring& path, double sampleRate, int blockSize,
                                             int channels);

using PluginList = std::vector<AudioPlugin*>;

// Hands the plugins to run from their owner to an audio thread. Set() returns once the audio thread no longer
// uses the previous list, so plugins left out of the new one may then be destroyed.
class PluginHandoff {
public:
    // The audio thread is about to start and holds no list yet.
    void Attach() { seen_ = generation_.load(); }
    // The audio thread ended; Set() no longer waits for it.
    void Detach() { seen_ = UINT64_MAX; }
    void Set(PluginList plugins);

    // Audio thread: the list for one pass, then Release() with the generation once the pass no longer uses it.
    std::shared_ptr<const PluginList> Acquire(uint64_t& generation) const {
        generation = generation_.load();
        return list_.load();
    }
    void Release(uint64_t generation) { seen_ = generation; }

private:
    std::atomic<std::shared_ptr<const PluginList>> list_{std::make_shared<const PluginList>()};
    std::atomic<uint64_t> generation_{0};
    std::atomic<uint64_t> seen_{UINT64_MAX};  // Generation the audio thread finished a pass with.
};

// Whether a plugin module was loaded in this process. Modules stay loaded until the process ends: some plugins
// (Supertone Clear) crash in their own unload code.
bool PluginModulesLoaded();

// The thread that owns all plugins and their editor windows. Plugin user interfaces expect a
// single-threaded COM apartment and a message loop, which the main thread (MTA) does not offer.
class PluginThread {
public:
    PluginThread() = default;
    ~PluginThread();
    PluginThread(const PluginThread&) = delete;
    PluginThread& operator=(const PluginThread&) = delete;

    bool Start();
    void Stop();
    // Runs work on the plugin thread and waits for it.
    void Run(const std::function<void()>& work);

private:
    static DWORD WINAPI ThreadProc(void* self);
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    HANDLE thread_ = nullptr;
    HANDLE ready_ = nullptr;
    HWND window_ = nullptr;
};
