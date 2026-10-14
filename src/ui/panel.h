#pragma once

#include <windows.h>

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "../audio_devices.h"
#include "../balance.h"
#include "../eq_presets.h"
#include "../headset.h"
#include "../hotkeys.h"
#include "../i18n.h"
#include "../parametric_eq.h"
#include "../settings.h"
#include "canvas.h"
#include "frame_clock.h"
#include "theme.h"

namespace ui {

struct AppItem {
    std::wstring exe;
    std::wstring path;
    std::wstring name;
    AppGroup group = AppGroup::Game;
    bool running = false;
};

enum class RoutingSlot { Game, Chat, Output };

struct MenuItem {
    std::wstring text;
    bool header = false;  // Section title; cannot be chosen.
};

// Virtual device mode as shown in the panel.
struct RoutingView {
    bool enabled = false;
    std::wstring ids[3];    // Indexed by RoutingSlot; an empty output means the headset.
    std::wstring names[3];
    std::wstring status;
    bool error = false;
    bool running = false;
    bool manageDevices = true;
    bool cablesMissing = false;  // No Game and Chat devices are chosen and none were found on their own.
};

// State of the update check as shown in the panel.
struct UpdateView {
    enum class State { Idle, Checking, UpToDate, Available, Installing, Failed, CheckFailed } state = State::Idle;
    std::wstring version;  // The newer version while one is available.
};

// Screensaver on the headset display as shown in the panel.
struct IdleScreenView {
    IdleAnimationKind animation = IdleAnimationKind::Off;
    int delaySeconds = 60;
    std::wstring imageName;  // File name of the chosen image.
    bool showing = false;    // The animation is on the display right now.
};

// Receives user actions from the panel. Calls arrive on the UI thread.
class PanelHost {
public:
    // balance as in balance.h; the host keeps it to the headset dial's positions when there is one.
    virtual void OnBalanceChanged(int balance) = 0;
    virtual void OnGroupChanged(const AppItem& app, AppGroup group) = 0;
    virtual std::vector<AppItem> RunningApps() = 0;
    virtual std::wstring AppName(const std::wstring& path) = 0;
    // Global hotkeys are suspended while a new combination is being recorded.
    virtual void OnHotkeyRecording(bool active) = 0;
    virtual void OnHotkeyChanged(int action, Hotkey hotkey) = 0;
    virtual void OnAutostartChanged(bool enabled) = 0;
    virtual void OnLanguageChanged(Language language) = 0;
    virtual void OnWarningDismissed() = 0;
    virtual void OnHeadsetSetting(uint8_t command, int value) = 0;
    virtual void OnEqualizerPreset(int index) = 0;
    virtual void OnEqualizerCurve(const EqCurve& curve) = 0;
    virtual void OnRoutingEnabled(bool enabled) = 0;
    virtual void OnRoutingDevice(RoutingSlot slot, const std::wstring& id) = 0;
    virtual void OnRoutingManage(bool enabled) = 0;
    virtual void OnIdleAnimation(IdleAnimationKind animation) = 0;
    virtual void OnIdleDelay(int seconds) = 0;
    virtual void OnIdleImage(const std::wstring& path) = 0;
    // Any other setting: the edit runs on the host's settings, which then reach the panel again.
    virtual void OnSettingsEdit(const std::function<void(Settings&)>& edit) = 0;
    virtual std::vector<AudioEndpoint> PlaybackDevices() = 0;
    virtual void OnCheckForUpdates() = 0;
    virtual void OnInstallUpdate() = 0;
    virtual void OnOpenReleasePage() = 0;
    virtual void OnQuit() = 0;

protected:
    ~PanelHost() = default;
};

enum class Page {
    // Destinations of the navigation bar.
    Mixer,
    Equalizer,
    Headset,
    Settings,
    // Pages below them.
    Group,
    AddApp,
    EqPresets,
    Appearance,
    ColorPicker,
    Audio,
    PickDevice,
    Hotkeys,
    Notifications,
    Profiles,
    ProfileEdit,
};

// Tray flyout with a Material 3 surface. The window, its render target and the Direct2D
// factories exist only while the panel is visible.
class Panel {
public:
    Panel(HINSTANCE instance, PanelHost& host);
    ~Panel();
    Panel(const Panel&) = delete;
    Panel& operator=(const Panel&) = delete;

    void SetTrayAnchor(HWND window, UINT id);
    void SetAutoHide(bool autoHide) { autoHide_ = autoHide; }

    void Show(Page page = Page::Mixer);
    void Hide();
    void Toggle();
    bool IsVisible() const { return window_ != nullptr; }
    HWND Handle() const { return window_; }

    void SetLevels(Levels levels);
    // A headset dial is connected: the slider keeps to its positions instead of single percents.
    void SetBalanceDial(bool dial);
    // Chat silenced by the mute hotkey; the balance itself is unchanged.
    void SetChatMuted(bool muted);
    // detail follows the device name; a warning replaces both and is shown in the error color.
    void SetStatus(const std::wstring& device, const std::wstring& detail, bool connected, const std::wstring& warning);
    void SetApps(std::vector<AppItem> apps);
    void SetHotkeys(const std::array<Hotkey, kHotkeyCount>& hotkeys, const std::array<bool, kHotkeyCount>& failed);
    void SetAutostart(bool enabled);
    void SetSettings(const Settings& settings);
    // features is null while no SteelSeries headset is connected.
    void SetHeadset(const std::wstring& name, const HeadsetFeatures* features, const HeadsetState& state);
    void SetRouting(const RoutingView& routing);
    void SetIdleScreen(const IdleScreenView& idle);
    void SetUpdate(const UpdateView& update);
    // Redraws after the language or theme changed.
    void Refresh();

private:
    struct Hit {
        D2D1_RECT_F rect;
        uint32_t id = 0;
        std::function<void()> click;
        std::function<void(float x, float y)> drag;
        std::function<void(int notches)> wheel;
        std::function<void()> doubleClick;
    };

    enum class FileDialog { BrowseExe, SavePreset, ImportPreset, IdleImage, SaveEqPreset, ImportEqPreset };
    enum class AddTarget { Group, Profile };

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    void CreatePanelWindow();
    void ApplyWindowTheme();
    void Invalidate();
    void Render();
    void GoTo(Page page);
    void GoBack();
    static Page ParentOf(Page page);
    Page TabOf(Page page) const;
    float ContentBottom() const;
    bool HeadsetTabVisible() const { return features_ && features_->HasSettings(); }

    // Shared pieces.
    void DrawNavigationBar();
    // trailing keeps room on the right for an action button.
    void DrawHeader(const wchar_t* title, const wchar_t* subtitle = nullptr, float trailing = 0.0f);
    void DrawTitle(const wchar_t* title);
    float SectionTitle(const wchar_t* title, float y);
    D2D1_RECT_F Card(float top, float height);
    float BeginScroll(Page page, float top);
    void EndScroll(Page page, float top, float y);
    void DrawSegmented(const D2D1_RECT_F& rect, uint32_t baseId, std::span<const wchar_t* const> labels, int selected,
                       D2D1_COLOR_F fill, D2D1_COLOR_F onFill, const std::function<void(int)>& select);
    void DrawSwitchRow(uint32_t id, float top, const wchar_t* label, bool on, std::function<void()> toggle);
    void DrawSwitch(const D2D1_RECT_F& track, bool on, uint32_t id);
    // A label that does not fit on one line wraps onto two in a smaller style instead of being cut off.
    void DrawRowLabel(std::wstring_view text, const D2D1_RECT_F& rect, D2D1_COLOR_F color);
    // ‹ value › on the right of a row; valueWidth is the room for the value text.
    // With menu, the value also opens a list of all choices, anchored below it.
    void DrawStepperRow(uint32_t id, float top, const wchar_t* label, const std::wstring& value, float valueWidth,
                        bool canPrevious, bool canNext, std::function<void()> previous, std::function<void()> next,
                        std::function<void(const D2D1_RECT_F&)> menu = {});
    // Dropdown over the page; choose gets the index of the chosen item.
    void OpenMenu(const D2D1_RECT_F& anchor, std::vector<MenuItem> items, int selected, std::function<void(int)> choose);
    void CloseMenu();
    void DrawMenu();
    // Row with a leading glyph, title, optional subtitle and a chevron; returns the bottom.
    float DrawNavRow(uint32_t id, float top, wchar_t glyph, D2D1_COLOR_F glyphColor, const std::wstring& title,
                     const std::wstring& subtitle, std::function<void()> click);
    void DrawHint(const wchar_t* text, float top, float height);
    // Room a hint under a row takes, including the gap below it; width is the text width.
    float HintHeight(const wchar_t* text, float width = 0.0f);
    void DrawScrollbar(const D2D1_RECT_F& area, float offset, float content);
    void DrawList(const D2D1_RECT_F& list, uint32_t id, float& scroll, size_t count,
                  const std::function<void(size_t, float)>& drawRow);
    void IconButton(uint32_t id, D2D1_POINT_2F center, wchar_t glyph, D2D1_COLOR_F color, std::function<void()> click);
    void TextButton(uint32_t id, const D2D1_RECT_F& rect, wchar_t glyph, const wchar_t* label, bool tonal,
                    std::function<void()> click);
    void StateLayer(uint32_t id, const D2D1_RECT_F& rect, float radius, D2D1_COLOR_F content);
    void AddHit(Hit hit);
    float Animate(uint64_t key, float target, float speed = 16.0f);
    void DrawSnackbar();

    // Mixer and groups (panel_mixer.cpp).
    void DrawMixer();
    void DrawMixCard(const D2D1_RECT_F& card);
    void DrawSlider(float left, float right, float centerY);
    // Moves the balance by positions of a grid with steps per side (the dial's grid when there is one).
    void NudgeBalance(int direction, int steps);
    void DrawGroup();
    void DrawAddApp();
    void DrawAppRow(const AppItem& app, float top);
    void DrawRunningRow(const AppItem& app, float top);
    void DrawAppIcon(const AppItem& app, const D2D1_RECT_F& rect);
    std::vector<AppGroup> AvailableGroups() const;
    std::vector<AppItem> AppsInGroup(AppGroup group) const;
    void OpenGroup(AppGroup group);
    void OpenAddApp(AddTarget target);

    // Equalizer of the virtual devices (panel_eq.cpp).
    void DrawEqualizerPage();
    void DrawEqGraph(const D2D1_RECT_F& rect, const ParametricEq& eq);
    float DrawBandEditor(float top, const ParametricEq& eq);
    void DrawEqPresets();
    EqChannelSettings& EqChannelState();
    void EditEq(const std::function<void(ParametricEq&)>& edit);
    void EditEffects(const std::function<void(EqEffects&)>& edit);
    void ApplyEqPreset(const NamedEq& preset);
    float DrawEqEffects(float top, const ParametricEq& eq);
    // Built-in, Sonar and own presets of a channel, kept while the pages that list them are open.
    void LoadEqLibrary(EqChannel channel);
    // The library as menu items under section titles; presets[i] is the preset of item i.
    std::vector<MenuItem> EqLibraryMenu(std::vector<const NamedEq*>& presets) const;
    void OpenEqPresetMenu(const D2D1_RECT_F& anchor);

    // Headset and devices (panel_pages.cpp).
    void DrawHeadset();
    float DrawEqualizer(float top);
    float DrawSettingRow(const SettingSpec& spec, float top);
    float DrawIdleScreen(float top);
    void DrawPickDevice();
    float DrawRoutingSettings(float top);
    void OpenPicker(RoutingSlot slot);
    // Converts a game preset to the headset's band curve; an unsaved custom curve is backed up first.
    void ApplyHeadsetGamePreset(const NamedEq& preset);
    // The curve the headset currently plays (or the custom curve for on-device presets).
    EqCurve ActiveCurve() const;

    // Settings (panel_settings.cpp).
    void DrawSettings();
    void DrawAppearance();
    void DrawColorPicker();
    void DrawAudio();
    void DrawHotkeys();
    void DrawNotifications();
    void DrawProfiles();
    void DrawProfileEdit();
    void Edit(const std::function<void(Settings&)>& edit);
    GameProfile* EditedProfile();
    std::wstring HotkeyHint() const;

    const Hit* HitAt(float x, float y, bool wheel) const;
    void OnMouseMove(float x, float y);
    void OnMouseDown(float x, float y, bool doubleClick);
    void OnMouseUp(float x, float y);
    void OnWheel(float x, float y, int delta);
    bool OnKey(UINT key, bool down);
    void OnChar(wchar_t ch);
    void StartRecording(int action);
    void StopRecording();
    void RunFileDialog(FileDialog kind);
    void OnFileDialogDone(FileDialog kind, std::wstring* path);

    ID2D1Bitmap* IconFor(const std::wstring& path);
    std::vector<AppItem> FilteredRunningApps() const;
    const Palette& P() const { return *palette_; }

    HINSTANCE instance_;
    PanelHost& host_;
    HWND window_ = nullptr;
    HWND trayWindow_ = nullptr;
    UINT trayId_ = 0;
    Canvas canvas_;
    const Palette* palette_ = &ThemePalette();
    bool backdrop_ = false;
    bool autoHide_ = true;
    float dpi_ = 96.0f;
    ULONGLONG hiddenAt_ = 0;

    Page page_ = Page::Mixer;
    std::map<Page, float> scroll_;  // Scroll offset per page.
    Levels levels_;
    bool balanceDial_ = false;
    bool chatMuted_ = false;
    std::wstring device_;
    std::wstring deviceDetail_;
    std::wstring warning_;
    bool deviceConnected_ = false;
    std::vector<AppItem> apps_;
    std::array<Hotkey, kHotkeyCount> hotkeys_{};
    std::array<bool, kHotkeyCount> hotkeyFailed_{};
    bool autostart_ = false;
    Settings settings_;

    std::wstring headsetName_;
    const HeadsetFeatures* features_ = nullptr;
    HeadsetState headset_;
    RoutingView routing_;
    IdleScreenView idle_;
    UpdateView update_;
    std::vector<AudioEndpoint> endpoints_;
    RoutingSlot pickSlot_ = RoutingSlot::Game;
    std::vector<UserPreset> userPresets_;

    AppGroup group_ = AppGroup::Chat;      // Group page being shown.
    AddTarget addTarget_ = AddTarget::Group;
    AppGroup addGroup_ = AppGroup::Chat;
    std::vector<AppItem> running_;
    std::wstring filter_;
    float addScroll_ = 0.0f;
    float groupScroll_ = 0.0f;
    bool browsing_ = false;
    int recording_ = -1;
    UINT heldModifiers_ = 0;

    EqChannel eqChannel_ = EqChannel::Game;
    int eqBand_ = 0;                        // Band selected in the equalizer graph.
    std::vector<NamedEq> eqLibrary_[3];     // Built-in, Sonar and own presets of eqLibraryChannel_.
    EqChannel eqLibraryChannel_ = EqChannel::Game;
    struct MenuState {
        bool open = false;
        D2D1_RECT_F anchor{};
        std::vector<MenuItem> items;
        int selected = -1;
        std::function<void(int)> choose;
        float scroll = -1.0f;  // Negative: brings the selected item into view on the next frame.
    } menu_;
    std::wstring profileExe_;               // Profile being edited.
    std::wstring hexInput_;                 // Typed custom color.
    float pickerHue_ = 0.0f;                // Custom color being edited, OKLCH hue in degrees.
    float pickerChroma_ = 0.12f;

    std::vector<Hit> hits_;
    std::vector<D2D1_RECT_F> hitClip_;
    uint32_t hotId_ = 0;
    uint32_t pressedId_ = 0;
    uint32_t dragId_ = 0;  // Equalizer handle being dragged, and where it was grabbed.
    float dragOffsetX_ = 0.0f;
    float dragOffsetY_ = 0.0f;
    bool trackingMouse_ = false;
    int wheelRemainder_ = 0;

    std::unordered_map<uint64_t, float> animations_;
    FrameClock frames_;
    bool animating_ = false;
    float frameSeconds_ = 0.0f;
    LARGE_INTEGER lastFrame_{};
    LARGE_INTEGER frequency_{};

    std::unordered_map<std::wstring, Microsoft::WRL::ComPtr<ID2D1Bitmap>> icons_;
};

}  // namespace ui
