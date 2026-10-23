#include "panel.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <shobjidl.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <limits>
#include <memory>
#include <thread>

#include "../app.h"
#include "../app_info.h"
#include "panel_internal.h"
#include "spring.h"

using Microsoft::WRL::ComPtr;

namespace ui {

using namespace detail;

Panel::Panel(HINSTANCE instance, PanelHost& host) : instance_(instance), host_(host) {
    QueryPerformanceFrequency(&frequency_);
}

Panel::~Panel() {
    if (window_) {
        DestroyWindow(window_);
    }
    canvas_.Shutdown();
}

void Panel::SetTrayAnchor(HWND window, UINT id) {
    trayWindow_ = window;
    trayId_ = id;
}

void Panel::Show(Page page) {
    if (window_) {
        GoTo(page);
        SetForegroundWindow(window_);
        return;
    }
    filter_.clear();
    recording_ = -1;
    scroll_.clear();
    animations_.clear();
    hits_.clear();
    focusables_.clear();
    focusId_ = 0;
    focusVisible_ = false;
    hotId_ = pressedId_ = 0;
    tipId_ = 0;
    tipShown_ = false;
    lastFrame_.QuadPart = 0;
    page_ = Page::Mixer;
    CreatePanelWindow();
    if (page != Page::Mixer) {
        GoTo(page);
    }
}

void Panel::Hide() {
    if (!window_) {
        return;
    }
    StopRecording();
    menu_ = {};
    DestroyWindow(window_);
    canvas_.Shutdown();
    hiddenAt_ = GetTickCount64();
    // Decommits heap blocks freed by Direct2D and the icon cache, then trims the working set.
    HANDLE heaps[64];
    const DWORD count = GetProcessHeaps(ARRAYSIZE(heaps), heaps);
    for (DWORD i = 0; i < count && i < ARRAYSIZE(heaps); ++i) {
        HeapCompact(heaps[i], 0);
    }
    SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
}

void Panel::Toggle() {
    if (browsing_) {
        return;
    }
    if (window_) {
        Hide();
    } else if (GetTickCount64() - hiddenAt_ > kReopenGuardMs) {
        Show();
    }
}

void Panel::SetLevels(Levels levels) {
    if (levels == levels_) {
        return;
    }
    levels_ = levels;
    Invalidate();
}

void Panel::SetUpdate(const UpdateView& update) {
    update_ = update;
    Invalidate();
}

void Panel::SetBalanceDial(bool dial) {
    balanceDial_ = dial;
}

void Panel::SetChatMuted(bool muted) {
    chatMuted_ = muted;
    Invalidate();
}

void Panel::SetGameMuted(bool muted) {
    gameMuted_ = muted;
    Invalidate();
}

void Panel::SetStatus(const std::wstring& device, const std::wstring& detail, const std::wstring& connection,
                      bool connected, const std::wstring& warning) {
    device_ = device;
    deviceDetail_ = detail;
    connection_ = connection;
    deviceConnected_ = connected;
    warning_ = warning;
    Invalidate();
}

void Panel::SetApps(std::vector<AppItem> apps) {
    apps_ = std::move(apps);
    Invalidate();
}

void Panel::SetHotkeys(const std::array<Hotkey, kHotkeyCount>& hotkeys,
                       const std::array<bool, kHotkeyCount>& failed) {
    hotkeys_ = hotkeys;
    hotkeyFailed_ = failed;
    Invalidate();
}

void Panel::SetAutostart(bool enabled) {
    autostart_ = enabled;
    Invalidate();
}

void Panel::SetSettings(const Settings& settings) {
    const bool backdropChanged = settings.appearance.backdrop != settings_.appearance.backdrop;
    settings_ = settings;
    if (window_ && backdropChanged) {
        ApplyWindowTheme();
    }
    if (page_ == Page::Equalizer || page_ == Page::EqPresets) {
        Invalidate();
    }
    Invalidate();
}

void Panel::Refresh() {
    if (window_) {
        ApplyWindowTheme();
    }
    Invalidate();
}

void Panel::CreatePanelWindow() {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_DBLCLKS;
        windowClass.lpfnWndProc = WindowProc;
        windowClass.hInstance = instance_;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.lpszClassName = kWindowClass;
        RegisterClassExW(&windowClass);
        registered = true;
    }

    POINT anchor{};
    RECT iconRect{};
    NOTIFYICONIDENTIFIER identifier{};
    identifier.cbSize = sizeof(identifier);
    identifier.hWnd = trayWindow_;
    identifier.uID = trayId_;
    if (trayWindow_ && SUCCEEDED(Shell_NotifyIconGetRect(&identifier, &iconRect))) {
        anchor = {(iconRect.left + iconRect.right) / 2, (iconRect.top + iconRect.bottom) / 2};
    } else {
        GetCursorPos(&anchor);
    }
    HMONITOR monitor = MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(monitor, &info);
    UINT dpiX = 96;
    UINT dpiY = 96;
    GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);

    const RECT& work = info.rcWork;
    const float monitorScale = dpiX / 96.0f;
    const int margin = static_cast<int>(std::lround(kScreenMargin * monitorScale));
    // Short screens get a uniformly smaller layout instead of a clipped one.
    const float scale = std::min(monitorScale, static_cast<float>(work.bottom - work.top - 2 * margin) / kHeight);
    dpi_ = 96.0f * scale;
    const int width = static_cast<int>(std::lround(kWidth * scale));
    const int height = static_cast<int>(std::lround(kHeight * scale));
    int x = work.right - width - margin;
    int y = work.bottom - height - margin;
    if (work.left > info.rcMonitor.left) {
        x = work.left + margin;
    }
    if (work.top > info.rcMonitor.top) {
        y = work.top + margin;
    }

    // A frameless popup; DWM supplies the shadow, rounded corners and backdrop.
    CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kWindowClass, kAppName, WS_POPUP, x, y, width, height, nullptr,
                    nullptr, instance_, this);
    if (!window_) {
        return;
    }
    ApplyWindowTheme();
    canvas_.Attach(window_, dpi_);
    meters_ = {};
    SetTimer(window_, kMeterTimer, kMeterIntervalMs, nullptr);
    ShowWindow(window_, SW_SHOW);
    SetForegroundWindow(window_);
}

void Panel::ApplyWindowTheme() {
    palette_ = &ThemePalette();
    const BOOL dark = palette_->dark;
    DwmSetWindowAttribute(window_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    const DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(window_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    DWM_SYSTEMBACKDROP_TYPE backdrop = DWMSBT_NONE;
    switch (settings_.appearance.backdrop) {
    case Backdrop::Acrylic: backdrop = DWMSBT_TRANSIENTWINDOW; break;
    case Backdrop::Mica: backdrop = DWMSBT_MAINWINDOW; break;
    case Backdrop::Solid: break;
    }
    const bool applied =
        SUCCEEDED(DwmSetWindowAttribute(window_, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop)));
    backdrop_ = backdrop != DWMSBT_NONE && applied;
    const MARGINS margins{-1, -1, -1, -1};
    DwmExtendFrameIntoClientArea(window_, &margins);
}

void Panel::Invalidate() {
    if (window_) {
        InvalidateRect(window_, nullptr, FALSE);
    }
}

LRESULT CALLBACK Panel::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    Panel* self = nullptr;
    if (message == WM_NCCREATE) {
        self = static_cast<Panel*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->window_ = window;
    } else {
        self = reinterpret_cast<Panel*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    }
    return self ? self->HandleMessage(window, message, wParam, lParam)
                : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT Panel::HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto dip = [this](int pixels) { return pixels * 96.0f / dpi_; };

    switch (message) {
    case WM_TIMER:
        if (wParam == kMeterTimer) {
            TickMeters();
            return 0;
        }
        if (wParam == kTooltipTimer) {
            KillTimer(window, kTooltipTimer);
            if (tipId_ && tipId_ == hotId_) {
                tipShown_ = true;
                Invalidate();
            }
            return 0;
        }
        break;
    case WM_ACTIVATE:
        // Deferred so the window is not destroyed in the middle of activation processing.
        if (LOWORD(wParam) == WA_INACTIVE && autoHide_ && !pinned_ && !browsing_) {
            PostMessageW(window, WM_CLOSE, 0, 0);
        }
        // DefWindowProc gives the window keyboard focus; without it typing arrives as WM_SYSCHAR.
        break;
    case WM_CLOSE:
        if (!browsing_) {
            Hide();
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint;
        BeginPaint(window, &paint);
        Render();
        EndPaint(window, &paint);
        return 0;
    }
    case WM_SIZE:
        canvas_.Resize(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_DPICHANGED:
        // The panel keeps the scale chosen when it opened.
        return 0;
    case WM_APP_FRAME:
        frames_.FrameHandled();
        if (animating_) {
            InvalidateRect(window, nullptr, FALSE);
            UpdateWindow(window);
        }
        return 0;
    case WM_MOUSEMOVE:
        OnMouseMove(dip(GET_X_LPARAM(lParam)), dip(GET_Y_LPARAM(lParam)));
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        OnMouseDown(dip(GET_X_LPARAM(lParam)), dip(GET_Y_LPARAM(lParam)), message == WM_LBUTTONDBLCLK);
        return 0;
    case WM_LBUTTONUP:
        OnMouseUp(dip(GET_X_LPARAM(lParam)), dip(GET_Y_LPARAM(lParam)));
        return 0;
    case WM_MOUSELEAVE:
        trackingMouse_ = false;
        ArmTooltip(nullptr);
        if (hotId_) {
            hotId_ = 0;
            Invalidate();
        }
        return 0;
    case WM_MOUSEWHEEL: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(window, &point);
        OnWheel(dip(point.x), dip(point.y), GET_WHEEL_DELTA_WPARAM(wParam));
        return 0;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (OnKey(static_cast<UINT>(wParam), true)) {
            return 0;
        }
        break;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (OnKey(static_cast<UINT>(wParam), false)) {
            return 0;
        }
        break;
    case WM_CHAR:
        OnChar(static_cast<wchar_t>(wParam));
        return 0;
    case WM_SYSCHAR:
        if (recording_ >= 0) {
            return 0;
        }
        break;
    case WM_SETTINGCHANGE:
        if (lParam && CompareStringOrdinal(reinterpret_cast<LPCWSTR>(lParam), -1, L"ImmersiveColorSet", -1, TRUE) ==
                          CSTR_EQUAL) {
            ApplyWindowTheme();
            Invalidate();
        }
        break;
    case WM_APP_BROWSE_DONE:
        OnFileDialogDone(static_cast<FileDialog>(wParam), reinterpret_cast<std::wstring*>(lParam));
        return 0;
    case WM_DESTROY:
        frames_.Stop();
        lastFrame_.QuadPart = 0;
        canvas_.Detach();
        icons_.clear();
        hits_.clear();
        return 0;
    case WM_NCDESTROY:
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        window_ = nullptr;
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

float Panel::Animate(uint64_t key, float target, design::Spring spring, bool essential) {
    const auto [it, inserted] = animations_.try_emplace(key, Motion{target, 0.0f});
    Motion& motion = it->second;
    if (inserted || (settings_.appearance.reduceMotion && !essential)) {
        motion = {target, 0.0f};
        return target;
    }
    if (settings_.appearance.reduceMotion) {
        spring = design::kSpringReduced;
    }
    StepSpring(motion.value, motion.velocity, target, spring, frameSeconds_);
    // Normalized values settle finer than ones in DIPs or dB.
    const float epsilon = std::fabs(target) <= 1.5f ? 0.002f : 0.05f;
    if (std::fabs(motion.value - target) < epsilon && std::fabs(motion.velocity) < 20.0f * epsilon) {
        motion = {target, 0.0f};
    } else {
        animating_ = true;
    }
    return motion.value;
}

float Panel::AnimatedValue(uint64_t key, float fallback) const {
    const auto found = animations_.find(key);
    return found == animations_.end() ? fallback : found->second.value;
}

void Panel::SetAnimation(uint64_t key, float value) {
    animations_[key] = {value, 0.0f};
}

void Panel::Render() {
    if (!canvas_.IsAttached() && !canvas_.Attach(window_, dpi_)) {
        return;
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    frameSeconds_ = lastFrame_.QuadPart
                        ? std::min(0.05f, static_cast<float>(now.QuadPart - lastFrame_.QuadPart) / frequency_.QuadPart)
                        : 0.0f;
    lastFrame_ = now;
    animating_ = false;
    hits_.clear();
    hitClip_.clear();
    focusables_.clear();
    pageScrollDepth_ = 0;

    canvas_.Begin();
    canvas_.Clear({0.0f, 0.0f, 0.0f, 0.0f});
    canvas_.FillRect({0.0f, 0.0f, kWidth, kHeight}, Alpha(P().surface, backdrop_ ? P().surfaceAlpha : 1.0f));
    switch (page_) {
    case Page::Mixer: DrawMixer(); break;
    case Page::Equalizer: DrawEqualizerPage(); break;
    case Page::Headset: DrawHeadset(); break;
    case Page::Settings: DrawSettings(); break;
    case Page::Group: DrawGroup(); break;
    case Page::AddApp: DrawAddApp(); break;
    case Page::EqPresets: DrawEqPresets(); break;
    case Page::Appearance: DrawAppearance(); break;
    case Page::ColorPicker: DrawColorPicker(); break;
    case Page::Audio: DrawAudio(); break;
    case Page::PickDevice: DrawPickDevice(); break;
    case Page::Hotkeys: DrawHotkeys(); break;
    case Page::Notifications: DrawNotifications(); break;
    case Page::Profiles: DrawProfiles(); break;
    case Page::ProfileEdit: DrawProfileEdit(); break;
    case Page::Microphone: DrawMicrophone(); break;
    case Page::Outputs: DrawOutputs(); break;
    case Page::General: DrawGeneral(); break;
    case Page::Welcome: DrawWelcome(); break;
    case Page::RoutingChannel: DrawRoutingChannel(); break;
    case Page::AddOns: DrawAddOns(); break;
    case Page::AddOnEdit: DrawAddOnEdit(); break;
    case Page::AddOnEvent: DrawAddOnEvent(); break;
    case Page::AddOnDisplay: DrawAddOnDisplay(); break;
    }
    DrawSnackbar();
    if (page_ != Page::Welcome) {
        DrawNavigationBar();
    }
    drawingMenu_ = true;
    DrawMenu();
    drawingMenu_ = false;
    DrawTooltip();
    DrawFocusRing();
    if (canvas_.End() == D2DERR_RECREATE_TARGET) {
        canvas_.Detach();
        icons_.clear();
        animating_ = true;
    }

    if (animating_) {
        frames_.Start(window_, WM_APP_FRAME);
    } else {
        frames_.Stop();
        lastFrame_.QuadPart = 0;
    }
}

Page Panel::ParentOf(Page page) {
    switch (page) {
    case Page::Group:
    case Page::AddApp: return Page::Mixer;
    case Page::EqPresets: return Page::Equalizer;
    case Page::Appearance:
    case Page::Audio:
    case Page::Hotkeys:
    case Page::Notifications:
    case Page::Profiles:
    case Page::Microphone:
    case Page::Outputs:
    case Page::AddOns:
    case Page::General: return Page::Settings;
    case Page::AddOnEdit: return Page::AddOns;
    case Page::AddOnEvent: return Page::AddOnEdit;
    case Page::AddOnDisplay: return Page::AddOnEvent;
    case Page::ColorPicker: return Page::Appearance;
    case Page::PickDevice:
    case Page::RoutingChannel: return Page::Audio;
    case Page::ProfileEdit: return Page::Profiles;
    case Page::Mixer:
    case Page::Equalizer:
    case Page::Headset:
    case Page::Settings:
    case Page::Welcome: break;
    }
    return page;
}

Page Panel::TabOf(Page page) const {
    if (page == Page::AddApp) {
        return addTarget_ == AddTarget::Profile ? Page::Settings : Page::Mixer;
    }
    while (ParentOf(page) != page) {
        page = ParentOf(page);
    }
    return page;
}

float Panel::ContentBottom() const {
    return page_ == Page::Welcome ? kHeight : kHeight - kNavHeight;
}

void Panel::GoTo(Page page) {
    StopRecording();
    ArmTooltip(nullptr);
    CloseMenu();
    CancelField();
    drag_ = {};
    if ((page == Page::Headset && !HeadsetTabVisible()) ||
        ((page == Page::Equalizer || page == Page::EqPresets) && !settings_.equalizer)) {
        page = Page::Mixer;
    }
    // Pages opened from their parent or from the navigation bar start at the top.
    if (page != page_) {
        focusId_ = 0;
        transferNote_.clear();
    }
    const bool deeper = ParentOf(page) == page_ || ParentOf(page) == page;
    if (deeper && page != page_) {
        scroll_[page] = 0.0f;
        animations_.erase(AnimKey(kIdSettingsPage, static_cast<uint32_t>(page)));
    }
    switch (page) {
    case Page::AddApp:
        running_ = host_.RunningApps();
        filter_.clear();
        addScroll_ = 0.0f;
        animations_.erase(AnimKey(kIdRunningList, 0));
        break;
    case Page::Group:
        if (page_ != Page::AddApp) {
            groupScroll_ = 0.0f;
            animations_.erase(AnimKey(kIdGroupList, 0));
        }
        break;
    case Page::Headset:
        userPresets_ = LoadUserPresets();
        LoadEqLibrary(EqChannel::Game);
        break;
    case Page::PickDevice:
        endpoints_ = pickSlot_ == RoutingSlot::MicInput ? host_.RecordingDevices() : host_.PlaybackDevices();
        scroll_[page] = 0.0f;
        animations_.erase(AnimKey(kIdPickList, 0));
        break;
    case Page::ColorPicker: {
        // The picker starts from the color in use.
        const AppearanceSettings& appearance = settings_.appearance;
        const uint32_t accent = appearance.windowsAccent ? WindowsAccentColor() : appearance.accent;
        const uint32_t seed = pickerChat_ ? EffectiveChatSeed(accent, appearance.chatAccent) : accent;
        HueChromaOf(seed, pickerHue_, pickerChroma_);
        hexInput_ = HexColor(seed);
        break;
    }
    case Page::Equalizer:
    case Page::EqPresets:
        LoadEqLibrary(eqChannel_);
        break;
    case Page::ProfileEdit:
        LoadEqLibrary(EqChannel::Game);
        break;
    case Page::Welcome:
        if (page_ != Page::Welcome) {
            tourStep_ = 0;
        }
        break;
    default:
        break;
    }
    page_ = page;
    hotId_ = pressedId_ = 0;
    Invalidate();
}

void Panel::GoBack() {
    if (page_ == Page::AddApp) {
        GoTo(addTarget_ == AddTarget::Profile ? Page::Profiles : Page::Group);
        return;
    }
    if (page_ == Page::PickDevice && MicSlot()) {
        GoTo(Page::Microphone);
        return;
    }
    if (page_ == Page::PickDevice && ChannelSlot()) {
        GoTo(Page::RoutingChannel);
        return;
    }
    GoTo(ParentOf(page_));
}

void Panel::DrawNavigationBar() {
    const Palette& p = P();
    const float top = ContentBottom();
    canvas_.FillRect({0.0f, top, kWidth, kHeight}, Alpha(p.surfaceContainer, backdrop_ ? 0.94f : 1.0f));
    // Swallows clicks so nothing underneath reacts through the bar.
    AddHit({{0.0f, top, kWidth, kHeight}, kIdNavigation + 7, [] {}});

    struct Destination {
        Page page;
        wchar_t glyph;
        Text label;
    };
    std::vector<Destination> destinations{{Page::Mixer, kGlyphMixer, Text::Mixer}};
    if (settings_.equalizer) {
        destinations.push_back({Page::Equalizer, kGlyphEqualizer, Text::Equalizers});
    }
    if (HeadsetTabVisible()) {
        destinations.push_back({Page::Headset, kGlyphHeadphones, Text::HeadsetPage});
    }
    destinations.push_back({Page::Settings, kGlyphSettings, Text::Settings});

    // Material 3 Expressive navigation bar: the active indicator springs open around the icon.
    const Page current = TabOf(page_);
    const float width = kWidth / destinations.size();
    const float indicatorTop = top + 6.0f;
    const float indicatorHeight = 32.0f;
    for (size_t i = 0; i < destinations.size(); ++i) {
        const Destination& destination = destinations[i];
        const float center = width * (i + 0.5f);
        const uint32_t id = kIdNavigation + static_cast<uint32_t>(destination.page);
        const bool active = destination.page == current;
        const float open = Animate(AnimKey(id, 0), active ? 1.0f : 0.0f, design::kSpringFastSpatial);
        const float fill = Animate(AnimKey(id, kSelectChannel), active ? 1.0f : 0.0f, design::kSpringDefaultEffects);
        const float half = 16.0f + 12.0f * std::max(0.0f, open);
        const D2D1_RECT_F indicator{center - half, indicatorTop, center + half, indicatorTop + indicatorHeight};
        if (fill > 0.01f) {
            canvas_.FillRound(indicator, indicatorHeight / 2.0f, Alpha(p.secondaryContainer, fill));
        }
        StateLayer(id, {center - 28.0f, indicatorTop, center + 28.0f, indicatorTop + indicatorHeight},
                   indicatorHeight / 2.0f, p.onSurface);
        canvas_.Glyph(destination.glyph, {center, indicatorTop + indicatorHeight / 2.0f}, design::kIconSize - 2.0f,
                      Mix(p.onSurfaceVariant, p.onSecondaryContainer, fill));
        if (destination.page == Page::Settings && update_.state == UpdateView::State::Available) {
            canvas_.FillCircle({center + 12.0f, indicatorTop + 7.0f}, 4.0f, p.error);
        }
        canvas_.Text(T(destination.label), {center - width / 2.0f, top + 40.0f, center + width / 2.0f, top + 58.0f},
                     TextStyle::LabelMedium, Mix(p.onSurfaceVariant, p.onSurface, fill), TextAlign::Center);
        AddHit({{center - width / 2.0f, top, center + width / 2.0f, kHeight}, id, [this, page = destination.page] {
                    GoTo(page);
                }});
    }
}

void Panel::DrawHeader(const wchar_t* title, const wchar_t* subtitle, float trailing) {
    const float middle = kHeaderHeight / 2.0f;
    IconButton(kIdBack, {kGutter + design::kIconButtonSize / 2.0f, middle}, kGlyphBack, P().onSurface,
               [this] { GoBack(); });
    Tooltip(kIdBack, T(Text::TipBack));
    const float left = kGutter + design::kIconButtonSize + design::kSpace1;
    const float right = kWidth - kGutter - trailing;
    if (subtitle && *subtitle) {
        canvas_.Text(title, {left, middle - 18.0f, right, middle + 2.0f}, TextStyle::TitleMedium, P().onSurface);
        canvas_.Text(subtitle, {left, middle + 2.0f, right, middle + 18.0f}, TextStyle::BodySmall,
                     P().onSurfaceVariant);
    } else {
        canvas_.Text(title, {left, 0.0f, right, kHeaderHeight}, TextStyle::TitleLarge, P().onSurface);
    }
}

void Panel::DrawTitle(const wchar_t* title) {
    canvas_.Text(title, {kInset, 0.0f, kWidth - kGutter - design::kIconButtonSize, kHeaderHeight},
                 TextStyle::TitleLarge, P().onSurface);
    DrawPinButton();
}

float Panel::SectionTitle(const wchar_t* title, float y) {
    canvas_.Text(title, {kInset, y, kWidth - kInset, y + design::kSpace5}, TextStyle::TitleSmall, P().primary);
    return y + design::kSectionTitleHeight;
}

float Panel::PageHint(const wchar_t* text, float y, std::optional<D2D1_COLOR_F> color) {
    const float height = std::ceil(canvas_.MeasureHeight(text, TextStyle::BodySmall, kWidth - 2.0f * kInset));
    canvas_.Text(text, {kInset, y, kWidth - kInset, y + height}, TextStyle::BodySmall,
                 color.value_or(P().onSurfaceVariant), TextAlign::Leading, true);
    return y + height + design::kPageHintGap;
}

D2D1_RECT_F Panel::Card(float top, float height) {
    const D2D1_RECT_F card{kGutter, top, kWidth - kGutter, top + height};
    canvas_.FillRound(card, design::kRadiusGroup, Alpha(P().surfaceContainer, P().tileAlpha));
    return card;
}

void Panel::DrawTile(const D2D1_RECT_F& rect, uint32_t id, bool first, bool last, bool lifted) {
    const Palette& p = P();
    // Hovered and pressed tiles round their inner corners; the group's outer corners stay large.
    const bool hot = id && hotId_ == id;
    const float hover = id ? Animate(AnimKey(id, kShapeChannel), lifted ? 2.0f : hot ? 1.0f : 0.0f,
                                     design::kSpringFastSpatial)
                           : 0.0f;
    float inner = design::kRadiusTile + (design::kRadiusTileHovered - design::kRadiusTile) * std::max(0.0f, hover);
    if (hover > 1.0f) {
        inner = design::kRadiusTileHovered + (design::kRadiusTileLifted - design::kRadiusTileHovered) * (hover - 1.0f);
    }
    const float top = first ? std::max(inner, design::kRadiusGroup) : inner;
    const float bottom = last ? std::max(inner, design::kRadiusGroup) : inner;
    const Corners corners = Corners::Vertical(top, bottom);
    if (lifted) {
        // Raised by tone, not by a shadow: the most raised surface and the drag layer.
        canvas_.FillShape(rect, corners, p.surfaceContainerHighest);
        canvas_.FillShape(rect, corners, Alpha(p.onSurface, design::kDragOpacity / 2.0f));
        return;
    }
    canvas_.FillShape(rect, corners, Alpha(p.surfaceContainer, p.tileAlpha));
    if (id) {
        StateLayer(id, rect, corners, p.onSurface);
    }
}

float Panel::BeginScroll(Page page, float top) {
    const D2D1_RECT_F area{0.0f, top, kWidth, ContentBottom()};
    const uint64_t key = AnimKey(kIdSettingsPage, static_cast<uint32_t>(page));
    const float offset = Animate(key, scroll_[page], design::kSpringScroll, true);
    Hit wheel;
    wheel.rect = area;
    wheel.id = kIdSettingsPage;
    // Clamped to the limit of the last frame, so the content never runs past its end and springs back.
    wheel.wheel = [this, page](int notches) {
        const auto limit = scrollMax_.find(page);
        scroll_[page] = std::clamp(scroll_[page] - notches * kRowHeight, 0.0f,
                                   limit == scrollMax_.end() ? std::numeric_limits<float>::max() : limit->second);
        Invalidate();
    };
    AddHit(std::move(wheel));
    canvas_.PushClip(area);
    hitClip_.push_back(area);
    pageScrollDepth_ = hitClip_.size();
    return top + 4.0f - offset;
}

void Panel::EndScroll(Page page, float top, float y) {
    hitClip_.pop_back();
    pageScrollDepth_ = 0;
    canvas_.PopClip();
    const D2D1_RECT_F area{0.0f, top, kWidth, ContentBottom()};
    const float offset = AnimatedValue(AnimKey(kIdSettingsPage, static_cast<uint32_t>(page)), 0.0f);
    const float content = y + offset - top + 12.0f;
    const float maxScroll = std::max(0.0f, content - (area.bottom - area.top));
    scrollMax_[page] = maxScroll;
    float& scroll = scroll_[page];
    if (scroll < 0.0f || scroll > maxScroll) {
        scroll = std::clamp(scroll, 0.0f, maxScroll);
        Invalidate();
    }
    DrawScrollbar(area, offset, content, kIdPageScrollbar, [this, page](float value) {
        scroll_[page] = value;
        SetAnimation(AnimKey(kIdSettingsPage, static_cast<uint32_t>(page)), value);
    });
}

void Panel::AddHit(Hit hit) {
    // Anything that acts on a click or a drag is a focus stop. The menu's backdrop and list surface,
    // the page's wheel area and the mixer card's wheel area are not.
    const bool focusable = hit.id != 0 && (hit.click || hit.drag) && hit.id != kIdMenu && hit.id != kIdMenuList &&
                           hit.id != kIdSettingsPage && hit.id != kIdMixCard && hit.id != kIdMenuScrollbar &&
                           hit.id != kIdPageScrollbar;
    if (focusable && std::none_of(focusables_.begin(), focusables_.end(),
                                  [&](const Focusable& known) { return known.id == hit.id; })) {
        Focusable stop;
        stop.id = hit.id;
        stop.rect = hit.rect;
        stop.clip = hitClip_.empty() ? D2D1_RECT_F{0.0f, 0.0f, kWidth, kHeight} : hitClip_.back();
        stop.pageScroll = pageScrollDepth_ != 0 && hitClip_.size() == pageScrollDepth_;
        stop.inMenu = drawingMenu_;
        stop.click = hit.click;
        stop.wheel = hit.wheel;
        focusables_.push_back(std::move(stop));
    }
    if (!hitClip_.empty()) {
        const D2D1_RECT_F& clip = hitClip_.back();
        hit.rect = {std::max(hit.rect.left, clip.left), std::max(hit.rect.top, clip.top),
                    std::min(hit.rect.right, clip.right), std::min(hit.rect.bottom, clip.bottom)};
        if (hit.rect.right <= hit.rect.left || hit.rect.bottom <= hit.rect.top) {
            return;
        }
    }
    hits_.push_back(std::move(hit));
}

void Panel::MoveFocus(int direction) {
    // While a menu is open, focus stays among its items.
    std::vector<const Focusable*> order;
    for (const Focusable& stop : focusables_) {
        if (stop.inMenu == menu_.open) {
            order.push_back(&stop);
        }
    }
    if (order.empty()) {
        return;
    }
    const auto current =
        std::find_if(order.begin(), order.end(), [this](const Focusable* stop) { return stop->id == focusId_; });
    const size_t count = order.size();
    const size_t index = current == order.end()
                             ? (direction > 0 ? 0 : count - 1)
                             : (static_cast<size_t>(current - order.begin()) + count + direction) % count;
    const Focusable& next = *order[index];
    focusId_ = next.id;
    focusVisible_ = true;

    // A stop out of view in the page's scrolling area scrolls in, with a little room around it.
    const auto limit = scrollMax_.find(page_);
    if (next.pageScroll && limit != scrollMax_.end()) {
        const float margin = design::kSpace3;
        float delta = 0.0f;
        if (next.rect.top < next.clip.top + margin) {
            delta = next.rect.top - (next.clip.top + margin);
        } else if (next.rect.bottom > next.clip.bottom - margin) {
            delta = std::min(next.rect.bottom - (next.clip.bottom - margin), next.rect.top - (next.clip.top + margin));
        }
        if (delta != 0.0f) {
            const float offset = AnimatedValue(AnimKey(kIdSettingsPage, static_cast<uint32_t>(page_)), 0.0f);
            scroll_[page_] = std::clamp(offset + delta, 0.0f, limit->second);
        }
    }
    Invalidate();
}

bool Panel::ActivateFocus() {
    if (!focusVisible_ || !focusId_) {
        return false;
    }
    const auto stop = std::find_if(focusables_.begin(), focusables_.end(),
                                   [this](const Focusable& known) { return known.id == focusId_; });
    if (stop == focusables_.end() || !stop->click) {
        return false;
    }
    // The click may change the page and with it the focus stops.
    const std::function<void()> click = stop->click;
    click();
    Invalidate();
    return true;
}

bool Panel::StepFocus(int notches) {
    if (!focusVisible_ || !focusId_) {
        return false;
    }
    const auto stop = std::find_if(focusables_.begin(), focusables_.end(),
                                   [this](const Focusable& known) { return known.id == focusId_; });
    if (stop == focusables_.end() || !stop->wheel) {
        return false;
    }
    const std::function<void(int)> wheel = stop->wheel;
    wheel(notches);
    Invalidate();
    return true;
}

void Panel::DrawFocusRing() {
    if (!focusVisible_ || !focusId_) {
        return;
    }
    const auto stop = std::find_if(focusables_.begin(), focusables_.end(),
                                   [this](const Focusable& known) { return known.id == focusId_; });
    if (stop == focusables_.end()) {
        return;
    }
    // Material 3 focus indicator: a ring that follows the element's shape, a little outside it.
    const float outset = design::kFocusRingOffset + design::kFocusRingWidth / 2.0f;
    const D2D1_RECT_F ring = Inset(stop->rect, -outset, -outset);
    const float radius = std::min((ring.bottom - ring.top) / 2.0f, design::kRadiusLarge + outset);
    canvas_.PushClip(stop->clip);
    canvas_.StrokeRound(ring, radius, P().secondary, design::kFocusRingWidth);
    canvas_.PopClip();
}

void Panel::StateLayer(uint32_t id, const D2D1_RECT_F& rect, float radius, D2D1_COLOR_F content) {
    StateLayer(id, rect, Corners::All(radius), content);
}

void Panel::StateLayer(uint32_t id, const D2D1_RECT_F& rect, const Corners& corners, D2D1_COLOR_F content) {
    const bool hot = hotId_ == id;
    float target =
        hot ? (pressedId_ == id ? design::kPressOpacity + design::kHoverOpacity : design::kHoverOpacity) : 0.0f;
    if (focusVisible_ && focusId_ == id) {
        target = std::max(target, design::kFocusOpacity);
    }
    const float alpha = Animate(AnimKey(id, kHoverChannel), target, design::kSpringFastEffects);
    if (alpha > 0.002f) {
        canvas_.FillShape(rect, corners, Alpha(content, alpha));
    }
}

void Panel::IconButton(uint32_t id, D2D1_POINT_2F center, wchar_t glyph, D2D1_COLOR_F color,
                       std::function<void()> click) {
    const float half = design::kIconButtonSize / 2.0f;
    const D2D1_RECT_F rect{center.x - half, center.y - half, center.x + half, center.y + half};
    StateLayer(id, rect, half, color);
    canvas_.Glyph(glyph, center, design::kIconSize - 2.0f, color);
    AddHit({rect, id, std::move(click)});
}

void Panel::Tooltip(uint32_t id, const wchar_t* text) {
    for (auto it = hits_.rbegin(); it != hits_.rend(); ++it) {
        if (it->id == id) {
            it->tooltip = text;
            return;
        }
    }
}

void Panel::ArmTooltip(const Hit* hit) {
    const uint32_t id = hit && hit->tooltip ? hit->id : 0;
    if (id == tipId_ && (id == 0 || tipShown_)) {
        return;
    }
    if (tipShown_) {
        Invalidate();
    }
    tipId_ = id;
    tipShown_ = false;
    SetAnimation(AnimKey(kIdTooltip, 0), 0.0f);
    if (!window_) {
        return;
    }
    KillTimer(window_, kTooltipTimer);
    if (id) {
        SetTimer(window_, kTooltipTimer, kTooltipDelayMs, nullptr);
    }
}

// Material 3 plain tooltip: one or two lines of BodySmall on inverseSurface below the control, or
// above it where the page ends. It follows a resting pointer, or keyboard focus at once.
void Panel::DrawTooltip() {
    if (menu_.open) {
        return;
    }
    const uint32_t id = tipShown_ && tipId_ == hotId_ ? tipId_ : focusVisible_ ? focusId_ : 0;
    const Hit* anchor = nullptr;
    for (const Hit& hit : hits_) {
        if (id && hit.id == id && hit.tooltip) {
            anchor = &hit;
        }
    }
    if (!anchor) {
        return;
    }
    const Palette& p = P();
    const float opacity = std::clamp(Animate(AnimKey(kIdTooltip, 0), 1.0f, design::kSpringFastEffects), 0.0f, 1.0f);
    constexpr float kPadding = 8.0f;
    constexpr float kMaxWidth = 232.0f;
    const float textWidth = std::min(std::ceil(canvas_.Measure(anchor->tooltip, TextStyle::BodySmall)) + 1.0f,
                                     kMaxWidth - 2.0f * kPadding);
    const float textHeight = std::ceil(canvas_.MeasureHeight(anchor->tooltip, TextStyle::BodySmall, textWidth));
    const float width = textWidth + 2.0f * kPadding;
    const float height = std::max(24.0f, textHeight + 8.0f);
    const D2D1_RECT_F& at = anchor->rect;
    const float left = std::clamp((at.left + at.right - width) / 2.0f, kGutter, kWidth - kGutter - width);
    float top = at.bottom + design::kSpace1;
    if (top + height > ContentBottom() - design::kSpace1 && at.top - design::kSpace1 - height >= design::kSpace1) {
        top = at.top - design::kSpace1 - height;
    }
    const D2D1_RECT_F box{left, top, left + width, top + height};
    canvas_.FillRound(box, design::kRadiusExtraSmall, Alpha(p.inverseSurface, opacity));
    canvas_.Text(anchor->tooltip, {box.left + kPadding, box.top, box.right - kPadding + 1.0f, box.bottom},
                 TextStyle::BodySmall, Alpha(p.inverseOnSurface, opacity), TextAlign::Center, textHeight > 20.0f);
}

void Panel::TextButton(uint32_t id, const D2D1_RECT_F& rect, wchar_t glyph, const wchar_t* label, bool tonal,
                       std::function<void()> click) {
    const Palette& p = P();
    // Material 3 Expressive buttons morph from round toward square while pressed.
    const float pressed = Animate(AnimKey(id, kPressChannel), pressedId_ == id ? 1.0f : 0.0f,
                                  design::kSpringFastSpatial);
    const float round = (rect.bottom - rect.top) / 2.0f;
    const float radius = round + (design::kRadiusSmall - round) * std::clamp(pressed, 0.0f, 1.2f);
    const D2D1_COLOR_F content = tonal ? p.onSecondaryContainer : p.primary;
    if (tonal) {
        canvas_.FillRound(rect, radius, p.secondaryContainer);
    }
    StateLayer(id, rect, radius, content);
    const float total = (glyph ? 16.0f + 8.0f : 0.0f) + canvas_.Measure(label, TextStyle::LabelLarge);
    const float start = (rect.left + rect.right - total) / 2.0f;
    const float centerY = (rect.top + rect.bottom) / 2.0f;
    if (glyph) {
        canvas_.Glyph(glyph, {start + 8.0f, centerY}, 16.0f, content);
    }
    canvas_.Text(label, {start + (glyph ? 24.0f : 0.0f), rect.top, rect.right, rect.bottom}, TextStyle::LabelLarge,
                 content);
    AddHit({rect, id, std::move(click)});
}

void Panel::DrawSegmented(const D2D1_RECT_F& rect, uint32_t baseId, std::span<const wchar_t* const> labels,
                          int selected, D2D1_COLOR_F fill, D2D1_COLOR_F onFill,
                          const std::function<void(int)>& select) {
    // A Material 3 Expressive connected button group: separate buttons 2 DIP apart, round on the
    // outside and squarer inside; the selected one springs fully round and fills.
    const Palette& p = P();
    const size_t count = labels.size();
    const float round = (rect.bottom - rect.top) / 2.0f;
    // Buttons share the width equally while every label fits. Otherwise each gets its label's width
    // plus an equal share of what is left, and only then the labels shrink a size: a label in a button
    // group is never cut off.
    constexpr float kLabelPadding = 6.0f;
    const float space = rect.right - rect.left - design::kButtonGroupGap * (count - 1);
    TextStyle style = TextStyle::LabelMedium;
    std::vector<float> natural(count);
    auto measure = [&] {
        float total = 0.0f;
        for (size_t index = 0; index < count; ++index) {
            natural[index] = canvas_.Measure(labels[index], style) + 2.0f * kLabelPadding;
            total += natural[index];
        }
        return total;
    };
    float naturalTotal = measure();
    if (naturalTotal > space) {
        style = TextStyle::LabelSmall;
        naturalTotal = measure();
    }
    const float equal = space / count;
    const bool equalFits = std::all_of(natural.begin(), natural.end(), [equal](float w) { return w <= equal; });
    const float extra = std::max(0.0f, space - naturalTotal) / count;
    float left = rect.left;
    for (size_t index = 0; index < count; ++index) {
        const float width = equalFits ? equal : natural[index] + extra;
        const D2D1_RECT_F button{left, rect.top, left + width, rect.bottom};
        left += width + design::kButtonGroupGap;
        const uint32_t id = PartId(baseId, static_cast<uint32_t>(index) + 1);
        const bool chosen = static_cast<int>(index) == selected;
        const float shape = Animate(AnimKey(id, kShapeChannel), chosen ? 1.0f : 0.0f, design::kSpringFastSpatial);
        const float color = Animate(AnimKey(id, kSelectChannel), chosen ? 1.0f : 0.0f, design::kSpringDefaultEffects);
        const float press = Animate(AnimKey(id, kPressChannel), pressedId_ == id && !chosen ? 1.0f : 0.0f,
                                    design::kSpringFastSpatial);
        float inner = design::kRadiusButtonGroupInner + (round - design::kRadiusButtonGroupInner) * shape;
        inner += (design::kRadiusButtonGroupPressed - design::kRadiusButtonGroupInner) * press;
        inner = std::clamp(inner, 2.0f, round);
        const Corners corners = Corners::Horizontal(index == 0 ? round : inner, index + 1 == count ? round : inner);
        canvas_.FillShape(button, corners, Mix(p.surfaceContainerHighest, fill, color));
        StateLayer(id, button, corners, chosen ? onFill : p.onSurface);
        canvas_.Text(labels[index], Inset(button, kLabelPadding, 0.0f), style, Mix(p.onSurfaceVariant, onFill, color),
                     TextAlign::Center);
        AddHit({button, id, [select, index] { select(static_cast<int>(index)); }});
    }
}

void Panel::DrawSwitchRow(uint32_t id, float top, const wchar_t* label, bool on, std::function<void()> toggle) {
    const Palette& p = P();
    const float switchLeft = kWidth - kInset - design::kSwitchWidth;
    DrawRowLabel(label, {kInset, top, switchLeft - design::kSpace3, top + kRowHeight}, p.onSurface);
    const float trackTop = top + (kRowHeight - design::kSwitchHeight) / 2.0f;
    DrawSwitch({switchLeft, trackTop, kWidth - kInset, trackTop + design::kSwitchHeight}, on, id);
    AddHit({TileRect(top, kRowHeight), id, std::move(toggle)});
}

void Panel::DrawSwitch(const D2D1_RECT_F& track, bool on, uint32_t id) {
    const Palette& p = P();
    const float position = Animate(AnimKey(id, 1), on ? 1.0f : 0.0f, design::kSpringFastSpatial);
    const float t = Animate(AnimKey(id, kSelectChannel), on ? 1.0f : 0.0f, design::kSpringDefaultEffects);
    const float radius = (track.bottom - track.top) / 2.0f;
    canvas_.FillRound(track, radius, Mix(p.surfaceContainerHighest, p.primary, t));
    if (t < 0.999f) {
        canvas_.StrokeRound(Inset(track, 1.0f, 1.0f), radius - 1.0f, Alpha(p.outline, 1.0f - t), 2.0f);
    }
    // The handle grows when on and grows further while pressed; on, it carries a check.
    const bool pressed = pressedId_ == id && hotId_ == id;
    const float size = Animate(AnimKey(id, kPressChannel), pressed ? 14.0f : on ? 12.0f : 8.0f,
                               design::kSpringFastSpatial);
    const float travel = track.right - track.left - 2.0f * radius;
    const D2D1_POINT_2F center{track.left + radius + position * travel, (track.top + track.bottom) / 2.0f};
    canvas_.FillCircle(center, size, Mix(p.outline, p.onPrimary, t));
    if (t > 0.05f) {
        canvas_.Glyph(kGlyphCheck, center, 11.0f, Alpha(p.primary, t));
    }
}

D2D1_RECT_F Panel::TileRect(float top, float height) const {
    return {kGutter, top, kWidth - kGutter, top + height};
}

void Panel::DrawRowLabel(std::wstring_view text, const D2D1_RECT_F& rect, D2D1_COLOR_F color) {
    if (canvas_.Measure(text, TextStyle::BodyLarge) <= rect.right - rect.left) {
        canvas_.Text(text, rect, TextStyle::BodyLarge, color);
    } else {
        canvas_.Text(text, rect, TextStyle::BodyMedium, color, TextAlign::Leading, true);
    }
}

void Panel::DrawNumberControl(uint32_t id, float right, float centerY, const std::wstring& value, bool canDecrease,
                              bool canIncrease, std::function<void()> decrease, std::function<void()> increase,
                              std::function<bool(const std::wstring&)> edit) {
    // − [value] +: the buttons step, and the value can be typed.
    const Palette& p = P();
    const D2D1_COLOR_F disabled = Alpha(p.onSurface, design::kDisabledOpacity);
    const float button = design::kSmallIconButtonSize;
    const float fieldWidth = 80.0f;
    const float plusCenter = right - button / 2.0f;
    const float fieldRight = right - button - design::kSpace1;
    const float fieldLeft = fieldRight - fieldWidth;
    const float minusCenter = fieldLeft - design::kSpace1 - button / 2.0f;
    auto stepButton = [&](uint32_t part, float x, wchar_t glyph, bool enabled, std::function<void()> action) {
        const D2D1_RECT_F rect{x - button / 2.0f, centerY - button / 2.0f, x + button / 2.0f, centerY + button / 2.0f};
        canvas_.FillRound(rect, button / 2.0f, Alpha(p.surfaceContainerHighest, enabled ? 1.0f : 0.5f));
        if (enabled) {
            StateLayer(PartId(id, part), rect, button / 2.0f, p.onSurface);
        }
        canvas_.Glyph(glyph, {x, centerY}, design::kSmallIconSize, enabled ? p.onSurface : disabled);
        AddHit({rect, PartId(id, part), enabled ? std::move(action) : std::function<void()>([] {})});
    };
    stepButton(1, minusCenter, kGlyphRemove, canDecrease, std::move(decrease));
    const D2D1_RECT_F field{fieldLeft, centerY - design::kFieldHeight / 2.0f, fieldRight,
                            centerY + design::kFieldHeight / 2.0f};
    if (edit) {
        DrawEditableValue(PartId(id, 4), field, value, std::move(edit));
    } else {
        canvas_.Text(value, field, TextStyle::LabelLarge, p.onSurface, TextAlign::Center);
    }
    stepButton(2, plusCenter, kGlyphAdd, canIncrease, std::move(increase));
}

float Panel::NumberControlWidth() {
    return 2.0f * design::kSmallIconButtonSize + 80.0f + 2.0f * design::kSpace1;
}

D2D1_RECT_F Panel::DrawDropdown(uint32_t id, float right, float centerY, const std::wstring& value, float maxWidth,
                                std::function<void(const D2D1_RECT_F&)> open) {
    // A tonal field with the value and a down arrow; it opens the menu of choices below itself.
    const Palette& p = P();
    const bool opened = menu_.open && menu_.ownerId == id;
    const float textWidth = std::min(canvas_.Measure(value, TextStyle::LabelLarge), maxWidth - 44.0f);
    const float width = textWidth + 44.0f;
    const D2D1_RECT_F rect{right - width, centerY - design::kFieldHeight / 2.0f, right,
                           centerY + design::kFieldHeight / 2.0f};
    canvas_.FillRound(rect, design::kRadiusField, opened ? p.secondaryContainer : p.surfaceContainerHighest);
    StateLayer(id, rect, design::kRadiusField, p.onSurface);
    canvas_.Text(value,
                 {rect.left + design::kSpace3, rect.top, rect.left + design::kSpace3 + textWidth + 1.0f, rect.bottom},
                 TextStyle::LabelLarge, opened ? p.onSecondaryContainer : p.onSurface);
    const float turn = Animate(AnimKey(id, kSelectChannel), opened ? 1.0f : 0.0f, design::kSpringFastSpatial);
    canvas_.Glyph(turn > 0.5f ? kGlyphChevronUp : kGlyphChevronDown, {rect.right - 16.0f, centerY}, 10.0f,
                  p.onSurfaceVariant);
    AddHit({rect, id, [this, open, rect, id] {
                open(rect);
                menu_.ownerId = id;
            }});
    return rect;
}

void Panel::OpenMenu(const D2D1_RECT_F& anchor, std::vector<MenuItem> items, int selected,
                     std::function<void(int)> choose, bool searchable) {
    menu_.open = true;
    menu_.anchor = anchor;
    menu_.items = std::move(items);
    menu_.selected = selected;
    menu_.choose = std::move(choose);
    menu_.scroll = -1.0f;
    menu_.searchable = searchable;
    menu_.search.clear();
    animations_.erase(AnimKey(kIdMenu, 0));
    animations_.erase(AnimKey(kIdMenu, 1));
    hotId_ = pressedId_ = 0;
    Invalidate();
}

void Panel::CloseMenu() {
    if (menu_.open) {
        menu_ = {};
        hotId_ = pressedId_ = 0;
        Invalidate();
    }
}

namespace {

// Ctrl+Backspace: the last word of a text and the spaces after it.
void DeleteLastWord(std::wstring& text) {
    while (!text.empty() && std::iswspace(text.back())) {
        text.pop_back();
    }
    while (!text.empty() && !std::iswspace(text.back())) {
        text.pop_back();
    }
}

}  // namespace

std::vector<size_t> Panel::MenuMatches() const {
    std::vector<size_t> matches;
    auto lower = [](std::wstring text) {
        for (wchar_t& c : text) {
            c = static_cast<wchar_t>(std::towlower(c));
        }
        return text;
    };
    const std::wstring search = lower(menu_.search);
    size_t title = SIZE_MAX;  // The section title not yet shown above a match.
    for (size_t i = 0; i < menu_.items.size(); ++i) {
        const MenuItem& item = menu_.items[i];
        if (search.empty()) {
            matches.push_back(i);
        } else if (item.header) {
            title = i;
        } else if (lower(item.text).find(search) != std::wstring::npos) {
            if (title != SIZE_MAX) {
                matches.push_back(title);
                title = SIZE_MAX;
            }
            matches.push_back(i);
        }
    }
    return matches;
}

void Panel::DrawMenu() {
    if (!menu_.open) {
        return;
    }
    const Palette& p = P();
    constexpr float kItemHeight = 40.0f;
    constexpr float kTitleHeight = 32.0f;
    constexpr float kPadding = 8.0f;
    constexpr float kMargin = 12.0f;
    constexpr float kSearchHeight = 40.0f;  // The search row of a searchable menu, above the list.

    // Everything outside the menu only closes it.
    Hit scrim;
    scrim.rect = {0.0f, 0.0f, kWidth, kHeight};
    scrim.id = kIdMenu;
    scrim.click = [this] { CloseMenu(); };
    scrim.wheel = [](int) {};
    AddHit(std::move(scrim));

    // The menu keeps the size of its whole list while a search narrows it, so it does not jump while typing.
    const float searchRow = menu_.searchable ? kSearchHeight : 0.0f;
    float full = 2.0f * kPadding + searchRow;
    for (const MenuItem& item : menu_.items) {
        full += item.header ? kTitleHeight : kItemHeight;
    }
    const std::vector<size_t> matches = MenuMatches();
    float content = 2.0f * kPadding;
    for (const size_t i : matches) {
        content += menu_.items[i].header ? kTitleHeight : kItemHeight;
    }
    if (matches.empty()) {
        content += kItemHeight;  // "Nothing found".
    }
    const float width = std::clamp(menu_.anchor.right - menu_.anchor.left, 220.0f, kWidth - 2.0f * kMargin);
    const float left = std::clamp(menu_.anchor.left, kMargin, kWidth - kMargin - width);
    const float below = kHeight - kMargin - (menu_.anchor.bottom + 4.0f);
    const float above = menu_.anchor.top - 4.0f - kMargin;
    const bool down = below >= std::min(full, 260.0f) || below >= above;
    const float height = std::min(full, down ? below : above);
    const float top = down ? menu_.anchor.bottom + 4.0f : menu_.anchor.top - 4.0f - height;
    const D2D1_RECT_F rect{left, top, left + width, top + height};
    const float listHeight = height - searchRow;

    const float maxScroll = std::max(0.0f, content - listHeight);
    if (menu_.scroll < 0.0f) {
        float offset = kPadding;
        for (const size_t i : matches) {
            if (static_cast<int>(i) >= menu_.selected) {
                break;
            }
            offset += menu_.items[i].header ? kTitleHeight : kItemHeight;
        }
        menu_.scroll = offset - (listHeight - kItemHeight) / 2.0f;
    }
    menu_.scroll = std::clamp(menu_.scroll, 0.0f, maxScroll);
    const float scroll = Animate(AnimKey(kIdMenuList, 0), menu_.scroll, design::kSpringScroll, true);

    // Fades in while it grows from the anchor. The page behind dims, and the menu takes the most
    // raised surface with an outline and a soft shadow, so it never blends into the cards below.
    const float appear = std::clamp(Animate(AnimKey(kIdMenu, 0), 1.0f, design::kSpringDefaultEffects), 0.0f, 1.0f);
    const float grow = Animate(AnimKey(kIdMenu, 1), 1.0f, design::kSpringFastSpatial);
    const float reveal = height * (0.8f + 0.2f * grow);
    const D2D1_RECT_F shown = down ? D2D1_RECT_F{rect.left, rect.top, rect.right, rect.top + reveal}
                                   : D2D1_RECT_F{rect.left, rect.bottom - reveal, rect.right, rect.bottom};
    // The menu stands out by its raised tone over the dimmed page; it needs no shadow.
    canvas_.FillRect({0.0f, 0.0f, kWidth, kHeight},
                     {0.0f, 0.0f, 0.0f, design::kScrimOpacity * appear});
    canvas_.FillRound(shown, design::kRadiusMenu, Alpha(p.surfaceContainerHighest, appear));

    // The search row: a hint until something is typed, then what was typed; a line sets it off from the list.
    D2D1_RECT_F listRect = shown;
    if (menu_.searchable) {
        const float rowTop = down ? shown.top : shown.top + (shown.bottom - shown.top) - height;
        if (rowTop + kSearchHeight <= shown.bottom) {
            const float centerY = rowTop + kSearchHeight / 2.0f;
            const bool typed = !menu_.search.empty();
            canvas_.Glyph(kGlyphSearch, {rect.left + 26.0f, centerY}, design::kSmallIconSize,
                          Alpha(typed ? p.onSurface : p.onSurfaceVariant, appear));
            const D2D1_RECT_F textRect{rect.left + 46.0f, rowTop, rect.right - 16.0f, rowTop + kSearchHeight};
            if (typed) {
                const float end =
                    std::min(textRect.left + canvas_.Measure(menu_.search, TextStyle::BodyLarge) + 1.0f, textRect.right);
                if (menu_.searchSelected) {
                    canvas_.FillRound({textRect.left - 2.0f, centerY - 11.0f, end + 2.0f, centerY + 11.0f},
                                      design::kRadiusExtraSmall, Alpha(p.primary, 0.3f * appear));
                }
                canvas_.Text(menu_.search, textRect, TextStyle::BodyLarge, Alpha(p.onSurface, appear));
                if (!menu_.searchSelected) {
                    canvas_.FillRect({end, centerY - 9.0f, end + 1.5f, centerY + 9.0f}, Alpha(p.primary, appear));
                }
            } else {
                canvas_.Text(T(Text::MenuSearchHint), textRect, TextStyle::BodySmall,
                             Alpha(p.onSurfaceVariant, appear));
            }
            canvas_.FillRect({rect.left + 12.0f, rowTop + kSearchHeight - 0.5f, rect.right - 12.0f,
                              rowTop + kSearchHeight + 0.5f},
                             Alpha(p.outlineVariant, appear));
        }
        listRect.top = std::min(shown.bottom, std::max(shown.top, rowTop + kSearchHeight));
    }

    Hit list;
    list.rect = shown;
    list.id = kIdMenuList;
    list.click = [] {};
    // Three items a notch, as Windows scrolls lists.
    list.wheel = [this](int notches) {
        menu_.scroll -= notches * 3.0f * kItemHeight;
        Invalidate();
    };
    AddHit(std::move(list));

    canvas_.PushClip(listRect);
    hitClip_.push_back(listRect);
    float y = rect.top + searchRow + kPadding - scroll;
    if (matches.empty()) {
        canvas_.Text(T(Text::MenuNoMatches), {rect.left + 18.0f, y, rect.right - 18.0f, y + kItemHeight},
                     TextStyle::BodyMedium, Alpha(p.onSurfaceVariant, appear));
    }
    for (const size_t i : matches) {
        const MenuItem& item = menu_.items[i];
        // Only what shows is drawn, so long lists (hundreds of game presets) stay smooth.
        const float itemHeight = item.header ? kTitleHeight : kItemHeight;
        if (y + itemHeight <= listRect.top || y >= listRect.bottom) {
            y += itemHeight;
            continue;
        }
        if (item.header) {
            canvas_.Text(item.text, {rect.left + 16.0f, y + 6.0f, rect.right - 16.0f, y + kTitleHeight},
                         TextStyle::LabelMedium, Alpha(p.primary, appear));
            y += kTitleHeight;
            continue;
        }
        const D2D1_RECT_F row{rect.left + 6.0f, y, rect.right - 6.0f, y + kItemHeight};
        const uint32_t id = PartId(kIdMenu, static_cast<uint32_t>(i) + 1);
        const bool chosen = static_cast<int>(i) == menu_.selected;
        if (chosen) {
            canvas_.FillRound(row, design::kRadiusMenuItem, Alpha(p.secondaryContainer, appear));
        }
        StateLayer(id, row, design::kRadiusMenuItem, p.onSurface);
        const D2D1_COLOR_F color = chosen ? p.onSecondaryContainer : item.action ? p.primary : p.onSurface;
        if (item.action) {
            canvas_.FillRect({rect.left + 12.0f, y - 0.5f, rect.right - 12.0f, y + 0.5f},
                             Alpha(p.outlineVariant, appear));
        }
        const float textLeft = item.glyph ? row.left + 44.0f : row.left + 12.0f;
        if (item.glyph) {
            canvas_.Glyph(item.glyph, {row.left + 22.0f, y + kItemHeight / 2.0f}, design::kIconSize - 2.0f,
                          Alpha(color, appear));
        }
        canvas_.Text(item.text, {textLeft, y, row.right - (chosen || item.action ? 36.0f : 12.0f), y + kItemHeight},
                     TextStyle::BodyLarge, Alpha(color, appear));
        if (chosen) {
            canvas_.Glyph(kGlyphCheck, {row.right - 20.0f, y + kItemHeight / 2.0f}, 14.0f, Alpha(color, appear));
        } else if (item.action) {
            canvas_.Glyph(kGlyphChevronRight, {row.right - 20.0f, y + kItemHeight / 2.0f}, design::kSmallIconSize,
                          Alpha(color, appear));
        }
        if (row.bottom > listRect.top && row.top < listRect.bottom) {
            AddHit({row, id, [this, i] {
                        std::function<void(int)> choose = std::move(menu_.choose);
                        CloseMenu();
                        if (choose) {
                            choose(static_cast<int>(i));
                        }
                    }});
        }
        y += kItemHeight;
    }
    hitClip_.pop_back();
    canvas_.PopClip();
    if (maxScroll > 0.0f) {
        DrawScrollbar(listRect, scroll, content, kIdMenuScrollbar, [this](float offset) {
            menu_.scroll = offset;
            SetAnimation(AnimKey(kIdMenuList, 0), offset);
        });
    }
}

float Panel::NavRowHeight(wchar_t glyph, const std::wstring& subtitle, float trailing) {
    if (subtitle.empty()) {
        return kRowHeight;
    }
    const float width = design::kTrailingIconCenter - design::kSpace4 - trailing -
                        (glyph ? design::kLeadingTextInset : kInset);
    if (canvas_.Measure(subtitle, TextStyle::BodySmall) <= width) {
        return design::kTwoLineRowHeight;
    }
    // A subtitle that does not fit on its line wraps, and the row grows by the extra lines.
    const float lines = std::ceil(canvas_.MeasureHeight(subtitle, TextStyle::BodySmall, width));
    return std::max(design::kTwoLineRowHeight, design::kSubtitleTop + lines + design::kHintBottomGap);
}

float Panel::DrawNavRow(uint32_t id, float top, wchar_t glyph, D2D1_COLOR_F glyphColor, const std::wstring& title,
                        const std::wstring& subtitle, std::function<void()> click, float trailing, bool wrap) {
    const Palette& p = P();
    const float height = wrap ? NavRowHeight(glyph, subtitle, trailing)
                              : subtitle.empty() ? kRowHeight
                                                 : design::kTwoLineRowHeight;
    const float textLeft = glyph ? design::kLeadingTextInset : kInset;
    const float textRight = design::kTrailingIconCenter - design::kSpace4 - trailing;
    if (glyph) {
        canvas_.Glyph(glyph, {design::kLeadingIconCenter, top + height / 2.0f}, design::kIconSize - 2.0f, glyphColor);
    }
    if (subtitle.empty()) {
        canvas_.Text(title, {textLeft, top, textRight, top + height}, TextStyle::BodyLarge, p.onSurface);
    } else {
        const bool wrapped = height > design::kTwoLineRowHeight;
        canvas_.Text(title, {textLeft, top + 9.0f, textRight, top + design::kSubtitleTop}, TextStyle::BodyLarge,
                     p.onSurface);
        canvas_.Text(subtitle,
                     {textLeft, top + design::kSubtitleTop, textRight,
                      wrapped ? top + height - design::kHintBottomGap : top + 50.0f},
                     TextStyle::BodySmall, p.onSurfaceVariant, TextAlign::Leading, wrapped);
    }
    canvas_.Glyph(kGlyphChevronRight, {design::kTrailingIconCenter, top + height / 2.0f}, design::kSmallIconSize,
                  p.onSurfaceVariant);
    AddHit({TileRect(top, height), id, std::move(click)});
    return top + height;
}

void Panel::DrawHint(const wchar_t* text, float top, float height) {
    // height comes from HintHeight and includes the gap below the text.
    canvas_.Text(text, {kInset, top, kWidth - kInset, top + height - design::kHintBottomGap}, TextStyle::BodySmall,
                 P().onSurfaceVariant, TextAlign::Leading, true);
}

float Panel::HintHeight(const wchar_t* text, float width) {
    // The default width matches DrawHint; the height includes the gap to the tile's edge.
    const float wrap = width > 0.0f ? width : kWidth - 2.0f * kInset;
    return std::ceil(canvas_.MeasureHeight(text, TextStyle::BodySmall, wrap)) + design::kHintBottomGap;
}

void Panel::DrawScrollbar(const D2D1_RECT_F& area, float offset, float content, uint32_t id,
                          std::function<void(float)> scrollTo) {
    const float visible = area.bottom - area.top;
    if (content <= visible + 0.5f) {
        return;
    }
    const float trackTop = area.top + 14.0f;
    const float trackHeight = area.bottom - 14.0f - trackTop;
    const float thumbHeight = std::max(24.0f, trackHeight * visible / content);
    const float range = content - visible;
    const float thumbTop = trackTop + (trackHeight - thumbHeight) * std::clamp(offset / range, 0.0f, 1.0f);
    // The thumb widens under the pointer and while dragged, so it is easy to catch.
    const bool draggable = id != 0 && static_cast<bool>(scrollTo);
    const float wide = draggable ? Animate(AnimKey(id, 0), hotId_ == id || pressedId_ == id ? 1.0f : 0.0f,
                                           design::kSpringFastSpatial)
                                 : 0.0f;
    const float width = 3.0f + 3.0f * std::clamp(wide, 0.0f, 1.0f);
    canvas_.FillRound({area.right - 4.0f - width, thumbTop, area.right - 4.0f, thumbTop + thumbHeight}, width / 2.0f,
                      Alpha(P().onSurfaceVariant, 0.45f + 0.25f * std::clamp(wide, 0.0f, 1.0f)));
    if (!draggable) {
        return;
    }
    Hit bar;
    bar.rect = {area.right - 14.0f, trackTop, area.right, trackTop + trackHeight};
    bar.id = id;
    bar.drag = [this, trackTop, trackHeight, thumbTop, thumbHeight, range, scrollTo](float, float y) {
        // A press on the thumb keeps the point it was caught at; a press beside it centers the thumb there.
        if (pressStarting_) {
            scrollGrab_ = y >= thumbTop && y <= thumbTop + thumbHeight ? y - thumbTop : thumbHeight / 2.0f;
        }
        const float travel = std::max(1.0f, trackHeight - thumbHeight);
        scrollTo(std::clamp((y - scrollGrab_ - trackTop) / travel, 0.0f, 1.0f) * range);
        Invalidate();
    };
    AddHit(std::move(bar));
}

void Panel::DrawList(const D2D1_RECT_F& list, uint32_t id, float& scroll, size_t count, float rowHeight,
                     const std::function<uint32_t(size_t)>& idOf, const std::function<void(size_t, float)>& drawRow) {
    // A scrolling segmented list: one tile per row.
    const float pitch = rowHeight + design::kSegmentGap;
    const float visible = list.bottom - list.top;
    const float content = count * pitch - design::kSegmentGap;
    const float maxScroll = std::max(0.0f, content - visible);
    scroll = std::clamp(scroll, 0.0f, maxScroll);
    const float offset = Animate(AnimKey(id, 0), scroll, design::kSpringScroll, true);

    Hit wheel;
    wheel.rect = list;
    wheel.id = id;
    wheel.wheel = [this, &scroll, maxScroll, pitch](int notches) {
        scroll = std::clamp(scroll - notches * pitch, 0.0f, maxScroll);
        Invalidate();
    };
    AddHit(std::move(wheel));

    canvas_.PushRoundClip(list, design::kRadiusGroup);
    hitClip_.push_back(list);
    for (size_t index = 0; index < count; ++index) {
        const float top = list.top + index * pitch - offset;
        if (top + rowHeight >= list.top && top <= list.bottom) {
            DrawTile(TileRect(top, rowHeight), idOf(index), index == 0, index + 1 == count);
            drawRow(index, top);
        }
    }
    hitClip_.pop_back();
    canvas_.PopRoundClip();
    DrawScrollbar(list, offset, content);
}

void Panel::DrawSnackbar() {
    if (warning_.empty() || TabOf(page_) != Page::Mixer) {
        return;
    }
    const Palette& p = P();
    const float bottom = ContentBottom() - 12.0f;
    const D2D1_RECT_F bar{16.0f, bottom - 56.0f, kWidth - 16.0f, bottom};
    canvas_.FillRound({bar.left, bar.top + 2.0f, bar.right, bar.bottom + 2.0f}, 8.0f, {0.0f, 0.0f, 0.0f, 0.18f});
    canvas_.FillRound(bar, 8.0f, p.inverseSurface);
    // Swallows clicks so the list underneath does not react through the bar.
    AddHit({bar, kIdSnackbar, [] {}});
    canvas_.Text(warning_, {bar.left + 16.0f, bar.top, bar.right - 96.0f, bar.bottom}, TextStyle::BodyMedium,
                 p.inverseOnSurface);
    const D2D1_RECT_F action{bar.right - 88.0f, bar.top + 10.0f, bar.right - 8.0f, bar.bottom - 10.0f};
    StateLayer(kIdSnackbarAction, action, 18.0f, p.inversePrimary);
    canvas_.Text(T(Text::Dismiss), action, TextStyle::LabelLarge, p.inversePrimary, TextAlign::Center);
    AddHit({action, kIdSnackbarAction, [this] { host_.OnWarningDismissed(); }});
}

std::vector<AppItem> Panel::FilteredRunningApps() const {
    if (filter_.empty()) {
        return running_;
    }
    const std::wstring needle = ToLower(filter_);
    std::vector<AppItem> result;
    for (const auto& app : running_) {
        if (ToLower(app.name).find(needle) != std::wstring::npos ||
            ToLower(app.exe).find(needle) != std::wstring::npos) {
            result.push_back(app);
        }
    }
    return result;
}

ID2D1Bitmap* Panel::IconFor(const std::wstring& path) {
    const std::wstring key = ToLower(path);
    const auto found = icons_.find(key);
    if (found != icons_.end()) {
        return found->second.Get();
    }
    ComPtr<ID2D1Bitmap> bitmap;
    HICON icon = nullptr;
    if (ExtractIconExW(path.c_str(), 0, &icon, nullptr, 1) > 0 && icon) {
        bitmap = canvas_.BitmapFromIcon(icon);
        DestroyIcon(icon);
    }
    // Failed lookups are cached as null so missing files are not probed every frame.
    return icons_.emplace(key, bitmap).first->second.Get();
}

const Panel::Hit* Panel::HitAt(float x, float y, bool wheel) const {
    for (auto it = hits_.rbegin(); it != hits_.rend(); ++it) {
        if (!Contains(it->rect, x, y)) {
            continue;
        }
        if (wheel ? static_cast<bool>(it->wheel) : (it->click || it->drag || it->doubleClick)) {
            return &*it;
        }
    }
    return nullptr;
}

void Panel::OnMouseMove(float x, float y) {
    if (!trackingMouse_) {
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window_, 0};
        TrackMouseEvent(&track);
        trackingMouse_ = true;
    }
    if (pressedId_) {
        for (const auto& hit : hits_) {
            if (hit.id == pressedId_ && hit.drag) {
                const auto drag = hit.drag;
                drag(x, y);
                break;
            }
        }
    }
    const Hit* hit = HitAt(x, y, false);
    const uint32_t hot = hit ? hit->id : 0;
    if (hot != hotId_) {
        hotId_ = hot;
        ArmTooltip(hit);
        Invalidate();
    }
}

void Panel::OnMouseDown(float x, float y, bool doubleClick) {
    const Hit* hit = HitAt(x, y, false);
    ArmTooltip(nullptr);
    // A press anywhere but on the field being edited applies its text.
    if (field_.id && (!hit || hit->id != field_.id)) {
        CommitField();
        hit = HitAt(x, y, false);
    }
    if (!hit) {
        return;
    }
    if (doubleClick && hit->doubleClick) {
        const auto action = hit->doubleClick;
        action();
        return;
    }
    pressedId_ = hotId_ = hit->id;
    focusId_ = hit->id;
    focusVisible_ = false;
    SetCapture(window_);
    if (hit->drag) {
        const auto drag = hit->drag;
        pressStarting_ = true;
        drag(x, y);
        pressStarting_ = false;
    }
    Invalidate();
}

void Panel::OnMouseUp(float x, float y) {
    if (GetCapture() == window_) {
        ReleaseCapture();
    }
    const uint32_t pressed = pressedId_;
    pressedId_ = 0;
    dragId_ = 0;
    std::function<void()> click;
    std::function<void()> release;
    for (const auto& hit : hits_) {
        if (hit.id == pressed && hit.release) {
            release = hit.release;
            break;
        }
    }
    if (const Hit* hit = HitAt(x, y, false); hit && hit->id == pressed && hit->click) {
        click = hit->click;
    }
    Invalidate();
    // The actions may close the panel, so they run last.
    if (release) {
        release();
    }
    if (click) {
        click();
    }
}

void Panel::OnWheel(float x, float y, int delta) {
    wheelRemainder_ += delta;
    const int notches = wheelRemainder_ / WHEEL_DELTA;
    if (notches == 0) {
        return;
    }
    wheelRemainder_ -= notches * WHEEL_DELTA;
    if (const Hit* hit = HitAt(x, y, true)) {
        const auto wheel = hit->wheel;
        wheel(notches);
    }
}

bool Panel::OnKey(UINT key, bool down) {
    if (recording_ >= 0) {
        if (down) {
            const UINT modifiers = HeldModifiers();
            if (key == VK_ESCAPE && modifiers == 0) {
                StopRecording();
            } else if ((key == VK_BACK || key == VK_DELETE) && modifiers == 0) {
                const int action = recording_;
                StopRecording();
                host_.OnHotkeyChanged(action, {});
            } else if (IsModifierKey(key)) {
                heldModifiers_ = modifiers;
            } else {
                const int action = recording_;
                StopRecording();
                host_.OnHotkeyChanged(action, {modifiers, key});
            }
        } else {
            heldModifiers_ = HeldModifiers();
        }
        Invalidate();
        return true;
    }

    if (!down) {
        return false;
    }
    if (field_.id) {
        return FieldKey(key);
    }
    if (menu_.open && menu_.searchable && (GetKeyState(VK_CONTROL) & 0x8000) &&
        (key == 'A' || key == 'C' || key == 'X' || key == 'V')) {
        // The search is selected whole or not at all: Ctrl+A selects it, Ctrl+C and Ctrl+X take it while
        // selected, Ctrl+V puts the clipboard's text in its place or after it.
        if (key == 'A') {
            menu_.searchSelected = !menu_.search.empty();
        } else if (key == 'V') {
            const std::wstring pasted = ClipboardText(window_);
            if (!pasted.empty()) {
                if (menu_.searchSelected) {
                    menu_.search.clear();
                    menu_.searchSelected = false;
                }
                menu_.search += pasted;
                menu_.scroll = 0.0f;
                SetAnimation(AnimKey(kIdMenuList, 0), 0.0f);
            }
        } else if (menu_.searchSelected) {
            SetClipboardText(window_, menu_.search);
            if (key == 'X') {
                menu_.search.clear();
                menu_.searchSelected = false;
                menu_.scroll = 0.0f;
            }
        }
        Invalidate();
        return true;
    }
    if (key == VK_ESCAPE && menu_.open) {
        if (!menu_.search.empty()) {
            menu_.searchSelected = false;
            menu_.search.clear();
            menu_.scroll = 0.0f;
            Invalidate();
        } else {
            CloseMenu();
        }
        return true;
    }
    if (key == VK_RETURN && menu_.open && !menu_.search.empty() && !ActivateFocus()) {
        // Without a focused item, Enter takes the first match.
        for (const size_t i : MenuMatches()) {
            if (!menu_.items[i].header) {
                std::function<void(int)> choose = std::move(menu_.choose);
                CloseMenu();
                if (choose) {
                    choose(static_cast<int>(i));
                }
                break;
            }
        }
        return true;
    }
    if (key == VK_ESCAPE) {
        if (page_ == Page::AddApp && !filter_.empty()) {
            filter_.clear();
            Invalidate();
        } else if (ParentOf(page_) != page_) {
            GoBack();
        } else if (page_ != Page::Mixer) {
            GoTo(Page::Mixer);
        } else {
            Hide();
        }
        return true;
    }
    if (key == VK_TAB) {
        MoveFocus(GetKeyState(VK_SHIFT) < 0 ? -1 : 1);
        return true;
    }
    // Space types into the pages with a text field; Enter activates everywhere.
    const bool typing = page_ == Page::AddApp || page_ == Page::ColorPicker || (menu_.open && menu_.searchable);
    if ((key == VK_RETURN || (key == VK_SPACE && !typing)) && ActivateFocus()) {
        return true;
    }
    if ((key == VK_LEFT || key == VK_DOWN || key == VK_RIGHT || key == VK_UP) &&
        StepFocus(key == VK_RIGHT || key == VK_UP ? 1 : -1)) {
        return true;
    }
    if (page_ == Page::Mixer && (key == VK_LEFT || key == VK_RIGHT)) {
        // Arrow keys move by one percent without a headset dial, for fine adjustments.
        NudgeBalance(key == VK_RIGHT ? 1 : -1, kBalanceRange);
        return true;
    }
    return false;
}

void Panel::OnChar(wchar_t ch) {
    if (recording_ >= 0) {
        return;
    }
    if (field_.id) {
        FieldChar(ch);
        return;
    }
    if (menu_.open) {
        if (!menu_.searchable) {
            return;
        }
        // Backspace, Ctrl+Backspace (0x7F) and typing replace a search selected with Ctrl+A.
        const bool printable = ch >= L' ' && ch != 0x7F;
        if (menu_.searchSelected && (printable || ch == L'\b' || ch == 0x7F)) {
            menu_.search.clear();
            menu_.searchSelected = false;
            if (!printable) {
                ch = 0;
            }
        }
        if (ch == L'\b') {
            if (menu_.search.empty()) {
                return;
            }
            menu_.search.pop_back();
        } else if (ch == 0x7F) {
            DeleteLastWord(menu_.search);
        } else if (printable && !(ch == L' ' && menu_.search.empty())) {
            menu_.search += ch;
        } else if (ch != 0) {
            return;
        }
        // The narrowed list starts from its top.
        menu_.scroll = 0.0f;
        SetAnimation(AnimKey(kIdMenuList, 0), 0.0f);
        Invalidate();
        return;
    }
    std::wstring* field = page_ == Page::AddApp ? &filter_ : page_ == Page::ColorPicker ? &hexInput_ : nullptr;
    if (!field) {
        return;
    }
    if (ch == L'\b') {
        if (!field->empty()) {
            field->pop_back();
        }
    } else if (ch >= L' ' && ch != 0x7F) {
        if (page_ == Page::ColorPicker && (field->size() >= 7 || !(std::iswxdigit(ch) || ch == L'#'))) {
            return;
        }
        *field += ch;
    } else {
        return;
    }
    if (page_ == Page::AddApp) {
        addScroll_ = 0.0f;
    } else {
        // A complete code applies right away.
        std::wstring digits = hexInput_;
        if (!digits.empty() && digits[0] == L'#') {
            digits.erase(0, 1);
        }
        if (digits.size() == 6) {
            const uint32_t rgb = static_cast<uint32_t>(wcstoul(digits.c_str(), nullptr, 16)) & 0xFFFFFF;
            HueChromaOf(rgb, pickerHue_, pickerChroma_);
            SetPickedColor(rgb);
        }
    }
    Invalidate();
}

void Panel::StartRecording(int action) {
    if (recording_ < 0) {
        host_.OnHotkeyRecording(true);
    }
    recording_ = action;
    heldModifiers_ = 0;
}

void Panel::StopRecording() {
    if (recording_ < 0) {
        return;
    }
    recording_ = -1;
    heldModifiers_ = 0;
    host_.OnHotkeyRecording(false);
}

void Panel::Edit(const std::function<void(Settings&)>& edit) {
    // Applied locally first so the panel reacts at once; the host sends the result back.
    edit(settings_);
    host_.OnSettingsEdit(edit);
    Invalidate();
}

void Panel::RunFileDialog(FileDialog kind) {
    if (browsing_ || !window_) {
        return;
    }
    struct Request {
        FileDialog kind;
        std::wstring title;
        std::wstring filterName;
        std::wstring pattern;
        std::wstring folder;
        std::wstring fileName;
        bool save = false;
        bool json = false;
        std::wstring extension;  // Added to a typed name without one.
    };
    Request request{kind};
    switch (kind) {
    case FileDialog::BrowseExe:
        request.title = T(Text::BrowseTitle);
        request.filterName = T(Text::BrowseFilter);
        request.pattern = L"*.exe";
        break;
    case FileDialog::AddOn:
        request.title = T(Text::AddOnBrowseTitle);
        request.filterName = T(Text::BrowseFilter);
        request.pattern = L"*.exe";
        break;
    case FileDialog::AddOnImage:
        request.title = T(Text::IdleImageTitle);
        request.filterName = T(Text::ImageFilter);
        request.pattern = L"*.gif;*.png;*.jpg;*.jpeg;*.bmp";
        break;
    case FileDialog::ImportPreset:
    case FileDialog::ImportEqPreset:
        request.title = T(Text::ImportPresetTitle);
        request.filterName = T(Text::PresetFilter);
        request.pattern = L"*.json";
        request.json = true;
        break;
    case FileDialog::SavePreset:
    case FileDialog::SaveEqPreset:
        request.title = T(Text::SavePresetTitle);
        request.filterName = T(Text::PresetFilter);
        request.pattern = L"*.json";
        request.folder = kind == FileDialog::SavePreset ? PresetDirectory() : ParametricPresetDirectory();
        request.fileName = std::wstring(T(Text::DefaultPresetName)) + L".json";
        request.save = true;
        request.json = true;
        break;
    case FileDialog::IdleImage:
        request.title = T(Text::IdleImageTitle);
        request.filterName = T(Text::ImageFilter);
        request.pattern = L"*.gif;*.png;*.jpg;*.jpeg;*.bmp";
        break;
    case FileDialog::ExportSettings: {
        request.title = T(Text::SettingsExportTitle);
        request.filterName = T(Text::SettingsFilter);
        request.pattern = L"*.zip";
        SYSTEMTIME now{};
        GetLocalTime(&now);
        wchar_t date[16];
        swprintf_s(date, L" %04u-%02u-%02u", now.wYear, now.wMonth, now.wDay);
        request.fileName = std::wstring(T(Text::SettingsFilter)) + date + L".zip";
        request.extension = L"zip";
        request.save = true;
        break;
    }
    case FileDialog::ImportSettings:
        request.title = T(Text::SettingsImportTitle);
        request.filterName = T(Text::SettingsFilter);
        request.pattern = L"*.zip";
        break;
    case FileDialog::Plugin: {
        request.title = T(Text::PluginTitle);
        request.filterName = T(Text::PluginFilter);
        request.pattern = L"*.vst3;*.dll";
        // Where installers put VST 3 plugins; a bundle folder is opened to pick the .vst3 inside.
        wchar_t common[MAX_PATH] = {};
        if (GetEnvironmentVariableW(L"CommonProgramFiles", common, MAX_PATH)) {
            request.folder = std::wstring(common) + L"\\VST3";
        }
        break;
    }
    }
    browsing_ = true;
    // The shell file dialogs need an STA thread; the UI thread runs in the MTA for audio notifications.
    std::thread([owner = window_, request = std::move(request)] {
        const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        std::unique_ptr<std::wstring> result;
        ComPtr<IFileDialog> dialog;
        const CLSID type = request.save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog;
        if (SUCCEEDED(CoCreateInstance(type, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
            const COMDLG_FILTERSPEC filter{request.filterName.c_str(), request.pattern.c_str()};
            dialog->SetFileTypes(1, &filter);
            dialog->SetTitle(request.title.c_str());
            if (request.json) {
                dialog->SetDefaultExtension(L"json");
            } else if (!request.extension.empty()) {
                dialog->SetDefaultExtension(request.extension.c_str());
            }
            ComPtr<IShellItem> folder;
            if (!request.folder.empty() &&
                SUCCEEDED(SHCreateItemFromParsingName(request.folder.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
                dialog->SetFolder(folder.Get());
            }
            if (!request.fileName.empty()) {
                dialog->SetFileName(request.fileName.c_str());
            }
            ComPtr<IShellItem> item;
            PWSTR path = nullptr;
            if (SUCCEEDED(dialog->Show(owner)) && SUCCEEDED(dialog->GetResult(&item)) &&
                SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                result = std::make_unique<std::wstring>(path);
                CoTaskMemFree(path);
            }
        }
        dialog.Reset();
        if (PostMessageW(owner, WM_APP_BROWSE_DONE, static_cast<WPARAM>(request.kind),
                         reinterpret_cast<LPARAM>(result.get()))) {
            result.release();
        }
        if (SUCCEEDED(apartment)) {
            CoUninitialize();
        }
    }).detach();
}

void Panel::OnFileDialogDone(FileDialog kind, std::wstring* path) {
    std::unique_ptr<std::wstring> owned(path);
    browsing_ = false;
    if (!owned || owned->empty()) {
        return;
    }
    switch (kind) {
    case FileDialog::BrowseExe: {
        AppItem app;
        app.path = *owned;
        app.exe = FileNameOf(app.path);
        app.name = host_.AppName(app.path);
        if (addTarget_ == AddTarget::Profile) {
            Edit([app](Settings& s) {
                const bool known = std::any_of(s.profiles.begin(), s.profiles.end(), [&](const GameProfile& profile) {
                    return _wcsicmp(profile.exe.c_str(), app.exe.c_str()) == 0;
                });
                if (!known) {
                    s.profiles.push_back({app.exe, app.path});
                }
            });
            profileExe_ = app.exe;
            GoTo(Page::ProfileEdit);
        } else {
            host_.OnGroupChanged(app, addGroup_);
            GoTo(Page::Group);
        }
        break;
    }
    case FileDialog::AddOnImage: {
        const std::wstring chosen = *owned;
        const size_t index = addOnDisplay_;
        EditAddOnScreen([chosen, index](AddOnEventScreen& s) {
            if (index < s.displays.size()) {
                s.displays[index].image = chosen;
            }
        });
        break;
    }
    case FileDialog::AddOn: {
        const std::wstring chosen = *owned;
        Edit([chosen](Settings& s) {
            const bool known = std::any_of(s.addOns.begin(), s.addOns.end(), [&](const AddOnSettings& addOn) {
                return _wcsicmp(addOn.path.c_str(), chosen.c_str()) == 0;
            });
            if (!known) {
                AddOnSettings addOn;
                addOn.path = chosen;
                s.addOns.push_back(std::move(addOn));
            }
        });
        addOnPath_ = chosen;
        GoTo(Page::AddOnEdit);
        break;
    }
    case FileDialog::SavePreset: {
        std::wstring name = FileNameOf(*owned);
        name = name.substr(0, name.find_last_of(L'.'));
        // A parametric headset saves its filters, with the closest curve for headsets with fixed bands.
        if (features_ && features_->equalizer.Parametric()) {
            WritePresetFilters(*owned, name, ActiveHeadsetFilters());
        } else {
            WritePresetFile(*owned, name, ActiveCurve());
        }
        userPresets_ = LoadUserPresets();
        Invalidate();
        break;
    }
    case FileDialog::ImportPreset:
        if (const auto preset = ImportPresetFile(*owned)) {
            userPresets_ = LoadUserPresets();
            if (features_ && features_->equalizer.Parametric()) {
                host_.OnEqualizerFilters(PresetFilters(*preset, features_->equalizer.range));
            } else if (features_) {
                host_.OnEqualizerCurve(PresetCurve(*preset, features_->equalizer.range));
            }
        }
        Invalidate();
        break;
    case FileDialog::SaveEqPreset: {
        std::wstring name = FileNameOf(*owned);
        name = name.substr(0, name.find_last_of(L'.'));
        WriteParametricPreset(*owned, {name, EqChannelState().eq});
        Edit([this, name](Settings& s) {
            EqStateOf(s, eqChannel_).preset = name;
        });
        eqLibrary_[2] = LoadParametricPresets();
        break;
    }
    case FileDialog::ImportEqPreset:
        if (auto preset = ReadParametricPreset(*owned)) {
            // A copy in the presets folder keeps it listed.
            WriteParametricPreset(ParametricPresetDirectory() + L"\\" + FileNameOf(*owned), *preset);
            eqLibrary_[2] = LoadParametricPresets();
            ApplyEqPreset(*preset);
        }
        break;
    case FileDialog::IdleImage:
        host_.OnIdleImage(*owned);
        break;
    case FileDialog::Plugin:
        host_.OnPluginAdd(pluginDialog_, *owned);
        break;
    case FileDialog::ExportSettings:
        transferNote_ = host_.OnSettingsExport(*owned) ? std::wstring(T(Text::SettingsExported)) + L" " + FileNameOf(*owned)
                                                       : T(Text::SettingsExportFailed);
        Invalidate();
        break;
    case FileDialog::ImportSettings:
        // On success the app restarts; the note shows only when the file was refused.
        transferNote_ = host_.OnSettingsImport(*owned) ? L"" : T(Text::SettingsImportFailed);
        Invalidate();
        break;
    }
}

}  // namespace ui
