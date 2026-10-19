#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// An effect plugin in the microphone chain: VST2 (.dll) or VST3 (.vst3), processed as mono.
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

    // Processes frames (at most the block size it was loaded with) in place.
    virtual void Process(float* mono, size_t frames) = 0;
};

// Loads a plugin and prepares it for sampleRate and blockSize. A .vst3 path may be the bundle folder
// or the binary. Returns null when the file is no usable effect plugin. Call on the plugin thread.
std::unique_ptr<AudioPlugin> LoadAudioPlugin(const std::wstring& path, double sampleRate, int blockSize);

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
