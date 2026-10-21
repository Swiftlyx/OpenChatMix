#include "osd.h"

#include <shellscalingapi.h>

#include <algorithm>
#include <cmath>
#include <string>

#include "../app.h"
#include "../i18n.h"
#include "theme.h"
#include "widgets.h"

namespace ui {
namespace {

constexpr wchar_t kWindowClass[] = L"OpenChatMix.Osd";
constexpr float kCardWidth = 340.0f;
constexpr float kCardHeight = 92.0f;
constexpr float kCardRadius = 24.0f;
constexpr float kShadow = 20.0f;  // Room around the card for the drawn shadow.
constexpr float kScreenMargin = 16.0f;
constexpr float kSlideDistance = 10.0f;
constexpr UINT_PTR kHoldTimer = 1;
constexpr float kFadeInMs = 120.0f;
constexpr float kFadeOutMs = 250.0f;
constexpr double kHoldMs = 1500.0;
constexpr double kWarningHoldMs = 5000.0;

constexpr wchar_t kGlyphGame = 0xE7FC;
constexpr wchar_t kGlyphChat = 0xE8BD;
constexpr wchar_t kGlyphVolume0 = 0xE992;  // Volume0..Volume3: a speaker with zero to three waves.

std::wstring Percent(int value) {
    return std::to_wstring(value) + L"%";
}

// Milliseconds from the performance counter; GetTickCount64 is too coarse for high refresh rates.
double NowMs() {
    static const double frequency = [] {
        LARGE_INTEGER value;
        QueryPerformanceFrequency(&value);
        return static_cast<double>(value.QuadPart);
    }();
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart) * 1000.0 / frequency;
}

float SmoothStep(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

}  // namespace

Osd::Osd(HINSTANCE instance) : instance_(instance) {}

Osd::~Osd() {
    Destroy();
}

void Osd::SetEnabled(bool enabled) {
    enabled_ = enabled;
    if (!enabled_ && !warning_) {
        Destroy();
    }
}

void Osd::SetPlacement(bool left, bool bottom) {
    if (left != left_ || bottom != bottom_) {
        left_ = left;
        bottom_ = bottom;
        Destroy();
    }
}

void Osd::Show(Levels levels) {
    if (!enabled_) {
        return;
    }
    if ((message_ || volume_) && window_) {
        position_ = static_cast<float>(BalanceFromLevels(levels));
    }
    message_ = false;
    volume_ = false;
    warning_ = false;
    levels_ = levels;
    Begin();
}

void Osd::ShowMessage(wchar_t glyph, const std::wstring& title, const std::wstring& detail, bool warning) {
    if (!enabled_ && !warning) {
        return;
    }
    message_ = true;
    volume_ = false;
    warning_ = warning;
    messageGlyph_ = glyph;
    messageTitle_ = title;
    messageDetail_ = detail;
    Begin();
}

void Osd::ShowVolume(int percent) {
    if (!enabled_) {
        return;
    }
    // The bar starts where the volume is, not where a balance shown before left it.
    if (!volume_ && window_) {
        position_ = static_cast<float>(percent);
    }
    message_ = false;
    volume_ = true;
    warning_ = false;
    volumePercent_ = std::clamp(percent, 0, 100);
    Begin();
}

void Osd::Begin() {
    lastShow_ = NowMs();
    if (!window_) {
        if (!CreateOsdWindow()) {
            return;
        }
        position_ = static_cast<float>(volume_ ? volumePercent_ : BalanceFromLevels(levels_));
        opacity_ = 0.0f;
        lastTick_ = lastShow_;
    }
    KillTimer(window_, kHoldTimer);
    frames_.Start(window_, WM_APP_FRAME);
    Tick();
}

void Osd::Hide() {
    Destroy();
}

bool Osd::CreateOsdWindow() {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = WindowProc;
        windowClass.hInstance = instance_;
        windowClass.lpszClassName = kWindowClass;
        RegisterClassExW(&windowClass);
        registered = true;
    }

    HMONITOR monitor = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(monitor, &info);
    UINT dpiX = 96;
    UINT dpiY = 96;
    GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    dpi_ = static_cast<float>(dpiX);
    const float scale = dpi_ / 96.0f;
    width_ = static_cast<int>(std::lround((kCardWidth + 2.0f * kShadow) * scale));
    height_ = static_cast<int>(std::lround((kCardHeight + 2.0f * kShadow) * scale));
    origin_.x = left_ ? info.rcWork.left + static_cast<LONG>(std::lround((kScreenMargin - kShadow) * scale))
                      : info.rcWork.right - static_cast<LONG>(std::lround((kScreenMargin + kCardWidth + kShadow) * scale));
    origin_.y = bottom_ ? info.rcWork.bottom - static_cast<LONG>(std::lround((kScreenMargin + kCardHeight + kShadow) * scale))
                        : info.rcWork.top + static_cast<LONG>(std::lround((kScreenMargin - kShadow) * scale));

    window_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                              kWindowClass, L"", WS_POPUP, origin_.x, origin_.y, width_, height_, nullptr, nullptr,
                              instance_, this);
    if (!window_) {
        return false;
    }

    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = width_;
    bitmapInfo.bmiHeader.biHeight = -height_;  // Top-down rows.
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;
    HDC screen = GetDC(nullptr);
    memoryDc_ = CreateCompatibleDC(screen);
    void* bits = nullptr;
    bitmap_ = CreateDIBSection(screen, &bitmapInfo, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!memoryDc_ || !bitmap_ || !canvas_.AttachDC(dpi_)) {
        Destroy();
        return false;
    }
    previousBitmap_ = SelectObject(memoryDc_, bitmap_);
    ShowWindow(window_, SW_SHOWNOACTIVATE);
    return true;
}

void Osd::Destroy() {
    if (HWND window = window_) {
        window_ = nullptr;
        frames_.Stop();
        KillTimer(window, kHoldTimer);
        DestroyWindow(window);
    }
    canvas_.Shutdown();
    if (memoryDc_) {
        if (previousBitmap_) {
            SelectObject(memoryDc_, previousBitmap_);
            previousBitmap_ = nullptr;
        }
        DeleteDC(memoryDc_);
        memoryDc_ = nullptr;
    }
    if (bitmap_) {
        DeleteObject(bitmap_);
        bitmap_ = nullptr;
    }
}

void Osd::Tick() {
    const double now = NowMs();
    const float elapsedMs = static_cast<float>(std::clamp(now - lastTick_, 0.0, 50.0));
    lastTick_ = now;
    const double holdMs = warning_ ? kWarningHoldMs : kHoldMs;
    const bool holding = now - lastShow_ < holdMs;
    opacity_ = holding ? std::min(1.0f, opacity_ + elapsedMs / kFadeInMs)
                       : std::max(0.0f, opacity_ - elapsedMs / kFadeOutMs);
    const float target = static_cast<float>(volume_ ? volumePercent_ : BalanceFromLevels(levels_));
    position_ += (target - position_) * (1.0f - std::exp(-18.0f * elapsedMs / 1000.0f));
    if (std::fabs(target - position_) < 0.02f || reduceMotion_) {
        position_ = target;
    }
    if (reduceMotion_) {
        opacity_ = holding ? 1.0f : 0.0f;
    }
    if (!holding && opacity_ <= 0.0f) {
        Destroy();
        return;
    }
    Render();
    if (holding && opacity_ >= 1.0f && position_ == target) {
        // Nothing moves until the fade-out starts.
        frames_.Stop();
        SetTimer(window_, kHoldTimer, static_cast<UINT>(std::max(1.0, holdMs - (now - lastShow_))), nullptr);
    }
}

void Osd::Render() {
    const Palette& p = ThemePalette();
    const RECT bounds{0, 0, width_, height_};
    if (!canvas_.BindDC(memoryDc_, bounds)) {
        return;
    }

    canvas_.Begin();
    canvas_.Clear({0.0f, 0.0f, 0.0f, 0.0f});
    const D2D1_RECT_F card{kShadow, kShadow, kShadow + kCardWidth, kShadow + kCardHeight};
    // Soft shadow from stacked translucent shapes, offset slightly downwards.
    for (int layer = 1; layer <= 6; ++layer) {
        const float grow = layer * 2.0f;
        canvas_.FillRound({card.left - grow, card.top - grow + 3.0f, card.right + grow, card.bottom + grow + 3.0f},
                          kCardRadius + grow, {0.0f, 0.0f, 0.0f, 0.035f});
    }
    canvas_.FillRound(card, kCardRadius, p.surfaceContainerHigh);
    canvas_.StrokeRound({card.left + 0.5f, card.top + 0.5f, card.right - 0.5f, card.bottom - 0.5f}, kCardRadius - 0.5f,
                        p.outlineVariant, 1.0f);

    const float left = card.left + 22.0f;
    const float right = card.right - 22.0f;
    if (message_) {
        canvas_.FillCircle({left + 22.0f, card.top + kCardHeight / 2.0f}, 22.0f,
                           warning_ ? Mix(p.surfaceContainerHigh, p.error, 0.18f) : p.secondaryContainer);
        canvas_.Glyph(messageGlyph_, {left + 22.0f, card.top + kCardHeight / 2.0f}, 18.0f,
                      warning_ ? p.error : p.onSecondaryContainer);
        canvas_.Text(messageTitle_, {left + 60.0f, card.top + 18.0f, right, card.top + 46.0f}, TextStyle::TitleMedium,
                     p.onSurface);
        canvas_.Text(messageDetail_, {left + 60.0f, card.top + 46.0f, right, card.top + 70.0f}, TextStyle::BodyMedium,
                     p.onSurfaceVariant);
        canvas_.End();
        Present();
        return;
    }
    const float rowTop = card.top + 14.0f;
    const float rowBottom = card.top + 40.0f;
    const float rowCenter = (rowTop + rowBottom) / 2.0f;
    const SliderStyle sliderStyle{12.0f, 5.0f, 32.0f};

    if (volume_) {
        const wchar_t glyph = static_cast<wchar_t>(kGlyphVolume0 + (volumePercent_ == 0 ? 0 : 1 + volumePercent_ / 34));
        canvas_.Glyph(glyph, {left + 9.0f, rowCenter}, 17.0f, p.primary);
        canvas_.Text(T(Text::HeadsetVolume), {left + 26.0f, rowTop, right - 64.0f, rowBottom}, TextStyle::LabelLarge,
                     p.onSurfaceVariant);
        canvas_.Text(Percent(volumePercent_), {right - 64.0f, rowTop, right, rowBottom}, TextStyle::TitleMedium,
                     p.primary, TextAlign::Trailing);
        DrawValueSlider(canvas_, p, left, right, card.top + 64.0f, position_ / 100.0f, 4.0f, sliderStyle, true);
        canvas_.End();
        Present();
        return;
    }

    const std::wstring gameLabel = T(Text::Game);
    const float gameLabelWidth = canvas_.Measure(gameLabel, TextStyle::LabelLarge);
    canvas_.Glyph(kGlyphGame, {left + 9.0f, rowCenter}, 17.0f, p.primary);
    canvas_.Text(gameLabel, {left + 26.0f, rowTop, left + 26.0f + gameLabelWidth + 1.0f, rowBottom},
                 TextStyle::LabelLarge, p.onSurfaceVariant);
    canvas_.Text(Percent(levels_.game), {left + 34.0f + gameLabelWidth, rowTop, left + 200.0f, rowBottom},
                 TextStyle::TitleMedium, p.primary);

    const std::wstring chatLabel = T(Text::Chat);
    const float chatLabelWidth = canvas_.Measure(chatLabel, TextStyle::LabelLarge);
    canvas_.Glyph(kGlyphChat, {right - 9.0f, rowCenter}, 17.0f, p.tertiary);
    canvas_.Text(chatLabel, {right - 27.0f - chatLabelWidth, rowTop, right - 26.0f, rowBottom}, TextStyle::LabelLarge,
                 p.onSurfaceVariant);
    canvas_.Text(Percent(levels_.chat), {right - 200.0f, rowTop, right - 34.0f - chatLabelWidth, rowBottom},
                 TextStyle::TitleMedium, p.tertiary, TextAlign::Trailing);

    if (BalanceFromLevels(levels_) == 0) {
        DrawBalancedChip(canvas_, p, {(left + right) / 2.0f, rowCenter}, T(Text::Balanced), 1.0f);
    }
    DrawBalanceSlider(canvas_, p, left, right, card.top + 64.0f, position_, 4.0f, sliderStyle);
    canvas_.End();
    Present();
}

// Puts the drawn card on screen with the current fade and slide.
void Osd::Present() {
    const float eased = SmoothStep(opacity_);
    // It slides in from the screen's edge: down at the top, up at the bottom.
    const LONG slide = static_cast<LONG>(std::lround((1.0f - eased) * kSlideDistance * dpi_ / 96.0f));
    POINT destination{origin_.x, bottom_ ? origin_.y + slide : origin_.y - slide};
    POINT source{0, 0};
    SIZE size{width_, height_};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, static_cast<BYTE>(std::lround(eased * 255.0f)), AC_SRC_ALPHA};
    UpdateLayeredWindow(window_, nullptr, &destination, &size, memoryDc_, &source, 0, &blend, ULW_ALPHA);
}

LRESULT CALLBACK Osd::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
    }
    auto* self = reinterpret_cast<Osd*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
    case WM_APP_FRAME:
        if (self) {
            self->frames_.FrameHandled();
            self->Tick();
        }
        return 0;
    case WM_TIMER:
        if (self && wParam == kHoldTimer) {
            KillTimer(window, kHoldTimer);
            self->lastTick_ = NowMs();
            self->frames_.Start(window, WM_APP_FRAME);
            self->Tick();
        }
        return 0;
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_CLOSE:
        // The popup hides itself; external close requests (e.g. taskkill) must not destroy it.
        return 0;
    case WM_NCDESTROY:
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        // The window can still be destroyed from outside, e.g. when its thread's desktop goes away.
        if (self && self->window_ == window) {
            self->window_ = nullptr;
            self->Destroy();
        }
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

}  // namespace ui
