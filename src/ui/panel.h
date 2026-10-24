#pragma once

#include <windows.h>

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
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
#include "../plugin_host.h"
#include "../settings.h"
#include "canvas.h"
#include "frame_clock.h"
#include "design.h"
#include "theme.h"

namespace ui {

struct AppItem {
    std::wstring exe;
    std::wstring path;
    std::wstring name;
    AppGroup group = AppGroup::Game;
    bool running = false;
    bool ignored = false;  // Left out of ChatMix; listed only where apps are added.
};

// The device pickers: the three of virtual device mode, then the two of the microphone chain.
// Devices the picker page chooses: the virtual devices, the shared output, where each channel plays
// (empty: the shared output) and the microphone's.
enum class RoutingSlot { Game, Chat, Output, MicInput, MicOutput, Media, GameOutput, ChatOutput, MediaOutput };
inline constexpr int kRoutingSlots = 9;

struct MenuItem {
    std::wstring text;
    bool header = false;  // Section title; cannot be chosen.
    wchar_t glyph = 0;    // Leading icon; 0 for none.
    bool action = false;  // Opens a page instead of choosing an entry: set apart by a line, primary color, a chevron.
};

// Virtual device mode as shown in the panel.
struct RoutingView {
    bool enabled = false;
    std::wstring ids[kRoutingSlots];  // Indexed by RoutingSlot; an empty output means the headset.
    std::wstring names[kRoutingSlots];
    bool mediaRouted = false;         // Media has a device of its own and the group is on.
    std::wstring status;
    bool error = false;
    bool running = false;
    bool manageDevices = true;
    bool cablesMissing = false;  // No Game and Chat devices are chosen and none were found on their own.
};

// A connected playback device in the order of preference.
struct OutputView {
    std::wstring id;
    std::wstring name;
    bool current = false;   // The output in use.
    bool excluded = false;  // Never switched to automatically or by the hotkey.
};

struct PluginView {
    std::wstring name;
    std::wstring path;
    bool enabled = true;
    bool loaded = false;
    bool hasEditor = false;
    std::optional<DeepFilterSettings> deepFilter;  // Set for the DeepFilterNet plugin once it loaded.
    std::optional<NvidiaAfxSettings> nvidiaAfx;    // Set for the NVIDIA Audio Effects plugin once it loaded.
};

// The plugins of the microphone chain or of a virtual device channel, in the order of their settings.
struct PluginChainView {
    std::vector<PluginView> plugins;
    int latencyMs = 0;  // What the enabled plugins delay the sound by.
};

// The microphone chain as shown in the panel.
struct MicView {
    std::wstring inputName;   // Empty while the Windows default is used.
    std::wstring outputName;  // The cable in use, also when chosen automatically.
    std::wstring status;
    bool error = false;
};

// Peak levels the panel shows, as linear samples (1 is 0 dBFS).
struct Meters {
    float game = 0.0f;
    float chat = 0.0f;
    float mic = 0.0f;
    float media = 0.0f;
};

// State of the update check as shown in the panel.
struct UpdateView {
    enum class State { Idle, Checking, UpToDate, Available, Installing, Failed, CheckFailed } state = State::Idle;
    std::wstring version;  // The newer version while one is available.
};

// Third-party add-ons as the panel shows them.
struct AddOnsView {
    // Off: none enabled, or nothing runs yet.
    enum class State { Off, Running, GgRunning, NoDisplay } state = State::Off;
    std::vector<bool> running;  // By position in Settings::addOns.
};

// Screensaver on the headset display as shown in the panel.
struct IdleScreenView {
    IdleAnimationKind animation = IdleAnimationKind::Off;
    int delaySeconds = 60;
    std::wstring imageName;  // File name of the chosen image.
    bool showing = false;    // The animation is on the display right now.
    std::wstring ggApps;     // SteelSeries GG's apps that draw on the display, which the screensaver gives way to.
    bool bluetooth = false;  // The headset tells its base station's Bluetooth, which the screensaver can show.
    IconCorner bluetoothIcon = IconCorner::TopRight;
};

// Receives user actions from the panel. Calls arrive on the UI thread.
class PanelHost {
public:
    // balance as in balance.h; the host keeps it to the headset dial's positions when there is one.
    virtual void OnBalanceChanged(int balance) = 0;
    virtual void OnGroupChanged(const AppItem& app, AppGroup group) = 0;
    // Forgets the app's group; it follows the Game side again, and a closed app leaves the lists.
    virtual void OnAppRemoved(const AppItem& app) = 0;
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
    // The custom filters of a parametric headset equalizer (Nova 7 Gen 2, Nova Elite).
    virtual void OnEqualizerFilters(const EqFilters& filters) = 0;
    // An extra equalizer of the headset (Nova Elite: Bluetooth, microphone): a built-in preset, or the custom curve.
    virtual void OnExtraEqualizerPreset(ExtraEqualizer which, int index) = 0;
    virtual void OnExtraEqualizerCurve(ExtraEqualizer which, const EqCurve& curve) = 0;
    virtual void OnRoutingEnabled(bool enabled) = 0;
    virtual void OnRoutingDevice(RoutingSlot slot, const std::wstring& id) = 0;
    virtual void OnRoutingManage(bool enabled) = 0;
    virtual void OnIdleAnimation(IdleAnimationKind animation) = 0;
    virtual void OnIdleDelay(int seconds) = 0;
    virtual void OnIdleImage(const std::wstring& path) = 0;
    // Shows the chosen screensaver on the headset for a few seconds.
    virtual void OnIdleTest() = 0;
    virtual void OnIdleBluetoothIcon(IconCorner corner) = 0;
    // Shows the add-on's screen for the event on the headset once, with sample fields.
    virtual void OnAddOnPreview(const std::wstring& path, const std::wstring& event) = 0;
    // Any other setting: the edit runs on the host's settings, which then reach the panel again.
    virtual void OnSettingsEdit(const std::function<void(Settings&)>& edit) = 0;
    virtual std::vector<AudioEndpoint> PlaybackDevices() = 0;
    virtual std::vector<AudioEndpoint> RecordingDevices() = 0;
    // Plugins of the microphone chain (EqChannel::Mic) or of a virtual device channel.
    virtual void OnPluginAdd(EqChannel channel, const std::wstring& path) = 0;
    virtual void OnPluginRemove(EqChannel channel, size_t index) = 0;
    // Moves the plugin at from to the place to, shifting the ones between.
    virtual void OnPluginMove(EqChannel channel, size_t from, size_t to) = 0;
    virtual void OnPluginEditor(EqChannel channel, size_t index) = 0;
    virtual void OnPluginDeepFilter(EqChannel channel, size_t index, const DeepFilterSettings& settings) = 0;
    virtual void OnPluginNvidiaAfx(EqChannel channel, size_t index, const NvidiaAfxSettings& settings) = 0;
    // Uses the device now.
    virtual void OnOutputChoose(const std::wstring& id) = 0;
    // Moves a device from one place to another among the listed ones, in the order of preference.
    virtual void OnOutputMove(size_t from, size_t to) = 0;
    virtual void OnOutputExclude(const std::wstring& id, bool excluded) = 0;
    virtual void OnCheckForUpdates() = 0;
    virtual void OnInstallUpdate() = 0;
    virtual void OnOpenReleasePage() = 0;
    virtual void OnQuit() = 0;
    // Writes every setting to a ZIP archive; false if it could not be saved.
    virtual bool OnSettingsExport(const std::wstring& path) = 0;
    // Takes the settings of an archive and restarts; false if the file holds none.
    virtual bool OnSettingsImport(const std::wstring& path) = 0;
    // Peak samples (0-1) since the last call: Game and Chat as heard, and the processed microphone.
    virtual Meters ReadMeters() = 0;

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
    Microphone,
    Outputs,
    General,
    // The first-run tour; it hides the navigation bar.
    Welcome,
    // One channel of virtual device mode: its device and where it plays.
    RoutingChannel,
    // Third-party add-ons, and the options of one.
    AddOns,
    AddOnEdit,
    // The screen of one add-on event, and one display of it.
    AddOnEvent,
    AddOnDisplay,
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
    void SetGameMuted(bool muted);
    // detail follows the device name; a warning replaces both and is shown in the error color.
    // connection: how the headset reaches the computer ("USB", "2.4 GHz"), shown beside its name; may be empty.
    void SetStatus(const std::wstring& device, const std::wstring& detail, const std::wstring& connection, bool connected,
                   const std::wstring& warning);
    void SetApps(std::vector<AppItem> apps);
    void SetHotkeys(const std::array<Hotkey, kHotkeyCount>& hotkeys, const std::array<bool, kHotkeyCount>& failed);
    void SetAutostart(bool enabled);
    void SetSettings(const Settings& settings);
    // features is null while no SteelSeries headset is connected.
    void SetHeadset(const std::wstring& name, const HeadsetFeatures* features, const HeadsetState& state);
    // Headsets that save a run of changes once it ends (features.saveDelayMs): the save is due at deadline
    // (GetTickCount64), and SetHeadsetSaved says it went out. The equalizer's title row counts down, then says
    // Done.
    void SetHeadsetSaving(ULONGLONG deadline, unsigned delayMs);
    void SetHeadsetSaved();
    void DrawSaveState(float right, float centerY);
    void SetRouting(const RoutingView& routing);
    void SetIdleScreen(const IdleScreenView& idle);
    void SetAddOns(const AddOnsView& addOns);
    void SetMic(const MicView& mic);
    void SetPlugins(EqChannel channel, const PluginChainView& view);
    void SetOutputs(std::vector<OutputView> outputs);
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
        std::function<void()> release;  // When the button goes up after a press on this hit, wherever it is.
        const wchar_t* tooltip = nullptr;  // Shown after the pointer rests on the hit, or on keyboard focus.
    };

    // A row of a card: how tall it is and how to draw it at a given top.
    struct CardRow {
        float height = 0.0f;
        std::function<void(float top)> draw;
        uint32_t id = 0;  // The tile reacts to hover and press with this id; 0 for a static tile.
    };

    enum class FileDialog { BrowseExe, SavePreset, ImportPreset, IdleImage, SaveEqPreset, ImportEqPreset, Plugin, AddOn,
                            ExportSettings, ImportSettings,
                            AddOnImage };
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

    // Design system components (panel_components.cpp). Pages are built from these; docs/design-system.md
    // describes when to use which.
    // A group of rows at y, one tile per row; returns the bottom of the group.
    float DrawCard(float y, const std::vector<CardRow>& rows);
    // A label with a switch; optional supporting text below it.
    CardRow SwitchItem(uint32_t id, const wchar_t* label, bool on, std::function<void()> toggle,
                       const wchar_t* hint = nullptr);
    // A leading icon, title, optional subtitle and a chevron; opens a page.
    CardRow NavItem(uint32_t id, wchar_t glyph, D2D1_COLOR_F glyphColor, const std::wstring& title,
                    const std::wstring& subtitle, std::function<void()> click);
    // A label above a connected button group: two to four short options, all in view.
    CardRow ChoiceItem(uint32_t id, const wchar_t* label, std::vector<std::wstring> options, int selected,
                       std::function<void(int)> select, const wchar_t* hint = nullptr);
    // A label with a value on the right and a chevron, e.g. a device that opens a picker page.
    CardRow ValueItem(uint32_t id, const wchar_t* label, const std::wstring& value, std::function<void()> click);
    // A label with a dropdown: one of count options from a menu, for longer lists or long labels.
    CardRow DropdownItem(uint32_t id, const wchar_t* label, int index, int count,
                         std::function<std::wstring(int)> labelOf, std::function<void(int)> select,
                         const wchar_t* hint = nullptr);
    // A label with − value +: a number stepped by the buttons or typed into the value.
    CardRow NumberItem(uint32_t id, const wchar_t* label, const std::wstring& value, bool canDecrease,
                       bool canIncrease, std::function<void()> decrease, std::function<void()> increase,
                       std::function<bool(const std::wstring&)> edit, const wchar_t* hint = nullptr);
    // Supporting text as a tile of its own.
    CardRow HintItem(const wchar_t* text);
    // Rows the user reorders by dragging; a press without movement is a click. ids are stable per item.
    // drawRow gets the item and the place it shows at, which differs from it while a row is dragged.
    float DrawDragCard(float y, uint32_t listId, const std::vector<uint32_t>& ids, float rowHeight,
                       const std::function<void(size_t index, size_t place, const D2D1_RECT_F& row, bool lifted)>& drawRow,
                       const std::function<void(size_t from, size_t to)>& move,
                       const std::function<void(size_t index)>& click);
    // A value that becomes a text field when clicked, its text selected; Enter or a click elsewhere
    // applies the text, Esc cancels. The caret moves with the arrows, Home and End or a click, and a
    // drag or Shift selects. commit returns false for text it cannot use: Enter then keeps the field
    // open to fix it, a click elsewhere drops it.
    void DrawEditableValue(uint32_t id, const D2D1_RECT_F& rect, const std::wstring& value,
                           std::function<bool(const std::wstring&)> commit, bool numeric = true);
    // Opens a field for id with text selected, e.g. to rename something from a button.
    void BeginField(uint32_t id, std::wstring text, std::function<bool(const std::wstring&)> commit, bool numeric);
    bool CommitField(bool keepOnError = false);
    bool FieldKey(UINT key);
    void FieldChar(wchar_t ch);
    // Keeps the field's text before a change for Ctrl+Z; typed: a typed character, which joins a run of them.
    void KeepFieldText(bool typed);
    // Caret position for a pointer x inside the field.
    size_t FieldCaretAt(float x);
    void CancelField();
    // The pin in the title row of the top-level pages: a pinned panel stays open when focus leaves it.
    void DrawPinButton();

    // Shared pieces.
    void DrawNavigationBar();
    // trailing keeps room on the right for an action button.
    void DrawHeader(const wchar_t* title, const wchar_t* subtitle = nullptr, float trailing = 0.0f);
    void DrawTitle(const wchar_t* title);
    float SectionTitle(const wchar_t* title, float y);
    // Explanation between groups, under a section title or the header; returns where the next group goes.
    float PageHint(const wchar_t* text, float y, std::optional<D2D1_COLOR_F> color = std::nullopt);
    // A standalone container, such as the mixer card or the equalizer graph: a group of one.
    D2D1_RECT_F Card(float top, float height);
    // The full-width rectangle of a tile at top.
    D2D1_RECT_F TileRect(float top, float height) const;
    // A tile of a group: outer corners of the group are large, inner ones small. With id it reacts to
    // hover and press; lifted draws it raised, as while dragged.
    void DrawTile(const D2D1_RECT_F& rect, uint32_t id, bool first, bool last, bool lifted = false);
    float BeginScroll(Page page, float top);
    void EndScroll(Page page, float top, float y);
    // A connected button group: the selected button fills with fill and holds onFill.
    void DrawSegmented(const D2D1_RECT_F& rect, uint32_t baseId, std::span<const wchar_t* const> labels, int selected,
                       D2D1_COLOR_F fill, D2D1_COLOR_F onFill, const std::function<void(int)>& select);
    void DrawSwitchRow(uint32_t id, float top, const wchar_t* label, bool on, std::function<void()> toggle);
    void DrawSwitch(const D2D1_RECT_F& track, bool on, uint32_t id);
    // A label that does not fit on one line wraps onto two in a smaller style instead of being cut off.
    void DrawRowLabel(std::wstring_view text, const D2D1_RECT_F& rect, D2D1_COLOR_F color);
    // − value + ending at right, centered on centerY; with edit the value can be typed.
    void DrawNumberControl(uint32_t id, float right, float centerY, const std::wstring& value, bool canDecrease,
                           bool canIncrease, std::function<void()> decrease, std::function<void()> increase,
                           std::function<bool(const std::wstring&)> edit = {});
    static float NumberControlWidth();
    // A dropdown field ending at right; open gets its rectangle to anchor the menu. Returns the field.
    D2D1_RECT_F DrawDropdown(uint32_t id, float right, float centerY, const std::wstring& value, float maxWidth,
                             std::function<void(const D2D1_RECT_F&)> open);
    // Dropdown over the page; choose gets the index of the chosen item.
    // searchable: typing filters the items, and a row at the top says so (long lists such as presets).
    void OpenMenu(const D2D1_RECT_F& anchor, std::vector<MenuItem> items, int selected, std::function<void(int)> choose,
                  bool searchable = false);
    // The items of the open menu that match its search, by index, with the titles of their sections.
    std::vector<size_t> MenuMatches() const;
    void CloseMenu();
    void DrawMenu();
    // Row content with a leading glyph, title, optional subtitle and a chevron; returns the bottom.
    // trailing: room kept free left of the chevron, e.g. for a value.
    // wrap: a subtitle that does not fit on its line wraps, in a row as tall as NavRowHeight.
    float DrawNavRow(uint32_t id, float top, wchar_t glyph, D2D1_COLOR_F glyphColor, const std::wstring& title,
                     const std::wstring& subtitle, std::function<void()> click, float trailing = 0.0f,
                     bool wrap = false);
    float NavRowHeight(wchar_t glyph, const std::wstring& subtitle, float trailing = 0.0f);
    void DrawHint(const wchar_t* text, float top, float height);
    // Room supporting text under a row takes, including the gap below it; width is the text width.
    float HintHeight(const wchar_t* text, float width = 0.0f);
    // The scrollbar of a scrolling area; with an id and scrollTo its thumb can be dragged, and a press beside it
    // jumps there.
    void DrawScrollbar(const D2D1_RECT_F& area, float offset, float content, uint32_t id = 0,
                       std::function<void(float)> scrollTo = {});
    // A scrolling segmented list filling the rectangle; idOf gives each row's hover id.
    void DrawList(const D2D1_RECT_F& list, uint32_t id, float& scroll, size_t count, float rowHeight,
                  const std::function<uint32_t(size_t)>& idOf, const std::function<void(size_t, float)>& drawRow);
    void IconButton(uint32_t id, D2D1_POINT_2F center, wchar_t glyph, D2D1_COLOR_F color, std::function<void()> click);
    // Gives the hit added last with this id a tooltip.
    void Tooltip(uint32_t id, const wchar_t* text);
    // Starts the tooltip delay for the hit under the pointer, or hides the tooltip.
    void ArmTooltip(const Hit* hit);
    void DrawTooltip();
    void TextButton(uint32_t id, const D2D1_RECT_F& rect, wchar_t glyph, const wchar_t* label, bool tonal,
                    std::function<void()> click);
    void StateLayer(uint32_t id, const D2D1_RECT_F& rect, float radius, D2D1_COLOR_F content);
    void StateLayer(uint32_t id, const D2D1_RECT_F& rect, const Corners& corners, D2D1_COLOR_F content);
    // Registers a hit for the pointer and, when it acts on a click or drag, as a keyboard focus stop.
    void AddHit(Hit hit);
    // Keyboard focus: Tab and Shift+Tab move it (direction 1 or -1) and scroll it into view; Enter
    // activates it; arrows step what the mouse wheel steps.
    void MoveFocus(int direction);
    bool ActivateFocus();
    bool StepFocus(int notches);
    void DrawFocusRing();
    // Moves the value for key toward target on a spring and returns where it is now. Essential motion
    // (scrolling) keeps a short glide with reduced motion; everything else then jumps.
    float Animate(uint64_t key, float target, design::Spring spring = design::kSpringDefaultEffects,
                  bool essential = false);
    float AnimatedValue(uint64_t key, float fallback) const;
    // Places the value without motion, e.g. where a dropped row was let go.
    void SetAnimation(uint64_t key, float value);
    void DrawSnackbar();
    uint32_t tipId_ = 0;     // The hovered hit whose tooltip is pending or shown.
    bool tipShown_ = false;  // The hover delay has passed.
    // Reads the meters while a page that shows them is open, and redraws when they change.
    void TickMeters();
    // A volume slider whose active track also shows what plays: the darker fill is the current
    // peak. volume is in percent; set gets the new one.
    void DrawVolumeSlider(uint32_t id, float left, float right, float centerY, int volume, float meter,
                          D2D1_COLOR_F accent, D2D1_COLOR_F container, std::function<void(int)> set);
    // The processed microphone's level against 0 dBFS, with a peak hold.
    CardRow MicLevelItem();

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
    // What the response graph and the band editor work on: a channel's equalizer, or a parametric headset's
    // filters (hardware), which have no preamp, preset files of their own or notch type.
    struct EqEditor {
        std::function<void(const std::function<void(ParametricEq&)>&)> edit;
        int* band = nullptr;  // The selected band.
        D2D1_COLOR_F accent{};
        D2D1_COLOR_F onAccent{};
        bool enabled = true;
        bool hardware = false;
        int typeCount = kFilterTypeCount;  // Filter types offered, in FilterType order.
        uint32_t idBase = 0;               // Added to the widget IDs, so both editors keep their own.
    };
    EqEditor ChannelEqEditor();
    EqEditor HeadsetEqEditor();
    void DrawEqGraph(const D2D1_RECT_F& rect, const ParametricEq& eq, const EqEditor& editor);
    float DrawBandEditor(float top, const ParametricEq& eq, const EqEditor& editor);
    void DrawEqPresets();
    EqChannelSettings& EqChannelState();
    // Channels the equalizer page offers: Game and Chat in virtual device mode, the microphone while
    // its chain is on.
    std::vector<EqChannel> EqChannels() const;
    void EditEq(const std::function<void(ParametricEq&)>& edit);
    void EditEffects(const std::function<void(EqEffects&)>& edit);
    void ApplyEqPreset(const NamedEq& preset);
    float DrawEqEffects(float top, const ParametricEq& eq);
    // Built-in, Sonar and own presets of a channel, kept while the pages that list them are open.
    void LoadEqLibrary(EqChannel channel);
    // The library as menu items under section titles; presets[i] is the preset of item i. Built-in and Sonar
    // presets named like one of deviceOwn are left out; own presets always stay.
    std::vector<MenuItem> EqLibraryMenu(std::vector<const NamedEq*>& presets,
                                        std::span<const EqPreset> deviceOwn = {}) const;
    void OpenEqPresetMenu(const D2D1_RECT_F& anchor);
    // A preset of the loaded library by name; null when it has none.
    const NamedEq* FindInEqLibrary(const std::wstring& name) const;

    // Headset and devices (panel_pages.cpp).
    void DrawHeadset();
    float DrawEqualizer(float top);
    // The equalizer's title with its reset; with extra equalizers, the switch between them below it.
    float DrawEqualizerTitle(float top, std::function<void()> reset);
    // Ten band sliders from top; change gets the edited curve. Returns the bottom of the band labels.
    float DrawBandSliders(float top, const EqCurve& curve, int8_t range, bool editable, uint32_t idBase,
                          std::function<void(const EqCurve&)> change);
    // The graph and band editor of a parametric headset equalizer, in place of the ten band sliders.
    float DrawHeadsetFilters(float top);
    // An extra equalizer of the Nova Elite (Bluetooth, microphone): presets and ten band sliders.
    float DrawExtraEqualizer(float top, ExtraEqualizer which);
    // Below the bands: preset file actions, the game presets menu and the saved presets.
    float HeadsetPresetToolsHeight();
    void DrawHeadsetPresetTools(float top);
    // A saved headset preset inside the equalizer card: applied by a click; rename, copy and delete.
    void DrawPresetRow(const UserPreset& preset, const D2D1_RECT_F& row, bool active, std::function<void()> apply);
    float DrawSettingRow(const SettingSpec& spec, float top);
    float DrawVisualizer(float top);
    float DrawIdleScreen(float top);
    void DrawPickDevice();
    float DrawRoutingSettings(float top);
    void OpenPicker(RoutingSlot slot);
    // Converts a game preset to the headset's band curve; an unsaved custom curve is backed up first.
    void ApplyHeadsetGamePreset(const NamedEq& preset);
    // The curve the headset currently plays (or the custom curve for on-device presets).
    EqCurve ActiveCurve() const;
    // The filters a parametric headset currently plays.
    EqFilters ActiveHeadsetFilters() const;
    // The curve of an extra equalizer: its preset's, or the custom one.
    EqCurve ActiveExtraCurve(ExtraEqualizer which) const;

    // Ctrl+Z and Ctrl+Y for the equalizers (panel_undo.cpp): how one looked before a change. The Headset tab's
    // steps hold all the headset's equalizers; the Equalizer tab's one channel.
    struct HeadsetEqSnapshot {
        int eqPreset = -1;
        EqCurve eqCurve{};
        bool eqCurveKnown = false;
        EqFilters eqFilters{};
        bool eqFiltersKnown = false;
        std::array<HeadsetState::ExtraEq, kExtraEqualizers> extraEq{};
    };
    struct UndoStep {
        bool headset = false;
        EqChannel channel = EqChannel::Game;
        EqChannelSettings eq;
        HeadsetEqSnapshot headsetEq;
    };
    UndoStep ChannelUndoStep(EqChannel channel);
    UndoStep HeadsetUndoStep() const;
    // Keeps the state before a change; a press (a drag included) or a quick run of wheel notches is one step.
    void RecordUndo(UndoStep before);
    // Undoes or redoes the last step of the equalizer page in view; false when there is none.
    bool UndoEqualizer(bool redo);
    void RestoreHeadsetEq(const HeadsetEqSnapshot& before);
    // The headset's equalizer changes, each kept for Ctrl+Z.
    void SetHeadsetPreset(int index);
    void SetHeadsetCurve(const EqCurve& curve);
    void SetHeadsetFilters(const EqFilters& filters);
    void SetExtraPreset(ExtraEqualizer which, int index);
    void SetExtraCurve(ExtraEqualizer which, const EqCurve& curve);

    // Settings (panel_settings.cpp).
    void DrawSettings();
    void DrawGeneral();
    void DrawWelcome();
    void DrawRoutingChannel();
    void DrawAppearance();
    void DrawBalancedIconPreview(BalancedIcon icon, D2D1_POINT_2F center);
    // The battery's color for a charge of 0..1 as the tint chooses.
    D2D1_COLOR_F BatteryTintColor(BatteryTint tint, float charge) const;
    // A choice between the theme's color and the charge for a battery; edit sets it.
    CardRow BatteryTintItem(uint32_t id, BatteryTint tint, std::function<void(Settings&, BatteryTint)> edit);
    void DrawColorPicker();
    // Applies a color from the picker to the accent or, while it edits Chat, to the Chat color.
    void SetPickedColor(uint32_t rgb);
    void DrawAudio();
    void DrawHotkeys();
    void DrawNotifications();
    void DrawProfiles();
    void DrawProfileEdit();
    void DrawAddOns();
    void DrawAddOnEdit();
    void DrawAddOnEvent();
    void DrawAddOnDisplay();
    AddOnSettings* EditedAddOn();
    // Changes the edited add-on, or the screen of its edited event starting from what it shows now.
    void EditAddOn(std::function<void(AddOnSettings&)> change);
    void EditAddOnScreen(std::function<void(AddOnEventScreen&)> change);
    void DrawPixelIcon(int icon, D2D1_POINT_2F center, float pixel, D2D1_COLOR_F color);
    std::wstring AddOnStatus(size_t index) const;
    std::wstring ScreenSummary(const AddOnEventScreen& screen) const;
    std::wstring DisplaySummary(const AddOnDisplay& display) const;
    void Edit(const std::function<void(Settings&)>& edit);

    // Microphone chain (panel_mic.cpp).
    void DrawMicrophone();
    // Plugin list of a chain (panel_plugins.cpp).
    float DrawPlugins(float top, EqChannel channel);
    void DrawOutputs();
    bool MicSlot() const { return pickSlot_ == RoutingSlot::MicInput || pickSlot_ == RoutingSlot::MicOutput; }
    // The picker chooses for one channel's page rather than for the Audio page.
    bool ChannelSlot() const {
        return pickSlot_ == RoutingSlot::Game || pickSlot_ == RoutingSlot::Chat || pickSlot_ == RoutingSlot::Media ||
               pickSlot_ == RoutingSlot::GameOutput || pickSlot_ == RoutingSlot::ChatOutput ||
               pickSlot_ == RoutingSlot::MediaOutput;
    }
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
    int tourStep_ = 0;  // The step of the tour on Page::Welcome.
    std::map<Page, float> scroll_;  // Scroll offset per page.
    Levels levels_;
    bool balanceDial_ = false;
    bool chatMuted_ = false;
    bool gameMuted_ = false;
    std::wstring device_;
    std::wstring deviceDetail_;
    std::wstring connection_;
    std::wstring transferNote_;  // How the last settings export or import went; shown until the page closes.
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
    AddOnsView addOns_;
    MicView mic_;
    std::array<PluginChainView, 4> pluginChains_;  // By EqChannel.
    EqChannel pluginDialog_ = EqChannel::Mic;      // The chain the plugin file dialog adds to.
    std::vector<OutputView> outputs_;
    UpdateView update_;
    std::vector<AudioEndpoint> endpoints_;
    RoutingSlot pickSlot_ = RoutingSlot::Game;
    int routingChannel_ = 0;  // Page::RoutingChannel: 0 Game, 1 Chat, 2 Media.
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
    int headsetBand_ = 0;                   // Band selected in the headset's filter graph.
    int headsetEqView_ = 0;                 // The Headset tab's equalizer: 0 the main one, 1 + ExtraEqualizer.
    std::vector<UndoStep> undo_;            // Oldest first.
    std::vector<UndoStep> redo_;
    uint64_t gesture_ = 0;                  // Counts presses, so the steps of one press go together.
    uint64_t undoGesture_ = 0;              // The press the last step was kept for.
    ULONGLONG undoTick_ = 0;                // When it was kept.
    bool clicking_ = false;                 // A click's action is running.
    std::vector<NamedEq> eqLibrary_[3];     // Built-in, Sonar and own presets of eqLibraryChannel_.
    EqChannel eqLibraryChannel_ = EqChannel::Game;
    struct MenuState {
        bool open = false;
        D2D1_RECT_F anchor{};
        std::vector<MenuItem> items;
        int selected = -1;
        std::function<void(int)> choose;
        float scroll = -1.0f;  // Negative: brings the selected item into view on the next frame.
        uint32_t ownerId = 0;  // The dropdown that opened it, drawn as open meanwhile.
        bool searchable = false;
        std::wstring search;   // Typed while it is open.
        bool searchSelected = false;  // Ctrl+A selected the whole search; the next key replaces it.
    } menu_;
    std::wstring profileExe_;               // Profile being edited.
    std::wstring addOnPath_;                // Add-on being edited.
    std::wstring addOnEvent_;               // Its event whose screen is being edited.
    size_t addOnDisplay_ = 0;               // The display of that screen being edited.
    std::wstring hexInput_;                 // Typed custom color.
    float pickerHue_ = 0.0f;                // Custom color being edited, OKLCH hue in degrees.
    float pickerChroma_ = 0.12f;
    bool pickerChat_ = false;               // The color picker edits the Chat color, not the accent.

    struct DragState {
        uint32_t list = 0;  // The list being dragged in; 0 when none.
        size_t index = 0;
        float startY = 0.0f;
        float pointerY = 0.0f;
        float grabOffset = 0.0f;  // Pointer to the top of the lifted row.
        bool moved = false;       // Past the threshold: a drag, not a click.
        float rowHeight = 0.0f;
        float listTop = 0.0f;
        size_t count = 0;
    } drag_;
    struct FieldState {
        uint32_t id = 0;  // 0 when no field is being edited.
        std::wstring text;
        size_t caret = 0;
        size_t anchor = 0;  // The other end of the selection; equal to caret when nothing is selected.
        std::function<bool(const std::wstring&)> commit;
        D2D1_RECT_F rect{};   // Where the field is, for placing the caret with the mouse.
        float textLeft = 0.0f;
        bool numeric = true;  // A number: centered, at most 16 characters. Otherwise a name.
        // Ctrl+Z and Ctrl+Y while it is open: the text before each change since it opened.
        struct Snapshot {
            std::wstring text;
            size_t caret = 0;
            size_t anchor = 0;
        };
        std::vector<Snapshot> undo;
        std::vector<Snapshot> redo;
        bool typing = false;  // The last change typed a character; the next one joins its step.
    } field_;
    bool pinned_ = false;
    Meters meters_;            // Shown levels: the peaks, falling back slowly.
    float micHold_ = 0.0f;     // Highest microphone peak lately, held for a moment.
    ULONGLONG micHoldTick_ = 0;
    std::map<Page, float> scrollMax_;  // Scroll limit of each page as of its last frame.

    std::vector<Hit> hits_;
    std::vector<D2D1_RECT_F> hitClip_;
    // Focus stops of the last frame in drawing order, also those scrolled out of view.
    struct Focusable {
        uint32_t id = 0;
        D2D1_RECT_F rect{};  // Unclipped.
        D2D1_RECT_F clip{};  // Where it can be seen; the whole panel outside scrolling areas.
        bool pageScroll = false;  // Inside the page's own scrolling area.
        bool inMenu = false;
        std::function<void()> click;
        std::function<void(int notches)> wheel;
    };
    std::vector<Focusable> focusables_;
    uint32_t focusId_ = 0;
    bool focusVisible_ = false;   // The ring shows only while the keyboard moves the focus.
    bool drawingMenu_ = false;
    size_t pageScrollDepth_ = 0;  // Depth of hitClip_ that is the page's scrolling area; 0 for none.
    uint32_t hotId_ = 0;
    uint32_t pressedId_ = 0;
    bool pressStarting_ = false;  // True while a hit's drag runs for the press itself, not a move.
    float scrollGrab_ = 0.0f;     // Where a dragged scrollbar thumb was grabbed, from its top.
    ULONGLONG saveDeadline_ = 0;  // When a pending headset save goes out; 0 without one.
    unsigned saveDelayMs_ = 0;    // The wait it counts down from.
    ULONGLONG savedAt_ = 0;       // When the last save went out, for Done; 0 once that has faded.
    uint32_t dragId_ = 0;  // Equalizer handle being dragged, and where it was grabbed.
    float dragOffsetX_ = 0.0f;
    float dragOffsetY_ = 0.0f;
    bool trackingMouse_ = false;
    int wheelRemainder_ = 0;

    struct Motion {
        float value;
        float velocity;
    };
    std::unordered_map<uint64_t, Motion> animations_;
    FrameClock frames_;
    bool animating_ = false;
    float frameSeconds_ = 0.0f;
    LARGE_INTEGER lastFrame_{};
    LARGE_INTEGER frequency_{};

    std::unordered_map<std::wstring, Microsoft::WRL::ComPtr<ID2D1Bitmap>> icons_;
};

}  // namespace ui
