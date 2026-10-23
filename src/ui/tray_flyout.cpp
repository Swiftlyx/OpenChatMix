#include "tray_flyout.h"

#include <shellscalingapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>

#include "../app.h"
#include "../i18n.h"
#include "design.h"
#include "theme.h"

namespace ui {
namespace {

constexpr wchar_t kWindowClass[] = L"OpenChatMix.TrayFlyout";
constexpr float kShadow = 16.0f;    // Room around the surface for the drawn shadow.
constexpr float kScreenGap = 8.0f;  // Between the taskbar and the surface.
constexpr float kSlide = 8.0f;      // The surface rises from the taskbar by this much while it appears.
constexpr double kAppearMs = 150.0;
constexpr float kCardWidth = 280.0f;
constexpr float kCardPadding = 16.0f;
constexpr float kCardHeight = 108.0f;
constexpr float kMenuPadding = 8.0f;
constexpr float kMenuHeader = 36.0f;
constexpr float kMenuDivider = 9.0f;
constexpr float kMenuItem = 40.0f;
constexpr float kMenuMinWidth = 220.0f;
constexpr float kMenuMaxWidth = 320.0f;
constexpr wchar_t kGlyphCheck = 0xE73E;

// The flyout whose card is up, for the WinEvent callback, which has no context of its own.
TrayFlyout* gCardFlyout = nullptr;

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

std::wstring Percent(int value) {
    return std::to_wstring(value) + L"%";
}

}  // namespace

TrayFlyout::TrayFlyout(HINSTANCE instance, HWND owner) : instance_(instance), owner_(owner) {}

TrayFlyout::~TrayFlyout() {
    Close();
}

void TrayFlyout::ShowCard(const RECT& icon, const Card& card) {
    if (MenuVisible()) {
        return;
    }
    card_ = card;
    if (CardVisible()) {
        Render();
        return;
    }
    const POINT anchor{(icon.left + icon.right) / 2, (icon.top + icon.bottom) / 2};
    Open(Kind::Card, icon, anchor, kCardWidth, kCardHeight);
}

void TrayFlyout::HideCard() {
    if (CardVisible()) {
        Close();
    }
}

void TrayFlyout::ShowMenu(const RECT& icon, POINT cursor, const std::wstring& header, std::vector<MenuEntry> entries) {
    Close();
    header_ = header;
    entries_ = std::move(entries);
    hot_ = -1;
    if (!canvas_.Init()) {
        return;
    }
    if (Open(Kind::Menu, icon, cursor, MenuWidth(), MenuHeight())) {
        SetForegroundWindow(window_);
        SetFocus(window_);
    }
}

float TrayFlyout::MenuWidth() {
    float widest = canvas_.Measure(header_, TextStyle::LabelMedium) + 32.0f;
    for (const MenuEntry& entry : entries_) {
        if (entry.command) {
            widest = std::max(widest, canvas_.Measure(entry.text, TextStyle::BodyLarge) + 52.0f + 44.0f);
        }
    }
    return std::clamp(std::ceil(widest), kMenuMinWidth, kMenuMaxWidth);
}

float TrayFlyout::MenuHeight() const {
    float height = 2.0f * kMenuPadding + kMenuHeader;
    for (const MenuEntry& entry : entries_) {
        height += entry.command ? kMenuItem : kMenuDivider;
    }
    return height;
}

bool TrayFlyout::Open(Kind kind, const RECT& icon, POINT anchor, float width, float height) {
    Close();
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = WindowProc;
        windowClass.hInstance = instance_;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.lpszClassName = kWindowClass;
        RegisterClassExW(&windowClass);
        registered = true;
    }

    HMONITOR monitor = MonitorFromRect(&icon, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(monitor, &info);
    UINT dpiX = 96;
    UINT dpiY = 96;
    GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    dpi_ = static_cast<float>(dpiX);
    const float scale = dpi_ / 96.0f;
    auto px = [scale](float dip) { return static_cast<LONG>(std::lround(dip * scale)); };

    surfaceWidth_ = width;
    surfaceHeight_ = height;
    width_ = px(width + 2.0f * kShadow);
    height_ = px(height + 2.0f * kShadow);
    const LONG surfaceWidth = px(width);
    const LONG surfaceHeight = px(height);
    const RECT& work = info.rcWork;
    const RECT& screen = info.rcMonitor;
    const LONG gap = px(kScreenGap);
    // Beside the taskbar, on whichever edge it is; an auto-hidden taskbar counts as the bottom one.
    LONG x = anchor.x - surfaceWidth / 2;
    LONG y = work.bottom - gap - surfaceHeight;
    slide_ = {0, px(kSlide)};
    if (work.top > screen.top) {
        y = work.top + gap;
        slide_ = {0, -px(kSlide)};
    } else if (work.left > screen.left) {
        x = work.left + gap;
        y = anchor.y - surfaceHeight / 2;
        slide_ = {-px(kSlide), 0};
    } else if (work.right < screen.right) {
        x = work.right - gap - surfaceWidth;
        y = anchor.y - surfaceHeight / 2;
        slide_ = {px(kSlide), 0};
    } else if (work.bottom == screen.bottom) {
        y = icon.top - gap - surfaceHeight;
    }
    x = std::clamp(x, work.left + gap, std::max(work.left + gap, work.right - gap - surfaceWidth));
    y = std::clamp(y, work.top + gap, std::max(work.top + gap, work.bottom - gap - surfaceHeight));
    origin_ = {x - px(kShadow), y - px(kShadow)};

    const DWORD exStyle = WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW |
                          (kind == Kind::Card ? WS_EX_TRANSPARENT | WS_EX_NOACTIVATE : 0);
    window_ = CreateWindowExW(exStyle, kWindowClass, L"", WS_POPUP, origin_.x, origin_.y, width_, height_, nullptr,
                              nullptr, instance_, this);
    if (!window_) {
        return false;
    }
    kind_ = kind;

    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = width_;
    bitmapInfo.bmiHeader.biHeight = -height_;  // Top-down rows.
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;
    HDC screenDc = GetDC(nullptr);
    memoryDc_ = CreateCompatibleDC(screenDc);
    void* bits = nullptr;
    bitmap_ = CreateDIBSection(screenDc, &bitmapInfo, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screenDc);
    if (!memoryDc_ || !bitmap_ || !canvas_.AttachDC(dpi_)) {
        Close();
        return false;
    }
    previousBitmap_ = SelectObject(memoryDc_, bitmap_);
    appear_ = reduceMotion_ ? 1.0f : 0.0f;
    shownAt_ = NowMs();
    Render();
    ShowWindow(window_, kind == Kind::Card ? SW_SHOWNOACTIVATE : SW_SHOW);
    if (!reduceMotion_) {
        frames_.Start(window_, WM_APP_FRAME);
    }
    // Windows 11 can still show an empty tooltip of its own (an explorer XAML popup) about a second into
    // the hover, over the card: whatever the taskbar shows while the card is up goes below it.
    DWORD taskbarProcess = 0;
    if (HWND taskbar = kind == Kind::Card ? FindWindowW(L"Shell_TrayWnd", nullptr) : nullptr) {
        GetWindowThreadProcessId(taskbar, &taskbarProcess);
    }
    if (taskbarProcess) {
        gCardFlyout = this;
        shellHook_ = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr, OnShellWindowShown, taskbarProcess,
                                     0, WINEVENT_OUTOFCONTEXT);
    }
    return true;
}

void CALLBACK TrayFlyout::OnShellWindowShown(HWINEVENTHOOK, DWORD, HWND window, LONG object, LONG child, DWORD,
                                             DWORD) {
    TrayFlyout* self = gCardFlyout;
    if (!self || !self->CardVisible() || object != OBJID_WINDOW || child != CHILDID_SELF || !window ||
        GetAncestor(window, GA_ROOT) != window) {
        return;
    }
    // The topmost window shown last is on top; the card takes that place back.
    SetWindowPos(self->window_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

void TrayFlyout::Close() {
    if (shellHook_) {
        UnhookWinEvent(shellHook_);
        shellHook_ = nullptr;
    }
    if (gCardFlyout == this) {
        gCardFlyout = nullptr;
    }
    if (HWND window = window_) {
        window_ = nullptr;
        frames_.Stop();
        DestroyWindow(window);
    }
    kind_ = Kind::None;
    hot_ = -1;
    trackingMouse_ = false;
    canvas_.Detach();
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

void TrayFlyout::Tick() {
    const double t = std::clamp((NowMs() - shownAt_) / kAppearMs, 0.0, 1.0);
    // Emphasized decelerate, approximated by a cubic ease-out.
    appear_ = static_cast<float>(1.0 - std::pow(1.0 - t, 3.0));
    Render();
    if (t >= 1.0) {
        frames_.Stop();
    }
}

void TrayFlyout::Render() {
    if (!window_ || !memoryDc_) {
        return;
    }
    const Palette& p = ThemePalette();
    const RECT bounds{0, 0, width_, height_};
    if (!canvas_.BindDC(memoryDc_, bounds)) {
        return;
    }
    canvas_.Begin();
    canvas_.Clear({0.0f, 0.0f, 0.0f, 0.0f});
    const D2D1_RECT_F surface{kShadow, kShadow, kShadow + surfaceWidth_, kShadow + surfaceHeight_};
    const float radius = kind_ == Kind::Menu ? design::kRadiusMenu : design::kRadiusMedium;
    // Level 2 elevation as a soft shadow, offset slightly downwards: the surface sits on any desktop.
    for (int layer = 1; layer <= 5; ++layer) {
        const float grow = layer * 2.0f;
        canvas_.FillRound({surface.left - grow, surface.top - grow + 2.0f, surface.right + grow, surface.bottom + grow + 2.0f},
                          radius + grow, {0.0f, 0.0f, 0.0f, 0.04f});
    }
    canvas_.FillRound(surface, radius, p.surfaceContainer);
    canvas_.StrokeRound({surface.left + 0.5f, surface.top + 0.5f, surface.right - 0.5f, surface.bottom - 0.5f},
                        radius - 0.5f, p.outlineVariant, 1.0f);
    if (kind_ == Kind::Card) {
        DrawCard();
    } else {
        DrawMenu();
    }
    canvas_.End();

    POINT destination{origin_.x + static_cast<LONG>(std::lround(slide_.x * (1.0f - appear_))),
                            origin_.y + static_cast<LONG>(std::lround(slide_.y * (1.0f - appear_)))};
    POINT source{0, 0};
    SIZE size{width_, height_};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, static_cast<BYTE>(std::lround(appear_ * 255.0f)), AC_SRC_ALPHA};
    UpdateLayeredWindow(window_, nullptr, &destination, &size, memoryDc_, &source, 0, &blend, ULW_ALPHA);
}

// Material 3 rich tooltip: the app as the subhead, the device as supporting text, then the balance
// in the Game and Chat colors.
void TrayFlyout::DrawCard() {
    const Palette& p = ThemePalette();
    const float left = kShadow + kCardPadding;
    const float right = kShadow + surfaceWidth_ - kCardPadding;
    float y = kShadow + kCardPadding;
    canvas_.Text(kAppName, {left, y, right, y + 20.0f}, TextStyle::TitleSmall, p.onSurface);
    y += 22.0f;
    canvas_.Text(card_.device, {left, y, right, y + 20.0f}, TextStyle::BodyMedium, p.onSurfaceVariant);
    y += 28.0f;
    const std::wstring game = card_.gameMuted ? std::wstring(T(Text::GameMuted))
                                              : std::wstring(T(Text::Game)) + L" " + Percent(card_.game);
    const std::wstring chat = card_.chatMuted ? std::wstring(T(Text::ChatMuted))
                                              : std::wstring(T(Text::Chat)) + L" " + Percent(card_.chat);
    const float middle = (left + right) / 2.0f;
    canvas_.Text(game, {left, y, middle, y + 22.0f}, TextStyle::TitleMedium, card_.gameMuted ? p.error : p.primary);
    canvas_.Text(chat, {middle, y, right, y + 22.0f}, TextStyle::TitleMedium, card_.chatMuted ? p.error : p.tertiary,
                 TextAlign::Trailing);
}

// Material 3 menu: the status as a header, entries with a leading icon, dividers between groups.
void TrayFlyout::DrawMenu() {
    const Palette& p = ThemePalette();
    const float left = kShadow;
    const float right = kShadow + surfaceWidth_;
    float y = kShadow + kMenuPadding;
    canvas_.Text(header_, {left + 16.0f, y, right - 16.0f, y + kMenuHeader}, TextStyle::LabelMedium, p.onSurfaceVariant);
    y += kMenuHeader;
    for (size_t index = 0; index < entries_.size(); ++index) {
        const MenuEntry& entry = entries_[index];
        if (!entry.command) {
            canvas_.FillRect({left, y + 4.0f, right, y + 5.0f}, p.outlineVariant);
            y += kMenuDivider;
            continue;
        }
        const D2D1_RECT_F row{left + 6.0f, y, right - 6.0f, y + kMenuItem};
        const D2D1_COLOR_F content = entry.danger ? p.error : p.onSurface;
        if (static_cast<int>(index) == hot_) {
            canvas_.FillRound(row, design::kRadiusMenuItem, Alpha(p.onSurface, design::kHoverOpacity));
        }
        const float middle = y + kMenuItem / 2.0f;
        if (entry.glyph) {
            canvas_.Glyph(entry.glyph, {left + 28.0f, middle}, 18.0f, entry.danger ? p.error : p.onSurfaceVariant);
        }
        canvas_.Text(entry.text, {left + 52.0f, y, right - 44.0f, y + kMenuItem}, TextStyle::BodyLarge, content);
        if (entry.checked) {
            canvas_.Glyph(kGlyphCheck, {right - 26.0f, middle}, 16.0f, p.primary);
        }
        y += kMenuItem;
    }
}

int TrayFlyout::EntryAt(POINT pixel) const {
    const float scale = dpi_ / 96.0f;
    const float x = pixel.x / scale;
    const float y = pixel.y / scale;
    if (x < kShadow || x > kShadow + surfaceWidth_) {
        return -1;
    }
    float top = kShadow + kMenuPadding + kMenuHeader;
    for (size_t index = 0; index < entries_.size(); ++index) {
        const float height = entries_[index].command ? kMenuItem : kMenuDivider;
        if (y >= top && y < top + height) {
            return entries_[index].command ? static_cast<int>(index) : -1;
        }
        top += height;
    }
    return -1;
}

void TrayFlyout::Choose(int index) {
    if (index < 0 || index >= static_cast<int>(entries_.size()) || !entries_[index].command) {
        return;
    }
    const UINT command = entries_[index].command;
    Close();
    PostMessageW(owner_, WM_COMMAND, command, 0);
}

void TrayFlyout::Step(int direction) {
    const int count = static_cast<int>(entries_.size());
    if (count == 0) {
        return;
    }
    int index = hot_;
    for (int tries = 0; tries < count; ++tries) {
        index = index < 0 ? (direction > 0 ? 0 : count - 1) : (index + direction + count) % count;
        if (entries_[index].command) {
            hot_ = index;
            Render();
            return;
        }
    }
}

LRESULT CALLBACK TrayFlyout::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* self = reinterpret_cast<TrayFlyout*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCDESTROY) {
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        if (self && self->window_ == window) {
            self->window_ = nullptr;
            self->Close();
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }
    if (self && self->window_ == window) {
        return self->Handle(message, wParam, lParam);
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT TrayFlyout::Handle(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_APP_FRAME:
        frames_.FrameHandled();
        Tick();
        return 0;
    case WM_NCHITTEST:
        return kind_ == Kind::Card ? HTTRANSPARENT : HTCLIENT;
    case WM_MOUSEACTIVATE:
        return kind_ == Kind::Card ? MA_NOACTIVATE : MA_ACTIVATE;
    case WM_ACTIVATE:
        // The menu closes like any menu once focus goes elsewhere.
        if (kind_ == Kind::Menu && LOWORD(wParam) == WA_INACTIVE) {
            PostMessageW(window_, WM_CLOSE, 0, 0);
        }
        return 0;
    case WM_CLOSE:
        Close();
        return 0;
    case WM_MOUSEMOVE: {
        if (!trackingMouse_) {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window_, 0};
            TrackMouseEvent(&track);
            trackingMouse_ = true;
        }
        const int hot = EntryAt({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
        if (hot != hot_) {
            hot_ = hot;
            Render();
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        trackingMouse_ = false;
        if (hot_ >= 0) {
            hot_ = -1;
            Render();
        }
        return 0;
    case WM_LBUTTONUP:
    case WM_RBUTTONUP: {
        const int index = EntryAt({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
        if (index >= 0) {
            Choose(index);
        }
        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN: {
        // A press on the shadow around the menu counts as outside it.
        const float scale = dpi_ / 96.0f;
        const float x = GET_X_LPARAM(lParam) / scale;
        const float y = GET_Y_LPARAM(lParam) / scale;
        if (x < kShadow || y < kShadow || x > kShadow + surfaceWidth_ || y > kShadow + surfaceHeight_) {
            Close();
        }
        return 0;
    }
    case WM_KEYDOWN:
        switch (wParam) {
        case VK_ESCAPE:
            Close();
            return 0;
        case VK_UP:
            Step(-1);
            return 0;
        case VK_DOWN:
        case VK_TAB:
            Step(1);
            return 0;
        case VK_HOME:
            hot_ = -1;
            Step(1);
            return 0;
        case VK_END:
            hot_ = -1;
            Step(-1);
            return 0;
        case VK_RETURN:
        case VK_SPACE:
            Choose(hot_);
            return 0;
        }
        break;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}

}  // namespace ui
