#pragma once

#include <windows.h>

#include <string>

#include "../balance.h"
#include "canvas.h"
#include "frame_clock.h"

namespace ui {

// Click-through popup in the top-right corner of the primary monitor that shows the balance
// after a hotkey changes it. It fades out on its own and frees its resources once hidden.
class Osd {
public:
    explicit Osd(HINSTANCE instance);
    ~Osd();
    Osd(const Osd&) = delete;
    Osd& operator=(const Osd&) = delete;

    void Show(Levels levels);
    // A short notice in place of the balance, e.g. "Chat muted".
    void ShowMessage(wchar_t glyph, const std::wstring& title, const std::wstring& detail);
    void Hide();
    void SetReduceMotion(bool reduce) { reduceMotion_ = reduce; }

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    bool CreateOsdWindow();
    void Destroy();
    void Begin();
    void Tick();
    void Render();

    HINSTANCE instance_;
    HWND window_ = nullptr;
    Canvas canvas_;
    HDC memoryDc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ previousBitmap_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    POINT origin_{};
    float dpi_ = 96.0f;
    FrameClock frames_;  // Runs while the popup fades or the slider moves; idle during the hold.

    Levels levels_;
    bool message_ = false;
    wchar_t messageGlyph_ = 0;
    std::wstring messageTitle_;
    std::wstring messageDetail_;
    bool reduceMotion_ = false;
    float position_ = 0.0f;  // Animated slider position in steps.
    float opacity_ = 0.0f;
    double lastShow_ = 0.0;  // NowMs() timestamps.
    double lastTick_ = 0.0;
};

}  // namespace ui
