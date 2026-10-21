#pragma once

#include <windows.h>

#include <memory>
#include <string>

#include "plugin_host.h"

// Top-level window that holds a plugin's editor. Lives on the plugin thread.
class EditorWindow {
public:
    class Client {
    public:
        // The user closed the window; the client detaches its editor and destroys the window.
        virtual void OnEditorClose() = 0;
        // About 30 times a second while the window is open (VST2 editors draw from it).
        virtual void OnEditorIdle() {}
        // The user resized the window; width and height are the new client size in pixels.
        virtual void OnEditorResized(int width, int height) { (void)width, (void)height; }

    protected:
        ~Client() = default;
    };

    EditorWindow() = default;
    ~EditorWindow();
    EditorWindow(const EditorWindow&) = delete;
    EditorWindow& operator=(const EditorWindow&) = delete;

    // dpiUnaware lets Windows scale editors that do not scale themselves, at the cost of sharpness.
    bool Create(const std::wstring& title, int width, int height, bool resizable, bool dpiUnaware, Client& client);
    void Destroy();
    HWND Handle() const { return window_; }
    bool IsOpen() const { return window_ != nullptr; }
    void BringToFront();
    // Resizes the window so its client area has this size.
    void SetClientSize(int width, int height);
    // Scale of the window's monitor relative to 96 DPI.
    float Scale() const;

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    HWND window_ = nullptr;
    Client* client_ = nullptr;
    bool resizing_ = false;  // SetClientSize is running; its WM_SIZE is not a user resize.
};

// Implemented in vst3_host.cpp.
std::unique_ptr<AudioPlugin> LoadVst3Plugin(const std::wstring& path, double sampleRate, int blockSize);

// Implemented in deep_filter_plugin.cpp. recognized tells whether the DLL is OpenChatMix's DeepFilterNet build,
// so a failure there is final and the DLL is not tried as a VST 2 plugin.
std::unique_ptr<AudioPlugin> LoadDeepFilterPlugin(const std::wstring& path, double sampleRate, int blockSize,
                                                  bool& recognized);
