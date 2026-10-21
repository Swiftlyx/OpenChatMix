#pragma once

#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "appearance.h"
#include "balance.h"
#include "headset.h"
#include "hotkeys.h"
#include "i18n.h"
#include "mic_dsp.h"
#include "parametric_eq.h"

enum class AppGroup {
    Game,   // Follows the Game side of ChatMix; default for unknown apps.
    Chat,   // Follows the Chat side of ChatMix.
    Media,  // Never touched by ChatMix.
};

struct NoCaseLess {
    bool operator()(const std::wstring& a, const std::wstring& b) const;
};

struct AppRule {
    AppGroup group = AppGroup::Game;
    std::wstring path;  // Last known executable path, used for the icon; may be empty.
};

// Virtual device mode: Game and Chat come from two playback devices and are mixed into the output.
struct RoutingSettings {
    bool enabled = false;
    std::wstring game;    // Endpoint IDs.
    std::wstring chat;
    std::wstring media;   // A virtual device of its own for the Media group, chosen by hand; empty: none.
    std::wstring output;  // Empty: the connected SteelSeries headset.
    // Where each channel plays; empty: the output above.
    std::wstring gameOutput;
    std::wstring chatOutput;
    std::wstring mediaOutput;
    // The Game and Chat devices are enabled and made the Windows defaults only while the mode runs.
    bool manageDevices = true;
    std::wstring savedDefault;         // Default output and communications device before the mode
    std::wstring savedCommunications;  // took over; restored when it stops.
    // Apps are sent to the Game or Chat device by their group (per-app device in Windows).
    bool autoRoute = true;
    // Executables that got a per-app device from OpenChatMix; cleared again when the mode stops.
    std::vector<std::wstring> routedApps;
};

// Playback devices in the order they are preferred, and whether OpenChatMix follows that order.
struct OutputSettings {
    std::vector<std::wstring> order;     // Endpoint IDs, most preferred first; unlisted devices come after.
    std::vector<std::wstring> excluded;  // Endpoint IDs never switched to automatically or by the hotkey.
    bool switchOnLoss = false;        // The output disconnects: take the most preferred device left.
    bool returnToPreferred = false;   // A more preferred device connects: switch back to it.

    bool operator==(const OutputSettings&) const = default;
};

struct AppearanceSettings {
    ThemeMode theme = ThemeMode::System;
    bool windowsAccent = true;  // Follow the Windows accent color instead of `accent`.
    uint32_t accent = kDefaultAccent;
    // Seed of the Chat side; empty pairs Chat with the accent automatically. The theme keeps it within
    // the soft range and apart from the Game hue.
    std::optional<uint32_t> chatAccent;
    Backdrop backdrop = Backdrop::Acrylic;
    ContrastLevel contrast = ContrastLevel::Standard;
    bool reduceMotion = false;
    TrayStyle tray = TrayStyle::Color;
    BalancedIcon balancedIcon = BalancedIcon::Bars;
    BatteryTint battery = BatteryTint::Theme;      // The battery beside the headset's name.
    BatteryTint trayBattery = BatteryTint::Theme;  // The battery mark of the tray icon.

    bool operator==(const AppearanceSettings&) const = default;
};

// Equalizer of one virtual device stream.
struct EqChannelSettings {
    bool enabled = false;
    std::wstring preset;  // Name of the last chosen preset; empty once the curve was edited.
    ParametricEq eq = FlatEq();

    bool operator==(const EqChannelSettings&) const = default;
};

// Lowers the Game side while there is sound on the Chat side.
struct DuckingSettings {
    bool enabled = false;
    int amount = 50;  // Percent taken from the Game level.

    bool operator==(const DuckingSettings&) const = default;
};

// A plugin in the microphone chain or on a virtual device channel. Its settings live in PluginStates\<id>.bin in
// the config directory.
struct ChainPlugin {
    std::wstring path;
    std::wstring id;
    bool enabled = true;

    bool operator==(const ChainPlugin&) const = default;
};

// The microphone through noise suppression, equalizer, compressor and plugins into a virtual cable.
struct MicSettings {
    bool enabled = false;
    std::wstring input;   // Recording device ID; empty: the Windows default communications microphone.
    std::wstring output;  // Playback side of a virtual cable; empty: the VB-Audio "CABLE Input".
    bool keepLevels = true;  // Holds microphone and cable levels at 100%.
    NoiseSuppression noise = NoiseSuppression::Standard;
    MicCompressor compressor = MicCompressor::Off;
    MicStageOrder order = kDefaultMicOrder;
    bool monitor = false;  // Also plays the result on the output, to listen to it.
    std::vector<ChainPlugin> plugins;

    bool operator==(const MicSettings&) const = default;
};

struct BatteryAlertSettings {
    bool enabled = true;
    int threshold = 15;  // Percent.
};

// Applied while the game runs and undone when it exits.
struct GameProfile {
    std::wstring exe;
    std::wstring path;
    std::optional<int> balance;  // ChatMix balance as in balance.h, in percent.
    std::wstring gameEq;         // Equalizer presets by name (virtual device mode); empty: unchanged.
    std::wstring chatEq;
    std::wstring mediaEq;
    int headsetEq = -1;          // Headset equalizer preset index; -1: unchanged.

    bool operator==(const GameProfile&) const = default;
};

// Order matches the panel.
enum class IdleAnimationKind { Off, Stars, Clock, Life, Blank, Image };

// Plays on the headset display while no sound plays, against OLED burn-in.
// Corner of an icon on the headset display; Off hides it.
enum class IconCorner { Off, TopLeft, TopRight, BottomLeft, BottomRight };

// Bars on the headset display that follow the sound while something plays.
struct VisualizerSettings {
    bool enabled = false;
    bool mediaOnly = true;  // While an app tells Windows it plays music or video; otherwise for any sound.

    bool operator==(const VisualizerSettings&) const = default;
};

struct IdleScreenSettings {
    IdleAnimationKind animation = IdleAnimationKind::Off;
    int delaySeconds = 60;
    std::wstring image;  // Image or GIF for IdleAnimationKind::Image.
    IconCorner bluetoothIcon = IconCorner::TopRight;  // While the base station's Bluetooth is on.
};

// One step of what an add-on's event puts on the headset display, for lengthMs: an icon with a bold heading and
// wrapped text, or an image. Heading and text are templates: {title}, {message}, {user} and {channel} take the
// event's fields.
struct AddOnDisplay {
    int icon = 0;  // One of the add-on icons; 0 none.
    std::wstring heading;
    std::wstring text;
    std::wstring image;  // An image or GIF shown instead of the heading and text.
    int lengthMs = 3000;

    bool operator==(const AddOnDisplay&) const = default;
};

// What one event of an add-on shows: its displays in turn, a number of times or as long as the event lasts.
struct AddOnEventScreen {
    bool enabled = true;
    bool whileOn = false;  // Repeats as long as the event lasts, e.g. while someone talks.
    int times = 1;
    std::vector<AddOnDisplay> displays;

    bool operator==(const AddOnEventScreen&) const = default;
};

// A program that talks SteelSeries GameSense, which OpenChatMix runs and shows on the headset display while
// SteelSeries GG is closed. The user picks the program.
struct AddOnSettings {
    std::wstring path;
    bool enabled = true;
    std::map<std::wstring, std::wstring> options;
    std::map<std::wstring, AddOnEventScreen> screens;  // Screens the user changed, by event; others are defaults.

    bool operator==(const AddOnSettings&) const = default;
};

// Add-ons OpenChatMix knows how to show without the program describing its screens.
enum class AddOnKind { Generic, Discord };
AddOnKind AddOnKindOf(const std::wstring& path);

// Where popups show on the primary monitor.
enum class PopupCorner { TopRight, TopLeft, BottomRight, BottomLeft };

struct Settings {
    std::map<std::wstring, AppRule, NoCaseLess> apps;  // Executable name -> rule; Game apps are not stored.
    Levels levels;                                     // Restored on start and synced to the device.
    int gameVolume = 100;                              // Volume of each side in percent, on top of ChatMix;
    int chatVolume = 100;                              // Windows volumes stay untouched.
    std::array<Hotkey, kHotkeyCount> hotkeys{};
    UINT wheelModifiers = 0;                           // MOD_* held to turn the balance with the mouse wheel; 0 off.
    Language language = Language::English;
    bool showPopup = true;                             // On-screen popup when hotkeys change the balance.
    bool dialPopup = true;                             // Popup for the headset wheel on headsets without a display.
    bool volumePopup = true;                           // Popup for the volume knob of a GameDAC or base station.
    bool knobPopup = true;                             // Balance popup for the knob of a GameDAC or base station.
    bool popups = true;                                // Popups at all; the low battery warning has its own switch.
    PopupCorner popupCorner = PopupCorner::TopRight;
    bool warnGg = true;                                // Notify when SteelSeries GG takes over the headset.
    bool mediaGroup = false;                           // Offer the Media group outside the virtual device mode.
    bool equalizer = false;                            // Equalizer tab and processing in virtual device mode.
    AppearanceSettings appearance;
    RoutingSettings routing;
    OutputSettings outputs;
    IdleScreenSettings idleScreen;
    VisualizerSettings visualizer;
    EqChannelSettings gameEq;
    EqChannelSettings chatEq;
    EqChannelSettings micEq;
    EqChannelSettings mediaEq;
    // Plugins of the virtual device channels; they run with the equalizer, before the channel's volume.
    std::vector<ChainPlugin> gamePlugins;
    std::vector<ChainPlugin> chatPlugins;
    std::vector<ChainPlugin> mediaPlugins;
    MicSettings mic;
    DuckingSettings ducking;
    BatteryAlertSettings battery;
    std::vector<GameProfile> profiles;
    bool checkUpdates = true;                          // Look for a newer release on GitHub once a day.
    long long lastUpdateCheck = 0;                     // Unix time of the last successful check.
    bool log = false;                                  // Write OpenChatMix.log into the config directory.
    // Heartbeat size learned per SteelSeries GG screen helper, so it is known right after a start.
    std::map<std::wstring, std::vector<unsigned long long>> ggHeartbeats;
    std::vector<AddOnSettings> addOns;
    bool firstRun = false;                             // The ini file did not exist when loaded.

    AppGroup GroupOf(const std::wstring& exe) const;
    // The group that applies now: Media counts as Game while it is turned off or in virtual device mode.
    AppGroup EffectiveGroupOf(const std::wstring& exe) const;
    // In virtual device mode Media needs a device of its own; without one it plays with Game.
    bool MediaAvailable() const { return mediaGroup && (!routing.enabled || !routing.media.empty()); }
    void SetGroup(const std::wstring& exe, AppGroup group, const std::wstring& path);
};

EqChannelSettings& EqStateOf(Settings& settings, EqChannel channel);
const EqChannelSettings& EqStateOf(const Settings& settings, EqChannel channel);
// The plugins of a channel; Mic gives the microphone chain's.
std::vector<ChainPlugin>& PluginsOf(Settings& settings, EqChannel channel);
const std::vector<ChainPlugin>& PluginsOf(const Settings& settings, EqChannel channel);

std::wstring ExePath();
std::wstring ExeDirectory();
// Next to the executable when OpenChatMix.ini already lives there (portable mode),
// otherwise %APPDATA%\OpenChatMix. The directory is created on demand.
std::wstring ConfigDirectory();
// Writes the Log key at once, for the switch in Settings; Save keeps a Log key that exists.
void SaveLogSetting(const std::wstring& path, bool log);

Settings LoadSettings(const std::wstring& path);
void SaveSettings(const std::wstring& path, const Settings& settings);

// Apps that start in the Chat group (Discord, the Xbox party chat).
bool IsDefaultChatApp(const std::wstring& exe);

// Last known native settings of one headset model, so values the device cannot report survive restarts.
void LoadHeadsetState(const std::wstring& path, uint16_t productId, const HeadsetFeatures& features, HeadsetState& state);
void SaveHeadsetState(const std::wstring& path, uint16_t productId, const HeadsetState& state);

// Autostart via HKCU\Software\Microsoft\Windows\CurrentVersion\Run.
bool IsAutostartEnabled();
bool SetAutostart(bool enable);
// Replaces the Run entry of the NovaChatMix prototype; returns true if it existed.
bool RemoveLegacyAutostart();
