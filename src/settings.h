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
    std::wstring output;  // Empty: the connected SteelSeries headset.
    // The Game and Chat devices are enabled and made the Windows defaults only while the mode runs.
    bool manageDevices = true;
    std::wstring savedDefault;         // Default output and communications device before the mode
    std::wstring savedCommunications;  // took over; restored when it stops.
    // Apps are sent to the Game or Chat device by their group (per-app device in Windows).
    bool autoRoute = true;
    // Executables that got a per-app device from OpenChatMix; cleared again when the mode stops.
    std::vector<std::wstring> routedApps;
};

struct AppearanceSettings {
    ThemeMode theme = ThemeMode::System;
    bool windowsAccent = false;  // Follow the Windows accent color instead of `accent`.
    uint32_t accent = kDefaultAccent;
    Backdrop backdrop = Backdrop::Acrylic;
    bool reduceMotion = false;
    TrayStyle tray = TrayStyle::Color;

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

struct BatteryAlertSettings {
    bool enabled = true;
    int threshold = 15;  // Percent.
};

// Applied while the game runs and undone when it exits.
struct GameProfile {
    std::wstring exe;
    std::wstring path;
    std::optional<int> balance;  // ChatMix balance as in balance.h, in percent.
    std::wstring gameEq;         // Game equalizer preset (virtual device mode); empty: unchanged.
    int headsetEq = -1;          // Headset equalizer preset index; -1: unchanged.

    bool operator==(const GameProfile&) const = default;
};

// Order matches the panel.
enum class IdleAnimationKind { Off, Stars, Bounce, Clock, Life, Blank, Image };

// Plays on the headset display while no sound plays, against OLED burn-in.
struct IdleScreenSettings {
    IdleAnimationKind animation = IdleAnimationKind::Off;
    int delaySeconds = 60;
    std::wstring image;  // Image or GIF for IdleAnimationKind::Image.
};

struct Settings {
    std::map<std::wstring, AppRule, NoCaseLess> apps;  // Executable name -> rule; Game apps are not stored.
    Levels levels;                                     // Restored on start and synced to the device.
    std::array<Hotkey, kHotkeyCount> hotkeys{};
    Language language = Language::English;
    bool showPopup = true;                             // On-screen popup when hotkeys change the balance.
    bool dialPopup = true;                             // Popup for the headset wheel on headsets without a display.
    bool warnGg = true;                                // Notify when SteelSeries GG takes over the headset.
    bool mediaGroup = true;                            // Offer the Media group outside the virtual device mode.
    bool equalizer = true;                             // Equalizer tab and processing in virtual device mode.
    AppearanceSettings appearance;
    RoutingSettings routing;
    IdleScreenSettings idleScreen;
    EqChannelSettings gameEq;
    EqChannelSettings chatEq;
    DuckingSettings ducking;
    BatteryAlertSettings battery;
    std::vector<GameProfile> profiles;
    bool checkUpdates = true;                          // Look for a newer release on GitHub once a day.
    long long lastUpdateCheck = 0;                     // Unix time of the last successful check.
    bool log = false;                                  // Write OpenChatMix.log into the config directory.
    bool firstRun = false;                             // The ini file did not exist when loaded.

    AppGroup GroupOf(const std::wstring& exe) const;
    // The group that applies now: Media counts as Game while it is turned off or in virtual device mode.
    AppGroup EffectiveGroupOf(const std::wstring& exe) const;
    bool MediaAvailable() const { return mediaGroup && !routing.enabled; }
    void SetGroup(const std::wstring& exe, AppGroup group, const std::wstring& path);
};

std::wstring ExePath();
std::wstring ExeDirectory();
// Next to the executable when OpenChatMix.ini already lives there (portable mode),
// otherwise %APPDATA%\OpenChatMix. The directory is created on demand.
std::wstring ConfigDirectory();

Settings LoadSettings(const std::wstring& path);
void SaveSettings(const std::wstring& path, const Settings& settings);

// Last known native settings of one headset model, so values the device cannot report survive restarts.
void LoadHeadsetState(const std::wstring& path, uint16_t productId, const HeadsetFeatures& features, HeadsetState& state);
void SaveHeadsetState(const std::wstring& path, uint16_t productId, const HeadsetState& state);

// Autostart via HKCU\Software\Microsoft\Windows\CurrentVersion\Run.
bool IsAutostartEnabled();
bool SetAutostart(bool enable);
// Replaces the Run entry of the NovaChatMix prototype; returns true if it existed.
bool RemoveLegacyAutostart();
