#include "panel.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <shobjidl.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <memory>
#include <thread>

#include "../app.h"
#include "../app_info.h"
#include "panel_internal.h"

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
    hotId_ = pressedId_ = 0;
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

void Panel::SetStatus(const std::wstring& device, const std::wstring& detail, bool connected,
                      const std::wstring& warning) {
    device_ = device;
    deviceDetail_ = detail;
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
    const bool applied = SUCCEEDED(DwmSetWindowAttribute(window_, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop)));
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
    case WM_ACTIVATE:
        // Deferred so the window is not destroyed in the middle of activation processing.
        if (LOWORD(wParam) == WA_INACTIVE && autoHide_ && !browsing_) {
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

float Panel::Animate(uint64_t key, float target, float speed) {
    const auto [it, inserted] = animations_.try_emplace(key, target);
    float& value = it->second;
    if (inserted || settings_.appearance.reduceMotion) {
        value = target;
        return value;
    }
    value += (target - value) * (1.0f - std::exp(-speed * frameSeconds_));
    if (std::fabs(target - value) < 0.0015f) {
        value = target;
    } else {
        animating_ = true;
    }
    return value;
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

    canvas_.Begin();
    canvas_.Clear({0.0f, 0.0f, 0.0f, 0.0f});
    canvas_.FillRect({0.0f, 0.0f, kWidth, kHeight}, Alpha(P().surface, backdrop_ ? kSurfaceAlpha : 1.0f));
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
    }
    DrawSnackbar();
    DrawNavigationBar();
    DrawMenu();
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
    case Page::Profiles: return Page::Settings;
    case Page::ColorPicker: return Page::Appearance;
    case Page::PickDevice: return Page::Audio;
    case Page::ProfileEdit: return Page::Profiles;
    case Page::Mixer:
    case Page::Equalizer:
    case Page::Headset:
    case Page::Settings: break;
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
    return kHeight - kNavHeight;
}

void Panel::GoTo(Page page) {
    StopRecording();
    CloseMenu();
    if ((page == Page::Headset && !HeadsetTabVisible()) ||
        ((page == Page::Equalizer || page == Page::EqPresets) && !settings_.equalizer)) {
        page = Page::Mixer;
    }
    // Pages opened from their parent or from the navigation bar start at the top.
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
        endpoints_ = host_.PlaybackDevices();
        scroll_[page] = 0.0f;
        animations_.erase(AnimKey(kIdPickList, 0));
        break;
    case Page::ColorPicker: {
        // The picker starts from the color in use.
        const uint32_t seed = settings_.appearance.windowsAccent ? WindowsAccentColor() : settings_.appearance.accent;
        HueChromaOf(seed, pickerHue_, pickerChroma_);
        pickerChroma_ = std::min(pickerChroma_, 0.20f);
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
    GoTo(ParentOf(page_));
}

void Panel::DrawNavigationBar() {
    const Palette& p = P();
    const float top = ContentBottom();
    canvas_.FillRect({0.0f, top, kWidth, kHeight}, Alpha(p.surfaceContainerHighest, backdrop_ ? 0.94f : 1.0f));
    canvas_.FillRect({0.0f, top, kWidth, top + 1.0f}, Alpha(p.outlineVariant, 0.5f));
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

    const Page current = TabOf(page_);
    const float width = kWidth / destinations.size();
    for (size_t i = 0; i < destinations.size(); ++i) {
        const Destination& destination = destinations[i];
        const float center = width * (i + 0.5f);
        const uint32_t id = kIdNavigation + static_cast<uint32_t>(destination.page);
        const bool active = destination.page == current;
        const float t = Animate(AnimKey(id, 0), active ? 1.0f : 0.0f, 20.0f);
        const D2D1_RECT_F pill{center - 14.0f - 18.0f * t, top + 12.0f, center + 14.0f + 18.0f * t, top + 44.0f};
        if (t > 0.01f) {
            canvas_.FillRound(pill, 16.0f, Alpha(p.secondaryContainer, t));
        }
        const D2D1_RECT_F hover{center - 32.0f, top + 12.0f, center + 32.0f, top + 44.0f};
        StateLayer(id, hover, 16.0f, p.onSurface);
        canvas_.Glyph(destination.glyph, {center, top + 28.0f}, 18.0f,
                      active ? p.onSecondaryContainer : p.onSurfaceVariant);
        if (destination.page == Page::Settings && update_.state == UpdateView::State::Available) {
            canvas_.FillCircle({center + 11.0f, top + 19.0f}, 4.0f, p.error);
        }
        canvas_.Text(T(destination.label), {center - width / 2.0f, top + 46.0f, center + width / 2.0f, top + 64.0f},
                     TextStyle::LabelMedium, active ? p.onSurface : p.onSurfaceVariant, TextAlign::Center);
        AddHit({{center - width / 2.0f, top, center + width / 2.0f, kHeight}, id, [this, page = destination.page] {
                    GoTo(page);
                }});
    }
}

void Panel::DrawHeader(const wchar_t* title, const wchar_t* subtitle, float trailing) {
    IconButton(kIdBack, {36.0f, 32.0f}, kGlyphBack, P().onSurface, [this] { GoBack(); });
    const float right = kWidth - 20.0f - trailing;
    if (subtitle && *subtitle) {
        canvas_.Text(title, {64.0f, 10.0f, right, 36.0f}, TextStyle::TitleMedium, P().onSurface);
        canvas_.Text(subtitle, {64.0f, 34.0f, right, 52.0f}, TextStyle::BodySmall, P().onSurfaceVariant);
    } else {
        canvas_.Text(title, {64.0f, 12.0f, right, 52.0f}, TextStyle::TitleLarge, P().onSurface);
    }
}

void Panel::DrawTitle(const wchar_t* title) {
    canvas_.Text(title, {24.0f, 12.0f, kWidth - 20.0f, 52.0f}, TextStyle::TitleLarge, P().onSurface);
}

float Panel::SectionTitle(const wchar_t* title, float y) {
    canvas_.Text(title, {20.0f, y, kWidth - 20.0f, y + 20.0f}, TextStyle::TitleSmall, P().primary);
    return y + 26.0f;
}

D2D1_RECT_F Panel::Card(float top, float height) {
    const D2D1_RECT_F card{16.0f, top, kWidth - 16.0f, top + height};
    canvas_.FillRound(card, kCardRadius, Alpha(P().surfaceContainer, kCardAlpha));
    return card;
}

float Panel::BeginScroll(Page page, float top) {
    const D2D1_RECT_F area{0.0f, top, kWidth, ContentBottom()};
    const uint64_t key = AnimKey(kIdSettingsPage, static_cast<uint32_t>(page));
    const float offset = Animate(key, scroll_[page], 22.0f);
    Hit wheel;
    wheel.rect = area;
    wheel.id = kIdSettingsPage;
    // The handler reads the limit computed at the end of this frame.
    wheel.wheel = [this, page](int notches) {
        scroll_[page] -= notches * kRowHeight;
        Invalidate();
    };
    AddHit(std::move(wheel));
    canvas_.PushClip(area);
    hitClip_.push_back(area);
    return top + 4.0f - offset;
}

void Panel::EndScroll(Page page, float top, float y) {
    hitClip_.pop_back();
    canvas_.PopClip();
    const D2D1_RECT_F area{0.0f, top, kWidth, ContentBottom()};
    const float offset = animations_[AnimKey(kIdSettingsPage, static_cast<uint32_t>(page))];
    const float content = y + offset - top + 12.0f;
    const float maxScroll = std::max(0.0f, content - (area.bottom - area.top));
    float& scroll = scroll_[page];
    if (scroll < 0.0f || scroll > maxScroll) {
        scroll = std::clamp(scroll, 0.0f, maxScroll);
        Invalidate();
    }
    DrawScrollbar(area, offset, content);
}

void Panel::AddHit(Hit hit) {
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

void Panel::StateLayer(uint32_t id, const D2D1_RECT_F& rect, float radius, D2D1_COLOR_F content) {
    const bool hot = hotId_ == id;
    const float target = hot ? (pressedId_ == id ? 0.12f : 0.08f) : 0.0f;
    const float alpha = Animate(AnimKey(id, kHoverChannel), target, 24.0f);
    if (alpha > 0.002f) {
        canvas_.FillRound(rect, radius, Alpha(content, alpha));
    }
}

void Panel::IconButton(uint32_t id, D2D1_POINT_2F center, wchar_t glyph, D2D1_COLOR_F color,
                       std::function<void()> click) {
    const D2D1_RECT_F rect{center.x - 20.0f, center.y - 20.0f, center.x + 20.0f, center.y + 20.0f};
    StateLayer(id, rect, 20.0f, color);
    canvas_.Glyph(glyph, center, 18.0f, color);
    AddHit({rect, id, std::move(click)});
}

void Panel::TextButton(uint32_t id, const D2D1_RECT_F& rect, wchar_t glyph, const wchar_t* label, bool tonal,
                       std::function<void()> click) {
    const Palette& p = P();
    const float radius = (rect.bottom - rect.top) / 2.0f;
    const D2D1_COLOR_F content = tonal ? p.onSecondaryContainer : p.primary;
    if (tonal) {
        canvas_.FillRound(rect, radius, p.secondaryContainer);
    }
    StateLayer(id, rect, radius, content);
    const float total = (glyph ? 18.0f + 8.0f : 0.0f) + canvas_.Measure(label, TextStyle::LabelLarge);
    const float start = (rect.left + rect.right - total) / 2.0f;
    const float centerY = (rect.top + rect.bottom) / 2.0f;
    if (glyph) {
        canvas_.Glyph(glyph, {start + 9.0f, centerY}, 16.0f, content);
    }
    canvas_.Text(label, {start + (glyph ? 26.0f : 0.0f), rect.top, rect.right, rect.bottom}, TextStyle::LabelLarge,
                 content);
    AddHit({rect, id, std::move(click)});
}

void Panel::DrawSegmented(const D2D1_RECT_F& rect, uint32_t baseId, std::span<const wchar_t* const> labels,
                          int selected, D2D1_COLOR_F fill, D2D1_COLOR_F onFill,
                          const std::function<void(int)>& select) {
    const Palette& p = P();
    const float radius = (rect.bottom - rect.top) / 2.0f;
    const float width = (rect.right - rect.left) / labels.size();

    // The selection slides between segments; clipping the pill keeps the outer corners round.
    if (selected >= 0) {
        const float position = Animate(AnimKey(baseId, 0), static_cast<float>(selected), 18.0f);
        canvas_.PushClip({rect.left + position * width, rect.top, rect.left + (position + 1.0f) * width, rect.bottom});
        canvas_.FillRound(rect, radius, fill);
        canvas_.PopClip();
    }

    for (size_t index = 0; index < labels.size(); ++index) {
        const D2D1_RECT_F segment{rect.left + index * width, rect.top, rect.left + (index + 1) * width, rect.bottom};
        const uint32_t id = PartId(baseId, static_cast<uint32_t>(index) + 1);
        canvas_.PushClip(segment);
        StateLayer(id, rect, radius, p.onSurface);
        canvas_.PopClip();
        canvas_.Text(labels[index], Inset(segment, 4.0f, 0.0f), TextStyle::LabelMedium,
                     static_cast<int>(index) == selected ? onFill : p.onSurface, TextAlign::Center);
        if (index > 0) {
            canvas_.FillRect({segment.left - 0.5f, rect.top, segment.left + 0.5f, rect.bottom}, p.outline);
        }
        AddHit({segment, id, [select, index] { select(static_cast<int>(index)); }});
    }
    canvas_.StrokeRound(Inset(rect, 0.5f, 0.5f), radius - 0.5f, p.outline, 1.0f);
}

void Panel::DrawSwitchRow(uint32_t id, float top, const wchar_t* label, bool on, std::function<void()> toggle) {
    const Palette& p = P();
    const D2D1_RECT_F row{20.0f, top, kWidth - 20.0f, top + kRowHeight};
    StateLayer(id, row, 16.0f, p.onSurface);
    DrawRowLabel(label, {36.0f, top, kWidth - 100.0f, top + kRowHeight}, p.onSurface);
    DrawSwitch({kWidth - 36.0f - 52.0f, top + 12.0f, kWidth - 36.0f, top + 44.0f}, on, id);
    AddHit({row, id, std::move(toggle)});
}

void Panel::DrawSwitch(const D2D1_RECT_F& track, bool on, uint32_t id) {
    const Palette& p = P();
    const float t = Animate(AnimKey(id, 1), on ? 1.0f : 0.0f, 18.0f);
    canvas_.FillRound(track, 16.0f, Mix(p.surfaceContainerHighest, p.primary, t));
    if (t < 0.999f) {
        canvas_.StrokeRound(Inset(track, 1.0f, 1.0f), 15.0f, Alpha(p.outline, 1.0f - t), 2.0f);
    }
    const float radius = 8.0f + 4.0f * t;
    const float x = track.left + 16.0f + t * (track.right - track.left - 32.0f);
    canvas_.FillCircle({x, (track.top + track.bottom) / 2.0f}, radius, Mix(p.outline, p.onPrimary, t));
}

void Panel::DrawRowLabel(std::wstring_view text, const D2D1_RECT_F& rect, D2D1_COLOR_F color) {
    if (canvas_.Measure(text, TextStyle::BodyLarge) <= rect.right - rect.left) {
        canvas_.Text(text, rect, TextStyle::BodyLarge, color);
    } else {
        canvas_.Text(text, rect, TextStyle::BodyMedium, color, TextAlign::Leading, true);
    }
}

void Panel::DrawStepperRow(uint32_t id, float top, const wchar_t* label, const std::wstring& value, float valueWidth,
                           bool canPrevious, bool canNext, std::function<void()> previous, std::function<void()> next,
                           std::function<void(const D2D1_RECT_F&)> menu) {
    const Palette& p = P();
    const float right = kWidth - 36.0f;
    const float valueLeft = right - 40.0f - valueWidth;
    const D2D1_COLOR_F disabled = Alpha(p.onSurfaceVariant, 0.38f);
    DrawRowLabel(label, {36.0f, top, valueLeft - 40.0f, top + kRowHeight}, p.onSurface);
    IconButton(PartId(id, 1), {valueLeft - 20.0f, top + kRowHeight / 2.0f}, kGlyphChevronLeft,
               canPrevious ? p.onSurface : disabled, canPrevious ? std::move(previous) : [] {});
    const D2D1_RECT_F valueRect{valueLeft, top, right - 40.0f, top + kRowHeight};
    if (menu) {
        // Value and a small chevron, centered together, act as the dropdown button.
        const D2D1_RECT_F button{valueRect.left, top + 8.0f, valueRect.right, valueRect.bottom - 8.0f};
        const uint32_t buttonId = PartId(id, 3);
        StateLayer(buttonId, button, 10.0f, p.onSurface);
        const float textWidth = std::min(canvas_.Measure(value, TextStyle::LabelLarge), valueWidth - 22.0f);
        const float start = (valueRect.left + valueRect.right - textWidth - 16.0f) / 2.0f;
        canvas_.Text(value, {start, top, start + textWidth + 1.0f, top + kRowHeight}, TextStyle::LabelLarge, p.onSurface);
        canvas_.Glyph(kGlyphChevronDown, {start + textWidth + 10.0f, top + kRowHeight / 2.0f}, 10.0f, p.onSurfaceVariant);
        AddHit({button, buttonId, [menu, button] { menu(button); }});
    } else {
        canvas_.Text(value, valueRect, TextStyle::LabelLarge, p.onSurface, TextAlign::Center);
    }
    IconButton(PartId(id, 2), {right - 20.0f, top + kRowHeight / 2.0f}, kGlyphChevronRight,
               canNext ? p.onSurface : disabled, canNext ? std::move(next) : [] {});
}

void Panel::OpenMenu(const D2D1_RECT_F& anchor, std::vector<MenuItem> items, int selected,
                     std::function<void(int)> choose) {
    menu_.open = true;
    menu_.anchor = anchor;
    menu_.items = std::move(items);
    menu_.selected = selected;
    menu_.choose = std::move(choose);
    menu_.scroll = -1.0f;
    animations_.erase(AnimKey(kIdMenu, 0));
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

void Panel::DrawMenu() {
    if (!menu_.open) {
        return;
    }
    const Palette& p = P();
    constexpr float kItemHeight = 44.0f;
    constexpr float kTitleHeight = 32.0f;
    constexpr float kPadding = 8.0f;
    constexpr float kMargin = 12.0f;

    // Everything outside the menu only closes it.
    Hit scrim;
    scrim.rect = {0.0f, 0.0f, kWidth, kHeight};
    scrim.id = kIdMenu;
    scrim.click = [this] { CloseMenu(); };
    scrim.wheel = [](int) {};
    AddHit(std::move(scrim));

    float content = 2.0f * kPadding;
    for (const MenuItem& item : menu_.items) {
        content += item.header ? kTitleHeight : kItemHeight;
    }
    const float width = std::clamp(menu_.anchor.right - menu_.anchor.left, 220.0f, kWidth - 2.0f * kMargin);
    const float left = std::clamp(menu_.anchor.left, kMargin, kWidth - kMargin - width);
    const float below = kHeight - kMargin - (menu_.anchor.bottom + 4.0f);
    const float above = menu_.anchor.top - 4.0f - kMargin;
    const bool down = below >= std::min(content, 260.0f) || below >= above;
    const float height = std::min(content, down ? below : above);
    const float top = down ? menu_.anchor.bottom + 4.0f : menu_.anchor.top - 4.0f - height;
    const D2D1_RECT_F rect{left, top, left + width, top + height};

    const float maxScroll = std::max(0.0f, content - height);
    if (menu_.scroll < 0.0f) {
        float offset = kPadding;
        for (int i = 0; i < menu_.selected && i < static_cast<int>(menu_.items.size()); ++i) {
            offset += menu_.items[i].header ? kTitleHeight : kItemHeight;
        }
        menu_.scroll = offset - (height - kItemHeight) / 2.0f;
    }
    menu_.scroll = std::clamp(menu_.scroll, 0.0f, maxScroll);
    const float scroll = Animate(AnimKey(kIdMenuList, 0), menu_.scroll, 24.0f);

    // Fades in while it grows from the anchor. The page behind dims, and the menu takes the most
    // raised surface with an outline and a soft shadow, so it never blends into the cards below.
    const float appear = Animate(AnimKey(kIdMenu, 0), 1.0f, 22.0f);
    const float reveal = height * (0.85f + 0.15f * appear);
    const D2D1_RECT_F shown = down ? D2D1_RECT_F{rect.left, rect.top, rect.right, rect.top + reveal}
                                   : D2D1_RECT_F{rect.left, rect.bottom - reveal, rect.right, rect.bottom};
    canvas_.FillRect({0.0f, 0.0f, kWidth, kHeight}, {0.0f, 0.0f, 0.0f, (p.dark ? 0.32f : 0.14f) * appear});
    canvas_.FillRound({shown.left - 2.0f, shown.top + 1.0f, shown.right + 2.0f, shown.bottom + 9.0f}, 18.0f,
                      {0.0f, 0.0f, 0.0f, 0.12f * appear});
    canvas_.FillRound({shown.left, shown.top + 2.0f, shown.right, shown.bottom + 4.0f}, 16.0f,
                      {0.0f, 0.0f, 0.0f, 0.22f * appear});
    canvas_.FillRound(shown, 16.0f, Alpha(p.surfaceContainerHighest, appear));
    canvas_.StrokeRound(Inset(shown, 0.5f, 0.5f), 15.5f, Alpha(p.outlineVariant, appear), 1.0f);

    Hit list;
    list.rect = shown;
    list.id = kIdMenuList;
    list.click = [] {};
    list.wheel = [this](int notches) {
        menu_.scroll -= notches * kItemHeight;
        Invalidate();
    };
    AddHit(std::move(list));

    canvas_.PushClip(shown);
    hitClip_.push_back(shown);
    float y = rect.top + kPadding - scroll;
    for (size_t i = 0; i < menu_.items.size(); ++i) {
        const MenuItem& item = menu_.items[i];
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
            canvas_.FillRound(row, 12.0f, Alpha(p.secondaryContainer, appear));
        }
        StateLayer(id, row, 12.0f, p.onSurface);
        const D2D1_COLOR_F color = chosen ? p.onSecondaryContainer : p.onSurface;
        canvas_.Text(item.text, {row.left + 12.0f, y, row.right - (chosen ? 36.0f : 12.0f), y + kItemHeight},
                     TextStyle::BodyLarge, Alpha(color, appear));
        if (chosen) {
            canvas_.Glyph(kGlyphCheck, {row.right - 20.0f, y + kItemHeight / 2.0f}, 14.0f, Alpha(color, appear));
        }
        if (row.bottom > shown.top && row.top < shown.bottom) {
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
        DrawScrollbar(shown, scroll, content);
    }
}

float Panel::DrawNavRow(uint32_t id, float top, wchar_t glyph, D2D1_COLOR_F glyphColor, const std::wstring& title,
                        const std::wstring& subtitle, std::function<void()> click) {
    const Palette& p = P();
    const float height = subtitle.empty() ? kRowHeight : 64.0f;
    const D2D1_RECT_F row{20.0f, top, kWidth - 20.0f, top + height};
    StateLayer(id, row, 16.0f, p.onSurface);
    const float textLeft = glyph ? 72.0f : 36.0f;
    if (glyph) {
        canvas_.Glyph(glyph, {46.0f, top + height / 2.0f}, 18.0f, glyphColor);
    }
    if (subtitle.empty()) {
        canvas_.Text(title, {textLeft, top, kWidth - 56.0f, top + height}, TextStyle::BodyLarge, p.onSurface);
    } else {
        canvas_.Text(title, {textLeft, top + 10.0f, kWidth - 56.0f, top + 32.0f}, TextStyle::BodyLarge, p.onSurface);
        canvas_.Text(subtitle, {textLeft, top + 32.0f, kWidth - 56.0f, top + 52.0f}, TextStyle::BodySmall,
                     p.onSurfaceVariant);
    }
    canvas_.Glyph(kGlyphChevronRight, {kWidth - 40.0f, top + height / 2.0f}, 14.0f, p.onSurfaceVariant);
    AddHit({row, id, std::move(click)});
    return top + height;
}

void Panel::DrawHint(const wchar_t* text, float top, float height) {
    canvas_.Text(text, {36.0f, top, kWidth - 36.0f, top + height}, TextStyle::BodySmall, P().onSurfaceVariant,
                 TextAlign::Leading, true);
}

float Panel::HintHeight(const wchar_t* text, float width) {
    // The default width matches DrawHint.
    const float wrap = width > 0.0f ? width : kWidth - 72.0f;
    return std::ceil(canvas_.MeasureHeight(text, TextStyle::BodySmall, wrap)) + 8.0f;
}

void Panel::DrawScrollbar(const D2D1_RECT_F& area, float offset, float content) {
    const float visible = area.bottom - area.top;
    if (content <= visible + 0.5f) {
        return;
    }
    const float trackTop = area.top + 14.0f;
    const float trackHeight = area.bottom - 14.0f - trackTop;
    const float thumbHeight = std::max(24.0f, trackHeight * visible / content);
    const float thumbTop = trackTop + (trackHeight - thumbHeight) * std::clamp(offset / (content - visible), 0.0f, 1.0f);
    canvas_.FillRound({area.right - 7.0f, thumbTop, area.right - 4.0f, thumbTop + thumbHeight}, 1.5f,
                      Alpha(P().onSurfaceVariant, 0.45f));
}

void Panel::DrawList(const D2D1_RECT_F& list, uint32_t id, float& scroll, size_t count,
                     const std::function<void(size_t, float)>& drawRow) {
    canvas_.FillRound(list, kCardRadius, Alpha(P().surfaceContainer, kCardAlpha));
    const float visible = list.bottom - list.top;
    const float content = count * kRowHeight + 2.0f * kListPadding;
    const float maxScroll = std::max(0.0f, content - visible);
    scroll = std::clamp(scroll, 0.0f, maxScroll);
    const float offset = Animate(AnimKey(id, 0), scroll, 22.0f);

    Hit wheel;
    wheel.rect = list;
    wheel.id = id;
    wheel.wheel = [this, &scroll, maxScroll](int notches) {
        scroll = std::clamp(scroll - notches * kRowHeight, 0.0f, maxScroll);
        Invalidate();
    };
    AddHit(std::move(wheel));

    canvas_.PushRoundClip(list, kCardRadius);
    hitClip_.push_back(list);
    for (size_t index = 0; index < count; ++index) {
        const float top = list.top + kListPadding + index * kRowHeight - offset;
        if (top + kRowHeight >= list.top && top <= list.bottom) {
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
        if (ToLower(app.name).find(needle) != std::wstring::npos || ToLower(app.exe).find(needle) != std::wstring::npos) {
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
        Invalidate();
    }
}

void Panel::OnMouseDown(float x, float y, bool doubleClick) {
    const Hit* hit = HitAt(x, y, false);
    if (!hit) {
        return;
    }
    if (doubleClick && hit->doubleClick) {
        const auto action = hit->doubleClick;
        action();
        return;
    }
    pressedId_ = hotId_ = hit->id;
    SetCapture(window_);
    if (hit->drag) {
        const auto drag = hit->drag;
        drag(x, y);
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
    if (const Hit* hit = HitAt(x, y, false); hit && hit->id == pressed && hit->click) {
        click = hit->click;
    }
    Invalidate();
    // The action may close the panel, so it runs last.
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
    if (key == VK_ESCAPE && menu_.open) {
        CloseMenu();
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
            pickerChroma_ = std::min(pickerChroma_, 0.20f);
            Edit([rgb](Settings& s) {
                s.appearance.windowsAccent = false;
                s.appearance.accent = rgb;
            });
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
    };
    Request request{kind};
    switch (kind) {
    case FileDialog::BrowseExe:
        request.title = T(Text::BrowseTitle);
        request.filterName = T(Text::BrowseFilter);
        request.pattern = L"*.exe";
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
    case FileDialog::SavePreset: {
        std::wstring name = FileNameOf(*owned);
        name = name.substr(0, name.find_last_of(L'.'));
        WritePresetFile(*owned, name, ActiveCurve());
        userPresets_ = LoadUserPresets();
        Invalidate();
        break;
    }
    case FileDialog::ImportPreset:
        if (const auto preset = ImportPresetFile(*owned)) {
            userPresets_ = LoadUserPresets();
            if (features_) {
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
            (eqChannel_ == EqChannel::Game ? s.gameEq : s.chatEq).preset = name;
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
    }
}

}  // namespace ui
