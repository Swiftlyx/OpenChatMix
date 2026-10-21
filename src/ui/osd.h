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
    // A short notice in place of the balance, e.g. "Chat muted". A warning (low battery) uses the
    // error colors and stays longer.
    void ShowMessage(wchar_t glyph, const std::wstring& title, const std::wstring& detail, bool warning = false);
    // The headset volume in percent, after the knob of a GameDAC or base station turned.
    void ShowVolume(int percent);
    void Hide();
    void SetReduceMotion(bool reduce) { reduceMotion_ = reduce; }
    // Off: only warnings show. A popup on screen hides at once.
    void SetEnabled(bool enabled);
    // The corner of the primary monitor's work area; a popup on screen moves there with the next one.
    void SetPlacement(bool left, bool bottom);

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    bool CreateOsdWindow();
    void Destroy();
    void Begin();
    void Tick();
    void Render();
    void Present();

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
    bool volume_ = false;  // Shows volumePercent_ instead of the balance.
    int volumePercent_ = 0;
    wchar_t messageGlyph_ = 0;
    std::wstring messageTitle_;
    std::wstring messageDetail_;
    bool warning_ = false;
    bool reduceMotion_ = false;
    bool enabled_ = true;
    bool left_ = false;
    bool bottom_ = false;
    float position_ = 0.0f;  // Animated slider position: the balance in steps, or the volume in percent.
    float opacity_ = 0.0f;
    double lastShow_ = 0.0;  // NowMs() timestamps.
    double lastTick_ = 0.0;
};

}  // namespace ui
