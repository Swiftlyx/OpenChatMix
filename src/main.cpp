#include <windows.h>

#include <dbt.h>
#include <hidsdi.h>
#include <objbase.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <windowsx.h>
#include <wtsapi32.h>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>

#include "app.h"
#include "app_info.h"
#include "app_routing.h"
#include "audio_bridge.h"
#include "audio_devices.h"
#include "chatmix_device.h"
#include "hang_watch.h"
#include "headset.h"
#include "hid_probe.h"
#include "i18n.h"
#include "icon.h"
#include "idle_animations.h"
#include "log.h"
#include "mic_chain.h"
#include "oled.h"
#include "output_priority.h"
#include "parametric_eq.h"
#include "plugin_host.h"
#include "reorder.h"
#include "resource.h"
#include "session_mixer.h"
#include "addon_events.h"
#include "audio_tap.h"
#include "media_sessions.h"
#include "visualizer.h"
#include "addon_screens.h"
#include "addons.h"
#include "gg_screen.h"
#include "settings.h"
#include "settings_transfer.h"
#include "ui/osd.h"
#include "ui/tray_flyout.h"
#include "ui/panel.h"
#include "ui/spring.h"
#include "ui/theme.h"
#include "updater.h"

namespace {

constexpr UINT kTrayId = 1;
constexpr int kHotkeyIdBase = 1;
constexpr UINT_PTR kTimerReconnect = 1;
constexpr UINT_PTR kTimerRebindAudio = 2;
constexpr UINT_PTR kTimerGgWatch = 3;
constexpr UINT_PTR kTimerSaveHeadset = 4;
constexpr UINT_PTR kTimerCommitHeadset = 5;
constexpr UINT_PTR kTimerRoutingRetry = 6;
constexpr UINT_PTR kTimerIdleScreen = 7;
constexpr UINT_PTR kTimerVerifyHeadset = 8;
constexpr UINT_PTR kTimerDucking = 9;
constexpr UINT_PTR kTimerSaveSettings = 10;
constexpr UINT_PTR kTimerSendLevels = 11;
constexpr UINT_PTR kTimerUpdateCheck = 12;
constexpr UINT_PTR kTimerMicRetry = 13;
constexpr UINT_PTR kTimerSavePlugins = 14;
constexpr UINT_PTR kTimerTrayMotion = 15;
constexpr UINT_PTR kTimerTrayCard = 16;
constexpr UINT_PTR kTimerAddOnScreen = 17;
constexpr UINT_PTR kTimerOutputBattery = 18;
constexpr UINT kTrayFrameMs = 15;  // About one frame of a 60 Hz display; the tray needs no more.
constexpr UINT kMicRetryMs = 3000;
constexpr UINT kSavePluginsMs = 60 * 1000;  // Plugin settings are kept safe while their windows are used.
constexpr UINT kFirstUpdateCheckMs = 60 * 1000;      // Leaves the start to the headset and audio.
constexpr UINT kUpdateTimerMs = 60 * 60 * 1000;      // Looks hourly whether a day has passed.
constexpr long long kUpdateIntervalSeconds = 24 * 60 * 60;
constexpr DWORD kUpdateWaitMs = 15000;               // The updated instance waits this long for the old one.
constexpr UINT kDuckingTickMs = 100;
constexpr ULONGLONG kDuckingHoldMs = 600;  // Chat counts as active this long after its last sound.
constexpr float kChatActivePeak = 0.02f;   // About -34 dBFS.
constexpr UINT kSaveSettingsDelayMs = 400;  // Edits such as equalizer drags are saved once they settle.
constexpr wchar_t kGlyphMute = 0xE74F;
constexpr wchar_t kGlyphChat = 0xE8BD;
constexpr wchar_t kGlyphGame = 0xE7FC;
constexpr wchar_t kGlyphMedia = 0xE767;  // Volume, as the Media group.
constexpr wchar_t kGlyphSpeakers = 0xE7F5;
constexpr wchar_t kGlyphMicrophone = 0xE720;
constexpr wchar_t kGlyphScreensaver = 0xE7F4;  // TVMonitor.
constexpr wchar_t kGlyphBattery0 = 0xEBA0;  // EBA0..EBAA: battery 0-100% in steps of 10.
constexpr wchar_t kGlyphTrayOpen = 0xF4C3;
constexpr wchar_t kGlyphTraySettings = 0xE713;
constexpr wchar_t kGlyphTrayReconnect = 0xE72C;
constexpr wchar_t kGlyphTrayStartup = 0xE770;
constexpr wchar_t kGlyphTrayQuit = 0xE7E8;
constexpr UINT kSaveHeadsetDelayMs = 1000;
constexpr UINT kCommitHeadsetDelayMs = 600;  // Device save commands write flash; wait until edits settle.
constexpr UINT kRoutingRetryMs = 3000;
constexpr UINT kReconnectDelayMs = 1500;  // Lets all interfaces of a freshly plugged device enumerate.
constexpr UINT kResumeDelayMs = 3000;     // Headsets re-enumerate after sleep.
constexpr UINT kRebindDelayMs = 500;      // Coalesces bursts of endpoint notifications.
constexpr UINT kGgWatchMs = 5000;
constexpr UINT kIdleCheckMs = 250;
constexpr UINT kOutputBatteryPollMs = 60000;  // A Bluetooth output's battery, as Windows reports it.
constexpr ULONGLONG kIdlePreviewMs = 8000;  // A newly chosen animation shows right away for this long.
constexpr float kSilencePeak = 0.0001f;     // About -80 dBFS.
// SteelSeries GG's apps draw on the same display; the screensaver holds still while they show something, for as
// long as GG's own settings show it, with a margin.
constexpr ULONGLONG kGgMessageMs = 6000;  // Discord: a message's title and text 2 s each, a voice event 3 s.
constexpr ULONGLONG kGgTrackMs = 3500;    // Now Playing and Spotify: the track for 3 s.
constexpr ULONGLONG kGgFrameMs = 1000;    // Audio Visualizer: frames follow each other while sound plays.
constexpr ULONGLONG kVisualizerLingerMs = 2000;  // The visualizer stays this long after the sound or media stops.
// The GameDAC's own screen after the headset or the app is used, before the visualizer or the screensaver turned on
// by the hotkey come back.
constexpr ULONGLONG kDisplayYieldMs = 3000;
constexpr ULONGLONG kMediaCheckMs = 1000;        // How often Windows is asked whether media plays.
// The headset is asked for its settings again after changes, so the panel shows what it really applied.
constexpr UINT kVerifyAfterCommandMs = 500;
constexpr UINT kVerifyAfterDialMs = 1500;
constexpr UINT kVerifyAfterBalanceMs = 600;
// The GameDAC can miss balance commands that follow each other closely, e.g. while the slider is dragged.
constexpr ULONGLONG kLevelsSendGapMs = 40;
constexpr DWORD kLegacyExitTimeoutMs = 3000;

// SteelSeries GG takes over the headset while any of these run.
constexpr const wchar_t* kGgProcesses[] = {L"SteelSeriesGG.exe", L"SteelSeriesEngine.exe", L"SteelSeriesSonar.exe"};

// Discord itself: its sound and its taskbar flashes come with what GG's Discord app shows.
constexpr const wchar_t* kDiscordApps[] = {L"Discord.exe", L"DiscordPTB.exe", L"DiscordCanary.exe"};

struct GgScreenApps {
    bool discord = false;     // Messages, and who talks in a voice channel.
    bool track = false;       // The track, once playback starts.
    bool visualizer = false;  // Bars that follow the sound while it plays.
    std::wstring names;

    bool Any() const { return discord || track || visualizer; }
    bool operator==(const GgScreenApps&) const = default;
};

class Host final : public ui::PanelHost {
public:
    void OnBalanceChanged(int steps) override;
    void OnGroupChanged(const ui::AppItem& app, AppGroup group) override;
    void OnAppRemoved(const ui::AppItem& app) override;
    std::vector<ui::AppItem> RunningApps() override;
    std::wstring AppName(const std::wstring& path) override;
    void OnHotkeyRecording(bool active) override;
    void OnHotkeyChanged(int action, Hotkey hotkey) override;
    void OnAutostartChanged(bool enabled) override;
    void OnLanguageChanged(Language language) override;
    void OnWarningDismissed() override;
    void OnHeadsetSetting(uint8_t command, int value) override;
    void OnEqualizerPreset(int index) override;
    void OnEqualizerCurve(const EqCurve& curve) override;
    void OnRoutingEnabled(bool enabled) override;
    void OnRoutingDevice(ui::RoutingSlot slot, const std::wstring& id) override;
    void OnRoutingManage(bool enabled) override;
    void OnIdleAnimation(IdleAnimationKind animation) override;
    void OnIdleDelay(int seconds) override;
    void OnIdleImage(const std::wstring& path) override;
    void OnIdleTest() override;
    void OnIdleBluetoothIcon(IconCorner corner) override;
    void OnAddOnPreview(const std::wstring& path, const std::wstring& event) override;
    void OnSettingsEdit(const std::function<void(Settings&)>& edit) override;
    std::vector<AudioEndpoint> PlaybackDevices() override;
    std::vector<AudioEndpoint> RecordingDevices() override;
    void OnMicPluginAdd(const std::wstring& path) override;
    void OnMicPluginRemove(size_t index) override;
    void OnMicPluginMove(size_t from, size_t to) override;
    void OnMicPluginEditor(size_t index) override;
    void OnMicPluginDeepFilter(size_t index, const DeepFilterSettings& settings) override;
    void OnOutputChoose(const std::wstring& id) override;
    void OnOutputMove(size_t from, size_t to) override;
    void OnOutputExclude(const std::wstring& id, bool excluded) override;
    void OnCheckForUpdates() override;
    void OnInstallUpdate() override;
    void OnOpenReleasePage() override;
    void OnQuit() override;
    bool OnSettingsExport(const std::wstring& path) override;
    bool OnSettingsImport(const std::wstring& path) override;
    ui::Meters ReadMeters() override;
};

// The tray icon on springs: the bars follow the levels and morph to and from the balanced mark.
struct TrayMotion {
    float game = 100.0f;
    float gameVelocity = 0.0f;
    float chat = 100.0f;
    float chatVelocity = 0.0f;
    float morph = 0.0f;  // 0 the bars, 1 the mark.
    float morphVelocity = 0.0f;
    BalancedIcon mark = BalancedIcon::Bars;  // The mark the bars morph into or out of.
    bool running = false;
    LARGE_INTEGER last{};
};

struct App {
    HINSTANCE instance = nullptr;
    HWND window = nullptr;
    UINT taskbarCreated = 0;
    UINT shellHook = 0;  // RegisterShellHookWindow messages: a window flashed on the taskbar.
    HDEVNOTIFY deviceNotify = nullptr;
    HICON trayIcon = nullptr;
    std::wstring iniPath;
    Settings settings;
    ChatMixDevice device;
    HidProbe probe;  // Logs unsupported SteelSeries headsets while the log is on.
    HangWatch hangWatch;  // Writes a dump when the window's thread hangs, while the log is on.
    SessionMixer mixer;
    Levels levels;
    std::array<bool, kHotkeyCount> hotkeyFailed{};
    bool hotkeysSuspended = false;
    HHOOK mouseHook = nullptr;  // Installed while the mouse wheel turns the balance.
    int wheelRemainder = 0;     // Wheel delta short of a whole notch, from high-resolution wheels.
    bool ggRunning = false;
    bool ggDismissed = false;  // The user hid the warning for the current GG session.
    bool ggNotified = false;   // The tray notification was shown for the current GG session.
    const HeadsetFeatures* features = nullptr;
    HeadsetState headset;
    AudioRouter router;
    bool routingDevicesAcquired = false;  // The Game and Chat devices were switched on for the running mode.
    bool routingDeviceError = false;
    // The playback device ChatMix works on while no SteelSeries headset is connected; empty if it is virtual.
    std::wstring outputId;
    std::wstring outputName;
    EndpointLink outputLink = EndpointLink::Unknown;
    int outputBattery = -1;  // Percent for a Bluetooth output that reports it; -1 otherwise.
    OledDisplay display;
    std::shared_ptr<IdleAnimation> idleAnimation;
    MicChain mic;
    LevelKeeper micLevels;
    PluginThread pluginThread;
    // Loaded plugins of settings.mic.plugins, same order; null where loading failed. Owned and
    // destroyed on the plugin thread.
    std::vector<std::unique_ptr<AudioPlugin>> micPlugins;
    bool micPluginsLoaded = false;
    std::wstring micInputId;    // The microphone the running chain records from.
    std::wstring micMonitorId;  // Where the running chain is listened to; empty when it is not.
    std::set<std::wstring> knownOutputs;  // Connected playback devices at the last check.
    std::wstring lastOutput;              // The output in use at the last check.
    ULONGLONG lastSoundTick = 0;
    ULONGLONG lastActivityTick = 0;  // Changes made in OpenChatMix that the headset display shows.
    ULONGLONG idlePreviewStart = 0;
    ULONGLONG idlePreviewUntil = 0;
    bool idlePinned = false;  // Turned on by the hotkey: shows despite sound until the hotkey is pressed again.
    bool idleManual = false;  // Started by the Test button: shows despite sound until idleManualUntil.
    ULONGLONG idleManualUntil = 0;
    ULONGLONG idleHoldStart = 0;    // Something SteelSeries GG shows holds the screensaver still...
    ULONGLONG idleHoldUntil = 0;    // ...until this tick.
    GgScreenApps ggScreen;          // GG's apps that may draw on the headset display.
    std::vector<GgHelperProcess> ggHelpers;  // Their running helpers.
    GgScreenWatch ggWatch;
    // What OpenChatMix puts on the headset display: nothing (the GameDAC's own screen), the screensaver or the
    // visualizer. Add-on screens go over any of them.
    enum class DisplayShows { Device, Screensaver, Visualizer } displayShows = DisplayShows::Device;
    std::shared_ptr<AudioTap> audioTap = std::make_shared<AudioTap>();
    std::shared_ptr<IdleAnimation> visualizer;
    MediaSessions media;
    ULONGLONG mediaPlayingTick = 0;  // Last check that found media playing.
    ULONGLONG mediaCheckTick = 0;
    AddOnHost addOns;
    AddOnScreens addOnScreens;
    std::optional<AddOnCard> addOnCard;  // On the display now.
    bool idleShowing = false;
    std::optional<UpdateRelease> update;
    ui::UpdateView updateView;
    bool updateNotified = false;  // The tray already announced the available version.
    bool updateBalloon = false;   // The shown balloon is about the update.
    ULONGLONG levelsSentTick = 0;
    bool levelsUnverified = false;  // The last balance sent is not confirmed by a state reply yet.
    AppRouting appRouting;
    std::map<std::wstring, std::wstring, NoCaseLess> routedNow;  // Executable -> device set this session ("" default).
    bool chatMuted = false;
    bool gameMuted = false;
    bool mediaMuted = false;
    float duck = 0.0f;  // Share of the ducking amount applied to the Game side, 0-1.
    ULONGLONG chatActiveTick = 0;
    bool batteryWarned = false;
    std::wstring activeProfile;  // Executable of the game profile in effect.
    // What a game profile replaced, put back when the game exits.
    struct ProfileRestore {
        std::optional<Levels> levels;
        std::optional<EqChannelSettings> gameEq;
        std::optional<EqChannelSettings> chatEq;
        std::optional<EqChannelSettings> mediaEq;
        int headsetEq = -1;
    } profileRestore;
    TrayMotion tray;
    Host host;
    std::unique_ptr<ui::Panel> panel;
    std::unique_ptr<ui::Osd> osd;
    std::unique_ptr<ui::TrayFlyout> trayFlyout;
    std::map<std::wstring, std::wstring, NoCaseLess> names;  // Executable path -> display name.
    bool cleanedUp = false;
};

App g_app;

void Save() {
    SaveSettings(g_app.iniPath, g_app.settings);
}

void ScheduleSave() {
    SetTimer(g_app.window, kTimerSaveSettings, kSaveSettingsDelayMs, nullptr);
}

uint32_t AccentSeed() {
    const AppearanceSettings& appearance = g_app.settings.appearance;
    return appearance.windowsAccent ? ui::WindowsAccentColor() : appearance.accent;
}

bool TaskbarIsLight() {
    DWORD value = 0;
    DWORD size = sizeof(value);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return value != 0;
}

uint32_t PackRgb(D2D1_COLOR_F color) {
    auto channel = [](float v) { return static_cast<uint32_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    return (channel(color.r) << 16) | (channel(color.g) << 8) | channel(color.b);
}

struct BatteryReading {
    int level = -1;  // Percent; -1 unknown.
    bool charging = false;
};

// The battery the panel and the tray show: the SteelSeries headset's while it is connected and switched on,
// otherwise a Bluetooth output's as Windows reports it.
BatteryReading CurrentBattery() {
    if (g_app.device.IsOpen()) {
        const HeadsetState& headset = g_app.headset;
        return headset.battery >= 0 && !headset.headsetOff ? BatteryReading{headset.battery, headset.charging}
                                                           : BatteryReading{};
    }
    return {g_app.outputBattery, false};
}

// Theme colors, or the taskbar's own text color for the monochrome icon.
LevelIconColors TrayColors() {
    const bool light = TaskbarIsLight();
    if (g_app.settings.appearance.tray == TrayStyle::Monochrome) {
        return light ? LevelIconColors{0x1F1F1F, 0x1F1F1F, 0x1F1F1F} : LevelIconColors{0xFFFFFF, 0xFFFFFF, 0xFFFFFF};
    }
    const ui::Palette palette = ui::MakePalette(AccentSeed(), !light, g_app.settings.appearance.chatAccent,
                                                g_app.settings.appearance.contrast);
    const float charge = std::max(0, CurrentBattery().level) / 100.0f;
    const D2D1_COLOR_F battery = g_app.settings.appearance.trayBattery == BatteryTint::Theme
                                     ? palette.primary
                                     : ui::BatteryColor(charge, !light);
    return {PackRgb(palette.primary), PackRgb(palette.tertiary), PackRgb(battery)};
}

std::wstring DisplayName(const std::wstring& path, const std::wstring& exe) {
    if (path.empty()) {
        return AppDisplayName(path, exe);
    }
    auto it = g_app.names.find(path);
    if (it == g_app.names.end()) {
        it = g_app.names.emplace(path, AppDisplayName(path, exe)).first;
    }
    return it->second;
}

std::wstring DeviceName() {
    if (g_app.device.IsOpen()) {
        return g_app.device.Model()->name;
    }
    return g_app.outputName.empty() ? T(Text::NoHeadset) : g_app.outputName;
}

std::wstring LevelsText() {
    return std::wstring(T(Text::Game)) + L" " + std::to_wstring(g_app.levels.game) + L"% · " + T(Text::Chat) + L" " +
           std::to_wstring(g_app.levels.chat) + L"%";
}

using ProcessSet = std::set<std::wstring, NoCaseLess>;

// helpers: also collects SteelSeries GG's running screen helpers.
ProcessSet RunningProcesses(std::vector<GgHelperProcess>* helpers = nullptr) {
    ProcessSet processes;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return processes;
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry)) {
        processes.insert(entry.szExeFile);
        if (!helpers) {
            continue;
        }
        for (const GgScreenHelper& helper : kGgScreenHelpers) {
            if (_wcsicmp(helper.exe, entry.szExeFile) == 0) {
                helpers->push_back({entry.th32ProcessID, &helper});
            }
        }
    }
    CloseHandle(snapshot);
    return processes;
}

bool IsGgRunning(const ProcessSet& processes) {
    return std::any_of(std::begin(kGgProcesses), std::end(kGgProcesses),
                       [&](const wchar_t* name) { return processes.count(name) != 0; });
}

GgScreenApps FindGgScreenApps(const std::vector<GgHelperProcess>& helpers) {
    GgScreenApps apps;
    for (const GgScreenHelper& helper : kGgScreenHelpers) {
        if (std::none_of(helpers.begin(), helpers.end(),
                         [&](const GgHelperProcess& process) { return process.spec == &helper; })) {
            continue;
        }
        switch (helper.app) {
        case GgScreenApp::Discord: apps.discord = true; break;
        case GgScreenApp::Track: apps.track = true; break;
        case GgScreenApp::Visualizer: apps.visualizer = true; break;
        }
        apps.names += (apps.names.empty() ? L"" : L", ") + std::wstring(helper.name);
    }
    return apps;
}

bool IsDiscordApp(const std::wstring& exe) {
    return std::any_of(std::begin(kDiscordApps), std::end(kDiscordApps),
                       [&](const wchar_t* name) { return _wcsicmp(name, exe.c_str()) == 0; });
}

// What the tray icon moves toward: the levels, and the balanced mark while the balance is even. A new
// mark is reached through the bars: the current one first turns back into them.
void TrayTargets(float& game, float& chat, float& morph) {
    TrayMotion& tray = g_app.tray;
    game = static_cast<float>(g_app.levels.game);
    game = g_app.gameMuted ? 0.0f : game;
    chat = g_app.chatMuted ? 0.0f : static_cast<float>(g_app.levels.chat);
    const bool balanced = BalanceFromLevels(g_app.levels) == 0 && !g_app.chatMuted && !g_app.gameMuted;
    BalancedIcon wanted = balanced ? g_app.settings.appearance.balancedIcon : BalancedIcon::Bars;
    if (wanted == BalancedIcon::Battery && CurrentBattery().level < 0) {
        wanted = BalancedIcon::Bars;
    }
    morph = 0.0f;
    if (wanted != BalancedIcon::Bars && (tray.mark == wanted || tray.morph < 0.02f)) {
        tray.mark = wanted;
        morph = 1.0f;
    }
}

HICON TrayIconNow() {
    const TrayMotion& tray = g_app.tray;
    const BatteryReading reading = CurrentBattery();
    const BatteryMark battery{std::max(0, reading.level) / 100.0f, reading.charging};
    return CreateLevelIcon(GetSystemMetrics(SM_CXSMICON), tray.game, tray.chat, TrayColors(), tray.mark, tray.morph,
                           battery);
}

// Advances the tray springs by dt; returns whether they are still moving. With reduced motion, and
// for the first icon, everything jumps to its target.
bool StepTray(float dt) {
    TrayMotion& tray = g_app.tray;
    float game = 0.0f;
    float chat = 0.0f;
    float morph = 0.0f;
    TrayTargets(game, chat, morph);
    if (dt <= 0.0f || g_app.settings.appearance.reduceMotion) {
        tray.game = game;
        tray.chat = chat;
        tray.morph = morph;
        tray.gameVelocity = tray.chatVelocity = tray.morphVelocity = 0.0f;
        return false;
    }
    // Fast spatial springs, as Material 3 moves small components: quick, with a little life.
    ui::StepSpring(tray.game, tray.gameVelocity, game, ui::design::kSpringFastSpatial, dt);
    ui::StepSpring(tray.chat, tray.chatVelocity, chat, ui::design::kSpringFastSpatial, dt);
    ui::StepSpring(tray.morph, tray.morphVelocity, morph, ui::design::kSpringFastSpatial, dt);
    auto settled = [](float& value, float& velocity, float target, float epsilon) {
        if (std::fabs(value - target) < epsilon && std::fabs(velocity) < 20.0f * epsilon) {
            value = target;
            velocity = 0.0f;
            return true;
        }
        return false;
    };
    const bool gameSettled = settled(tray.game, tray.gameVelocity, game, 0.05f);
    const bool chatSettled = settled(tray.chat, tray.chatVelocity, chat, 0.05f);
    const bool morphSettled = settled(tray.morph, tray.morphVelocity, morph, 0.002f);
    // A mark that was left behind for a new one keeps the animation going until the new one is reached.
    float ignored = 0.0f;
    float wantedMorph = 0.0f;
    TrayTargets(ignored, ignored, wantedMorph);
    return !(gameSettled && chatSettled && morphSettled && wantedMorph == morph);
}

// Starts the tray animation toward the current targets, or shows them at once when the icon is new.
void MoveTray(bool jump = false) {
    TrayMotion& tray = g_app.tray;
    if (jump || g_app.settings.appearance.reduceMotion) {
        StepTray(0.0f);
        return;
    }
    if (!tray.running) {
        tray.running = true;
        QueryPerformanceCounter(&tray.last);
        SetTimer(g_app.window, kTimerTrayMotion, kTrayFrameMs, nullptr);
    }
}

// One animation frame: steps the springs by the time since the last one and shows the icon.
void TickTray() {
    TrayMotion& tray = g_app.tray;
    LARGE_INTEGER now{};
    LARGE_INTEGER frequency{};
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    const float dt = std::min(0.05f, static_cast<float>(now.QuadPart - tray.last.QuadPart) / frequency.QuadPart);
    tray.last = now;
    const bool moving = StepTray(dt);
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = g_app.window;
    data.uID = kTrayId;
    data.uFlags = NIF_ICON;
    HICON icon = TrayIconNow();
    data.hIcon = icon;
    if (Shell_NotifyIconW(NIM_MODIFY, &data)) {
        if (g_app.trayIcon) {
            DestroyIcon(g_app.trayIcon);
        }
        g_app.trayIcon = icon;
    } else if (icon) {
        DestroyIcon(icon);
    }
    if (!moving) {
        tray.running = false;
        KillTimer(g_app.window, kTimerTrayMotion);
    }
}

// The device for the tray: the battery follows the headset's name while it reports one, as in
// "Arctis Nova 7 · 85% · charging".
std::wstring TrayDeviceText() {
    std::wstring device = DeviceName();
    if (const BatteryReading battery = CurrentBattery(); battery.level >= 0) {
        device += L" · " + std::to_wstring(battery.level) + L"%";
        if (battery.charging) {
            device += std::wstring(L" · ") + T(Text::Charging);
        }
    }
    return device;
}

RECT TrayIconRect() {
    NOTIFYICONIDENTIFIER id{sizeof(id), g_app.window, kTrayId};
    RECT rect{};
    if (FAILED(Shell_NotifyIconGetRect(&id, &rect))) {
        POINT cursor{};
        GetCursorPos(&cursor);
        rect = {cursor.x, cursor.y, cursor.x + 1, cursor.y + 1};
    }
    return rect;
}

// The hover card over the tray icon; not while the panel or the menu is open.
void ShowTrayCard() {
    if (g_app.panel->IsVisible() || g_app.trayFlyout->MenuVisible()) {
        return;
    }
    ui::TrayFlyout::Card card;
    card.device = TrayDeviceText();
    card.game = g_app.levels.game;
    card.chat = g_app.levels.chat;
    card.chatMuted = g_app.chatMuted;
    card.gameMuted = g_app.gameMuted;
    g_app.trayFlyout->ShowCard(TrayIconRect(), card);
}

void UpdateTray(DWORD action) {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = g_app.window;
    data.uID = kTrayId;
    // Without NIF_SHOWTIP the shell sends NIN_POPUPOPEN and the app shows its own card. The tip text
    // goes only with the first NIM_ADD, for screen readers: later updates of it make the Windows 11
    // taskbar draw an empty tooltip next to the card.
    data.uFlags = NIF_MESSAGE | NIF_ICON | (action == NIM_ADD ? NIF_TIP : 0);
    data.uCallbackMessage = WM_APP_TRAY;
    // A new tray icon shows the current state at once; later changes animate.
    MoveTray(action == NIM_ADD);
    HICON icon = TrayIconNow();
    data.hIcon = icon;
    const std::wstring device = TrayDeviceText();
    const std::wstring tip = std::wstring(kAppName) + L"\n" + device + L"\n" + LevelsText();
    wcsncpy_s(data.szTip, tip.c_str(), _TRUNCATE);

    bool shown = Shell_NotifyIconW(action, &data) != FALSE;
    // At logon the shell may not be ready yet; TaskbarCreated triggers another NIM_ADD later.
    if (!shown && action == NIM_MODIFY) {
        action = NIM_ADD;
        shown = Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
    }
    if (shown && action == NIM_ADD) {
        data.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &data);
    }
    if (g_app.trayIcon) {
        DestroyIcon(g_app.trayIcon);
    }
    g_app.trayIcon = icon;
    if (g_app.trayFlyout && g_app.trayFlyout->CardVisible()) {
        ShowTrayCard();
    }
}

void RemoveTray() {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = g_app.window;
    data.uID = kTrayId;
    Shell_NotifyIconW(NIM_DELETE, &data);
    if (g_app.trayIcon) {
        DestroyIcon(g_app.trayIcon);
        g_app.trayIcon = nullptr;
    }
}

// How the headset reaches the computer, shown beside its name: by model for a SteelSeries headset, as
// Windows tells it for other devices. Empty while that is unknown or a wireless headset is off.
std::wstring ConnectionText() {
    if (g_app.device.IsOpen()) {
        if (g_app.device.Model()->family == DeviceFamily::NovaProWired) {
            return L"USB";
        }
        if (g_app.headset.headsetOff) {
            return {};
        }
        // The Nova Pro Wireless also plays from a Bluetooth device at the same time.
        std::wstring text = T(Text::Link24Ghz);
        if (g_app.headset.bluetooth == BluetoothState::Connected) {
            text += L" + BT";
        }
        return text;
    }
    if (g_app.outputName.empty()) {
        return {};
    }
    switch (g_app.outputLink) {
    case EndpointLink::Usb:
        return L"USB";
    case EndpointLink::Bluetooth:
        return L"Bluetooth";
    case EndpointLink::Jack:
        return T(Text::LinkJack);
    case EndpointLink::Hdmi:
        return L"HDMI";
    case EndpointLink::Spdif:
        return L"S/PDIF";
    case EndpointLink::Unknown:
        break;
    }
    return {};
}

void UpdateStatus() {
    std::wstring detail;
    if (g_app.device.IsOpen()) {
        switch (g_app.device.CurrentDialMode()) {
        case DialMode::ChatMix:
            detail = T(Text::DialChatMix);
            break;
        case DialMode::Volume:
            detail = T(Text::DialVolume);
            break;
        case DialMode::Unknown:
            break;
        }
    }
    // Other headsets and speakers have no dial: their name shows with the hint that hotkeys drive the balance.
    const bool other = !g_app.device.IsOpen() && !g_app.outputName.empty();
    if (other) {
        detail = T(Text::HotkeysOnly);
    }
    const std::wstring device = g_app.device.IsOpen() || other ? DeviceName() : T(Text::NoHeadsetStatus);
    const bool warn = g_app.ggRunning && g_app.device.IsOpen() && g_app.settings.warnGg && !g_app.ggDismissed;
    g_app.panel->SetStatus(device, detail, ConnectionText(), g_app.device.IsOpen() || other,
                           warn ? T(Text::GgRunning) : L"");
    UpdateTray(NIM_MODIFY);
}

// Tray notification, once per GG session, only when GG can actually interfere with a headset.
void NotifyGg() {
    if (!g_app.ggRunning || !g_app.device.IsOpen() || !g_app.settings.warnGg || g_app.ggNotified) {
        return;
    }
    g_app.ggNotified = true;
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = g_app.window;
    data.uID = kTrayId;
    data.uFlags = NIF_INFO;
    data.dwInfoFlags = NIIF_WARNING | NIIF_RESPECT_QUIET_TIME;
    wcsncpy_s(data.szInfoTitle, T(Text::GgBalloonTitle), _TRUNCATE);
    wcsncpy_s(data.szInfo, T(Text::GgBalloonText), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

void PushHeadset() {
    if (!g_app.device.IsOpen()) {
        // Without a SteelSeries headset only a Bluetooth output's battery shows.
        HeadsetState output;
        output.battery = g_app.outputBattery;
        g_app.panel->SetHeadset(L"", nullptr, output);
        return;
    }
    const bool visible = g_app.features && g_app.features->HasSettings();
    g_app.panel->SetHeadset(g_app.device.Model()->name, visible ? g_app.features : nullptr, g_app.headset);
}

void ScheduleHeadsetSave() {
    SetTimer(g_app.window, kTimerSaveHeadset, kSaveHeadsetDelayMs, nullptr);
}

void SaveHeadset() {
    if (g_app.device.IsOpen()) {
        SaveHeadsetState(g_app.iniPath, g_app.device.Model()->productId, g_app.headset);
    }
}

void CheckIdleScreen();

// Something changed that the headset display shows; the screensaver makes way for it.
void NoteActivity() {
    g_app.lastActivityTick = GetTickCount64();
    if (g_app.display.IsPlaying()) {
        CheckIdleScreen();
    }
}

void ScheduleHeadsetVerify(UINT delayMs) {
    if (g_app.features && g_app.features->stateQuery) {
        SetTimer(g_app.window, kTimerVerifyHeadset, delayMs, nullptr);
    }
}

// Sends a native setting and asks the headset to keep it.
void SendHeadsetCommand(std::span<const uint8_t> command) {
    // The screensaver gives the display back first, so the change shows on it.
    NoteActivity();
    g_app.device.SendCommand(command);
    if (g_app.features && !g_app.features->save.empty()) {
        SetTimer(g_app.window, kTimerCommitHeadset, kCommitHeadsetDelayMs, nullptr);
    }
    ScheduleHeadsetVerify(kVerifyAfterCommandMs);
}

// Sends the balance at most every kLevelsSendGapMs; the latest value always goes out.
void SendDeviceLevels() {
    const ULONGLONG now = GetTickCount64();
    const ULONGLONG since = now - g_app.levelsSentTick;
    if (since < kLevelsSendGapMs) {
        SetTimer(g_app.window, kTimerSendLevels, static_cast<UINT>(kLevelsSendGapMs - since), nullptr);
        return;
    }
    KillTimer(g_app.window, kTimerSendLevels);
    g_app.levelsSentTick = now;
    g_app.device.SetLevels(g_app.levels);
    g_app.levelsUnverified = g_app.features && g_app.features->stateQuery;
    ScheduleHeadsetVerify(kVerifyAfterBalanceMs);
}

// A state reply shows the balance the GameDAC applied; a missed command is sent once more.
void VerifyDeviceLevels(int game, int chat) {
    if (!g_app.levelsUnverified) {
        return;
    }
    g_app.levelsUnverified = false;
    auto step = [](int value) { return std::lround((100 - value) * kBalanceSteps / 100.0); };
    if (step(game) == step(g_app.levels.game) && step(chat) == step(g_app.levels.chat)) {
        return;
    }
    Log("balance %d/%d not applied by the device (%d/%d), sent again", g_app.levels.game, g_app.levels.chat, game, chat);
    g_app.levelsSentTick = GetTickCount64();
    g_app.device.SetLevels(g_app.levels);
}

void CommitHeadset() {
    if (!g_app.features) {
        return;
    }
    for (const auto& command : g_app.features->save) {
        g_app.device.SendCommand(std::span<const uint8_t>(command.bytes, command.length));
    }
}

void SendEqualizerCurve(const EqCurve& curve) {
    for (const std::vector<uint8_t>& command : EqualizerCommands(g_app.features->equalizer, curve)) {
        SendHeadsetCommand(command);
    }
}

// Endpoint ID of the routing output: the chosen device or the connected SteelSeries headset.
std::wstring RoutingOutputId(const std::vector<AudioEndpoint>& endpoints) {
    if (!g_app.settings.routing.output.empty()) {
        return g_app.settings.routing.output;
    }
    if (g_app.device.IsOpen()) {
        for (const auto& endpoint : endpoints) {
            if (endpoint.steelSeriesProductId == g_app.device.Model()->productId) {
                return endpoint.id;
            }
        }
    }
    return {};
}

void CheckBattery();

// A Bluetooth output's battery while no SteelSeries headset is connected, as Windows reports it; read again every
// minute while the output is such a device.
void RefreshOutputBattery() {
    const bool bluetooth = !g_app.device.IsOpen() && g_app.outputLink == EndpointLink::Bluetooth;
    const int battery = bluetooth ? BluetoothBatteryOf(g_app.outputId).value_or(-1) : -1;
    if (bluetooth) {
        SetTimer(g_app.window, kTimerOutputBattery, kOutputBatteryPollMs, nullptr);
    } else {
        KillTimer(g_app.window, kTimerOutputBattery);
    }
    if (battery == g_app.outputBattery) {
        return;
    }
    g_app.outputBattery = battery;
    if (battery >= 0) {
        Log("output battery: %d%%", battery);
    }
    PushHeadset();
    UpdateStatus();
    CheckBattery();
}

// Name of the routing output in virtual device mode, otherwise of the default playback device.
void RefreshOutputName() {
    const std::vector<AudioEndpoint> endpoints = ListRenderEndpoints();
    const std::wstring id = g_app.settings.routing.enabled ? RoutingOutputId(endpoints) : DefaultRenderEndpoint(eConsole);
    g_app.outputId.clear();
    g_app.outputName.clear();
    g_app.outputLink = EndpointLink::Unknown;
    for (const auto& endpoint : endpoints) {
        if (endpoint.id == id && !IsVirtualEndpoint(endpoint)) {
            g_app.outputId = endpoint.id;
            g_app.outputName = endpoint.name;
            g_app.outputLink = EndpointLinkOf(endpoint.id);
        }
    }
    RefreshOutputBattery();
}

bool IsRoutingDevice(const std::wstring& id) {
    const RoutingSettings& routing = g_app.settings.routing;
    return !id.empty() && (id == routing.game || id == routing.chat || id == routing.media);
}

// Whether a playback device is a virtual cable. Only those are ever switched off or used as a channel's
// device, so a headset or speakers can never be taken away from Windows.
bool IsVirtualDeviceId(const std::wstring& id) {
    if (id.empty()) {
        return false;
    }
    for (const auto& endpoint : ListRenderEndpoints(true)) {
        if (endpoint.id == id) {
            return IsVirtualEndpoint(endpoint) && !IsSonarEndpoint(endpoint);
        }
    }
    return false;
}

// Media gets its own bridge while the Media group is on and has a device.
bool MediaRouted() {
    const Settings& settings = g_app.settings;
    return settings.routing.enabled && settings.mediaGroup && !settings.routing.media.empty();
}

// The devices virtual device mode would use now; empty channel outputs become the shared output.
RoutingPlan CurrentRoutingPlan() {
    const RoutingSettings& routing = g_app.settings.routing;
    const std::wstring output = RoutingOutputId(ListRenderEndpoints());
    auto resolve = [&](const std::wstring& id) { return id.empty() ? output : id; };
    RoutingPlan plan{routing.game, routing.chat, MediaRouted() ? routing.media : L"",
                     resolve(routing.gameOutput), resolve(routing.chatOutput), resolve(routing.mediaOutput)};
    return plan;
}

// Why a plan cannot run, or nothing: every device chosen, the sources apart from each other, and no
// channel playing into a source.
std::optional<Text> RoutingProblem(const RoutingPlan& plan) {
    if (plan.game.empty() || plan.chat.empty() || plan.gameOutput.empty() || plan.chatOutput.empty() ||
        (!plan.media.empty() && plan.mediaOutput.empty())) {
        return Text::RoutingNeedsDevices;
    }
    std::vector<std::wstring> sources{plan.game, plan.chat};
    if (!plan.media.empty()) {
        sources.push_back(plan.media);
    }
    for (size_t i = 0; i < sources.size(); ++i) {
        for (size_t j = i + 1; j < sources.size(); ++j) {
            if (sources[i] == sources[j]) {
                return Text::RoutingSameDevice;
            }
        }
    }
    for (const std::wstring* output : {&plan.gameOutput, &plan.chatOutput, &plan.mediaOutput}) {
        if (std::find(sources.begin(), sources.end(), *output) != sources.end()) {
            return Text::RoutingSameDevice;
        }
    }
    return std::nullopt;
}

// With device management the Game and Chat devices are enabled and become the default output and
// communications device while the mode runs. Afterwards they are disabled again, so apps fall back
// to the headset instead of playing into devices nobody listens to.
void AcquireRoutingDevices() {
    RoutingSettings& routing = g_app.settings.routing;
    if (g_app.routingDevicesAcquired || !routing.enabled || !routing.manageDevices || routing.game.empty() ||
        routing.chat.empty() || routing.game == routing.chat) {
        return;
    }
    // Saved before the devices are enabled: Windows may make a device that comes back the default at
    // once. A device saved by an earlier run is kept while the default still points at one of ours.
    if (const std::wstring current = DefaultRenderEndpoint(eConsole); !IsRoutingDevice(current)) {
        routing.savedDefault = current;
    }
    if (const std::wstring current = DefaultRenderEndpoint(eCommunications); !IsRoutingDevice(current)) {
        routing.savedCommunications = current;
    }
    bool ok = SetEndpointEnabled(routing.game, true);
    ok = SetEndpointEnabled(routing.chat, true) && ok;
    if (MediaRouted() && routing.media != routing.game && routing.media != routing.chat) {
        ok = SetEndpointEnabled(routing.media, true) && ok;
    }
    ok = SetDefaultRenderEndpoint(routing.game, eConsole) && ok;
    ok = SetDefaultRenderEndpoint(routing.game, eMultimedia) && ok;
    ok = SetDefaultRenderEndpoint(routing.chat, eCommunications) && ok;
    g_app.routingDevicesAcquired = true;
    g_app.routingDeviceError = !ok;
    Log("routing devices switched on%s", ok ? "" : " with errors");
    Save();
}

// Restores the previous default devices; disable also switches the Game and Chat devices off.
void ReleaseRoutingDevices(bool disable) {
    if (!g_app.routingDevicesAcquired) {
        return;
    }
    g_app.routingDevicesAcquired = false;
    const RoutingSettings& routing = g_app.settings.routing;
    const std::vector<AudioEndpoint> endpoints = ListRenderEndpoints();
    const std::wstring fallback = RoutingOutputId(endpoints);
    auto usable = [&](const std::wstring& id) {
        return !IsRoutingDevice(id) && std::any_of(endpoints.begin(), endpoints.end(),
                                                   [&](const AudioEndpoint& endpoint) { return endpoint.id == id; });
    };
    bool ok = true;
    auto restore = [&](const std::wstring& saved, ERole role) {
        // A default the user picked while the mode ran stays.
        if (!IsRoutingDevice(DefaultRenderEndpoint(role))) {
            return;
        }
        const std::wstring target = usable(saved) ? saved : fallback;
        if (!target.empty()) {
            ok = SetDefaultRenderEndpoint(target, role) && ok;
        }
    };
    restore(routing.savedDefault, eConsole);
    restore(routing.savedDefault, eMultimedia);
    restore(routing.savedCommunications, eCommunications);
    if (disable) {
        for (const std::wstring* id : {&routing.game, &routing.chat, &routing.media}) {
            if (id->empty() || (id == &routing.media && (routing.media == routing.game || routing.media == routing.chat))) {
                continue;
            }
            if (IsVirtualDeviceId(*id)) {
                ok = SetEndpointEnabled(*id, false) && ok;
            } else {
                Log("routing: a channel device is not a virtual cable and stays on");
            }
        }
    }
    g_app.routingDeviceError = !ok;
    Log("routing devices %s%s", disable ? "switched off" : "released", ok ? "" : " with errors");
}

// Earlier builds could pick the SteelSeries Sonar devices (see IsSonarEndpoint). They are handed back:
// the headset becomes the default output again and the devices return to the disabled state GG keeps
// them in while Sonar is off.
void DropSonarDevices() {
    RoutingSettings& routing = g_app.settings.routing;
    const std::vector<AudioEndpoint> endpoints = ListRenderEndpoints(true);
    bool changed = false;
    for (std::wstring* slot : {&routing.game, &routing.chat}) {
        const auto endpoint = std::find_if(endpoints.begin(), endpoints.end(),
                                           [&](const AudioEndpoint& candidate) { return candidate.id == *slot; });
        if (slot->empty() || endpoint == endpoints.end() || !IsSonarEndpoint(*endpoint)) {
            continue;
        }
        const std::wstring fallback = RoutingOutputId(ListRenderEndpoints());
        for (const ERole role : {eConsole, eMultimedia, eCommunications}) {
            if (DefaultRenderEndpoint(role) == *slot && !fallback.empty()) {
                SetDefaultRenderEndpoint(fallback, role);
            }
        }
        SetEndpointEnabled(*slot, false);
        Log("dropped Sonar device: %s", Utf8(endpoint->name).c_str());
        slot->clear();
        changed = true;
    }
    if (changed) {
        g_app.routingDevicesAcquired = false;
        routing.savedDefault.clear();
        routing.savedCommunications.clear();
        Save();
    }
}

// Chooses the Game and Chat devices on its own when none are selected, see FindVirtualDevices().
void AssignVirtualDevices() {
    RoutingSettings& routing = g_app.settings.routing;
    if (!routing.game.empty() || !routing.chat.empty()) {
        return;
    }
    const std::optional<VirtualDevicePair> pair = FindVirtualDevices(ListRenderEndpoints(true));
    if (!pair) {
        return;
    }
    routing.game = pair->game.id;
    routing.chat = pair->chat.id;
    Log("virtual devices assigned: %s / %s", Utf8(pair->game.name).c_str(), Utf8(pair->chat.name).c_str());
    Save();
}

void UpdateRouting() {
    const std::vector<AudioEndpoint> endpoints = ListRenderEndpoints(true);
    auto nameOf = [&](const std::wstring& id) -> std::wstring {
        for (const auto& endpoint : endpoints) {
            if (endpoint.id == id) {
                return endpoint.name;
            }
        }
        return {};
    };
    const RoutingSettings& routing = g_app.settings.routing;
    ui::RoutingView view;
    view.enabled = routing.enabled;
    auto set = [&](ui::RoutingSlot slot, const std::wstring& id, const std::wstring& name) {
        view.ids[static_cast<int>(slot)] = id;
        view.names[static_cast<int>(slot)] = name;
    };
    const std::wstring outputId = RoutingOutputId(ListRenderEndpoints());
    set(ui::RoutingSlot::Game, routing.game, nameOf(routing.game));
    set(ui::RoutingSlot::Chat, routing.chat, nameOf(routing.chat));
    set(ui::RoutingSlot::Media, routing.media, routing.media.empty() ? L"" : nameOf(routing.media));
    set(ui::RoutingSlot::Output, routing.output,
        routing.output.empty()
            ? std::wstring(T(Text::AutomaticHeadset)) + (outputId.empty() ? L"" : L" · " + nameOf(outputId))
            : nameOf(routing.output));
    auto channelOutput = [&](ui::RoutingSlot slot, const std::wstring& id) {
        set(slot, id, id.empty() ? std::wstring(T(Text::SameAsOutput)) : nameOf(id));
    };
    channelOutput(ui::RoutingSlot::GameOutput, routing.gameOutput);
    channelOutput(ui::RoutingSlot::ChatOutput, routing.chatOutput);
    channelOutput(ui::RoutingSlot::MediaOutput, routing.mediaOutput);
    view.mediaRouted = MediaRouted();
    view.manageDevices = routing.manageDevices;
    view.cablesMissing = (routing.game.empty() || routing.chat.empty()) && !FindVirtualDevices(endpoints);
    if (routing.enabled) {
        if (g_app.router.IsRunning()) {
            view.status = T(Text::RoutingRunning);
            if (const int latency = g_app.router.LatencyMs(); latency > 0) {
                view.status += std::wstring(L" · ") + T(Text::AddsLatency) + L" ~" + std::to_wstring(latency) + L" " +
                               T(Text::MillisecondsShort);
            }
        } else {
            view.error = true;
            const std::optional<Text> problem = RoutingProblem(CurrentRoutingPlan());
            view.status = T(problem.value_or(Text::RoutingDeviceMissing));
        }
        if (g_app.routingDeviceError) {
            view.status += std::wstring(L" · ") + T(Text::RoutingManageFailed);
            view.error = true;
        }
    }
    g_app.panel->SetRouting(view);
}

Levels EffectiveLevels();
std::vector<AudioEndpoint> PreferredOutputs();
std::wstring CurrentOutput();
void MaybeCheckForUpdate();
void ShowTrayNotice(const std::wstring& title, const std::wstring& text, DWORD icon);
void PushEqualizers();
void UpdateMixerEndpoints();
void RouteApps();

void StartRouting() {
    g_app.router.Stop();
    const RoutingSettings& routing = g_app.settings.routing;
    if (routing.enabled) {
        DropSonarDevices();
        AssignVirtualDevices();
        AcquireRoutingDevices();
        // The Media device may come into use after the others were taken over, with the Media group.
        if (MediaRouted() && routing.manageDevices) {
            SetEndpointEnabled(routing.media, true);
        }
        UpdateMixerEndpoints();
        const RoutingPlan plan = CurrentRoutingPlan();
        if (!RoutingProblem(plan) && g_app.router.Start(plan, g_app.window)) {
            // App volumes may carry the balance from the time the streams were not running.
            g_app.mixer.ResetVolumes();
            g_app.router.SetLevels(EffectiveLevels());
            PushEqualizers();
            Log("routing started");
        } else {
            Log("routing not started");
        }
        RouteApps();
    }
    UpdateRouting();
    RefreshOutputName();
    UpdateStatus();
}

void PushSettings() {
    g_app.panel->SetAutostart(IsAutostartEnabled());
    g_app.panel->SetSettings(g_app.settings);
}

void ApplyAppearance() {
    const AppearanceSettings& appearance = g_app.settings.appearance;
    ui::SetTheme(appearance.theme, AccentSeed(), appearance.chatAccent, appearance.contrast);
    if (g_app.osd) {
        g_app.osd->SetReduceMotion(appearance.reduceMotion);
        g_app.trayFlyout->SetReduceMotion(appearance.reduceMotion);
    }
    if (g_app.panel) {
        g_app.panel->Refresh();
    }
    if (g_app.trayIcon) {
        UpdateTray(NIM_MODIFY);
    }
}

void PushEqualizers() {
    auto equalizer = [](const EqChannelSettings& channel) {
        return g_app.settings.equalizer && channel.enabled ? std::make_shared<const ParametricEq>(channel.eq) : nullptr;
    };
    g_app.router.SetEqualizers(equalizer(g_app.settings.gameEq), equalizer(g_app.settings.chatEq),
                               equalizer(g_app.settings.mediaEq));
}

// ---- Microphone chain -------------------------------------------------------------------------------

std::wstring PluginStatePath(const MicPlugin& plugin) {
    const std::wstring directory = ConfigDirectory() + L"\\PluginStates";
    CreateDirectoryW(directory.c_str(), nullptr);
    return directory + L"\\" + plugin.id + L".bin";
}

std::vector<uint8_t> ReadFileBytes(const std::wstring& path) {
    std::vector<uint8_t> data;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return data;
    }
    LARGE_INTEGER size{};
    if (GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart < 256 * 1024 * 1024) {
        data.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        if (!ReadFile(file, data.data(), static_cast<DWORD>(data.size()), &read, nullptr) || read != data.size()) {
            data.clear();
        }
    }
    CloseHandle(file);
    return data;
}

void WriteFileBytes(const std::wstring& path, const std::vector<uint8_t>& data) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD written = 0;
    WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr);
    CloseHandle(file);
}

std::wstring NewPluginId() {
    GUID guid{};
    wchar_t text[64] = {};
    if (FAILED(CoCreateGuid(&guid)) || !StringFromGUID2(guid, text, ARRAYSIZE(text))) {
        return std::to_wstring(GetTickCount64());
    }
    std::wstring id = text;
    return id.substr(1, id.size() - 2);  // Without the braces.
}

MicDspSettings MicDsp() {
    MicDspSettings dsp;
    dsp.noise = g_app.settings.mic.noise;
    dsp.compressor = g_app.settings.mic.compressor;
    dsp.order = g_app.settings.mic.order;
    if (g_app.settings.equalizer && g_app.settings.micEq.enabled) {
        dsp.eq = std::make_shared<const ParametricEq>(g_app.settings.micEq.eq);
    }
    return dsp;
}

// The playback side of the cable the microphone plays into.
std::wstring MicOutputId(const std::vector<AudioEndpoint>& endpoints) {
    if (!g_app.settings.mic.output.empty()) {
        return g_app.settings.mic.output;
    }
    const std::optional<AudioEndpoint> cable = FindMicCable(endpoints);
    return cable ? cable->id : L"";
}

void PushMic() {
    const MicSettings& settings = g_app.settings.mic;
    ui::MicView view;
    const std::vector<AudioEndpoint> renders = ListRenderEndpoints();
    const std::wstring outputId = MicOutputId(renders);
    for (const auto& endpoint : renders) {
        if (endpoint.id == outputId) {
            view.outputName = endpoint.name;
        }
    }
    if (!settings.input.empty()) {
        for (const auto& endpoint : ListCaptureEndpoints()) {
            if (endpoint.id == settings.input) {
                view.inputName = endpoint.name;
            }
        }
        if (view.inputName.empty()) {
            view.inputName = T(Text::NotSelected);
        }
    }
    if (settings.enabled) {
        if (g_app.mic.IsRunning()) {
            view.status = T(Text::RoutingRunning);
            if (const int latency = g_app.mic.LatencyMs(); latency > 0) {
                view.status += std::wstring(L" · ") + T(Text::AddsLatency) + L" ~" + std::to_wstring(latency) + L" " +
                               T(Text::MillisecondsShort);
            }
        } else {
            view.error = true;
            view.status = outputId.empty() ? T(Text::MicNoCable) : T(Text::RoutingDeviceMissing);
        }
    }
    for (size_t index = 0; index < settings.plugins.size(); ++index) {
        ui::MicPluginView plugin;
        plugin.path = settings.plugins[index].path;
        plugin.enabled = settings.plugins[index].enabled;
        if (index < g_app.micPlugins.size() && g_app.micPlugins[index]) {
            plugin.name = g_app.micPlugins[index]->Name();
            plugin.loaded = true;
            plugin.hasEditor = g_app.micPlugins[index]->HasEditor();
            plugin.deepFilter = g_app.micPlugins[index]->DeepFilter();
        } else {
            // Not loaded yet while the chain is off; only a failed load counts as an error.
            plugin.loaded = !g_app.micPluginsLoaded;
        }
        view.plugins.push_back(std::move(plugin));
    }
    g_app.panel->SetMic(view);
}

// The enabled plugins that loaded, in order, to the audio thread.
void PushMicPlugins() {
    MicPluginList list;
    for (size_t index = 0; index < g_app.micPlugins.size() && index < g_app.settings.mic.plugins.size(); ++index) {
        if (g_app.micPlugins[index] && g_app.settings.mic.plugins[index].enabled) {
            list.push_back(g_app.micPlugins[index].get());
        }
    }
    g_app.mic.SetPlugins(std::move(list));
}

std::unique_ptr<AudioPlugin> LoadMicPlugin(const MicPlugin& plugin) {
    std::unique_ptr<AudioPlugin> loaded;
    g_app.pluginThread.Run([&] {
        loaded = LoadAudioPlugin(plugin.path, MicProcessor::kRate, static_cast<int>(MicProcessor::kBlock));
        if (loaded) {
            loaded->LoadState(ReadFileBytes(PluginStatePath(plugin)));
        }
    });
    return loaded;
}

void LoadMicPlugins() {
    if (g_app.micPluginsLoaded || !g_app.pluginThread.Start()) {
        return;
    }
    g_app.micPluginsLoaded = true;
    for (const MicPlugin& plugin : g_app.settings.mic.plugins) {
        g_app.micPlugins.push_back(LoadMicPlugin(plugin));
    }
    SetTimer(g_app.window, kTimerSavePlugins, kSavePluginsMs, nullptr);
}

void SaveMicPluginStates() {
    for (size_t index = 0; index < g_app.micPlugins.size() && index < g_app.settings.mic.plugins.size(); ++index) {
        if (AudioPlugin* plugin = g_app.micPlugins[index].get()) {
            const std::wstring path = PluginStatePath(g_app.settings.mic.plugins[index]);
            g_app.pluginThread.Run([&] { WriteFileBytes(path, plugin->SaveState()); });
        }
    }
}

// Plugins are only loaded while the chain is on.
void UnloadMicPlugins() {
    if (!g_app.micPluginsLoaded) {
        return;
    }
    SaveMicPluginStates();
    g_app.mic.SetPlugins({});
    g_app.pluginThread.Run([] { g_app.micPlugins.clear(); });
    g_app.micPluginsLoaded = false;
    KillTimer(g_app.window, kTimerSavePlugins);
}

void StartMic() {
    g_app.mic.Stop();
    g_app.micLevels.Stop();
    KillTimer(g_app.window, kTimerMicRetry);
    const MicSettings& settings = g_app.settings.mic;
    if (!settings.enabled) {
        UnloadMicPlugins();
        PushMic();
        return;
    }
    LoadMicPlugins();
    g_app.micInputId = settings.input.empty() ? DefaultCaptureEndpoint(eCommunications) : settings.input;
    const std::vector<AudioEndpoint> renders = ListRenderEndpoints();
    const std::wstring output = MicOutputId(renders);
    g_app.mic.SetSettings(MicDsp());
    PushMicPlugins();
    // Listening goes where ChatMix plays: the routing output, or the Windows default device.
    const std::wstring monitor = settings.monitor ? CurrentOutput() : L"";
    g_app.micMonitorId = monitor;
    if (!output.empty() && g_app.mic.Start(settings.input, output, monitor, g_app.window)) {
        Log("mic chain started");
    } else {
        Log("mic chain not started");
        // A missing cable comes back through an endpoint change; a busy or unplugged device is retried.
        if (!output.empty()) {
            SetTimer(g_app.window, kTimerMicRetry, kMicRetryMs, nullptr);
        }
    }
    if (settings.keepLevels) {
        std::vector<std::wstring> ids{g_app.micInputId};
        if (!output.empty()) {
            ids.push_back(output);
            for (const auto& endpoint : renders) {
                if (endpoint.id == output) {
                    ids.push_back(PairedCaptureEndpoint(endpoint, ListCaptureEndpoints()));
                }
            }
        }
        g_app.micLevels.Start(ids, g_app.window, WM_APP_MIC_LEVEL);
    }
    PushMic();
}

// Apps on the Game and Chat devices are listed (and routed) in virtual device mode.
void UpdateMixerEndpoints() {
    const RoutingSettings& routing = g_app.settings.routing;
    std::vector<std::wstring> extra;
    if (routing.enabled) {
        for (const std::wstring* id : {&routing.game, &routing.chat, &routing.media}) {
            if (!id->empty()) {
                extra.push_back(*id);
            }
        }
    }
    g_app.mixer.SetExtraEndpoints(std::move(extra));
}

void RememberRouted(const std::wstring& exe, bool routed) {
    std::vector<std::wstring>& list = g_app.settings.routing.routedApps;
    const auto found = std::find_if(list.begin(), list.end(),
                                    [&](const std::wstring& item) { return _wcsicmp(item.c_str(), exe.c_str()) == 0; });
    if (routed && found == list.end()) {
        list.push_back(exe);
    } else if (!routed && found != list.end()) {
        list.erase(found);
    }
}

// Chat apps play on the Chat device and Media apps on the Media device when it has one; the others follow
// the default device, which is the Game device while devices are managed, or get the Game device
// explicitly otherwise.
void RouteApps() {
    const RoutingSettings& routing = g_app.settings.routing;
    if (!routing.enabled || !routing.autoRoute || !g_app.appRouting.Available() || routing.game.empty() ||
        routing.chat.empty()) {
        return;
    }
    bool changed = false;
    for (const auto& app : g_app.mixer.ActiveApps()) {
        if (app.pid == 0) {
            continue;
        }
        const AppGroup group = g_app.settings.EffectiveGroupOf(app.exe);
        const bool chat = group == AppGroup::Chat;
        const bool media = group == AppGroup::Media && MediaRouted();
        const std::wstring target = chat    ? routing.chat
                                    : media ? routing.media
                                            : (routing.manageDevices ? L"" : routing.game);
        const auto now = g_app.routedNow.find(app.exe);
        const bool listed = std::any_of(routing.routedApps.begin(), routing.routedApps.end(), [&](const std::wstring& exe) {
            return _wcsicmp(exe.c_str(), app.exe.c_str()) == 0;
        });
        if (now != g_app.routedNow.end() ? now->second == target : (target.empty() && !listed)) {
            continue;
        }
        if (g_app.appRouting.Route(app.pid, target)) {
            g_app.routedNow[app.exe] = target;
            RememberRouted(app.exe, !target.empty());
            changed = true;
            Log("routed %s to %s", Utf8(app.exe).c_str(),
                target.empty() ? "the default device" : chat ? "Chat" : media ? "Media" : "Game");
        }
    }
    if (changed) {
        ScheduleSave();
    }
}

// Returns running apps that OpenChatMix routed to the default device again.
void UnrouteApps() {
    if (!g_app.appRouting.Available() || g_app.settings.routing.routedApps.empty()) {
        g_app.routedNow.clear();
        return;
    }
    bool changed = false;
    for (const auto& app : g_app.mixer.ActiveApps()) {
        const std::vector<std::wstring>& list = g_app.settings.routing.routedApps;
        const bool listed = std::any_of(list.begin(), list.end(), [&](const std::wstring& exe) {
            return _wcsicmp(exe.c_str(), app.exe.c_str()) == 0;
        });
        if (listed && app.pid && g_app.appRouting.Route(app.pid, L"")) {
            RememberRouted(app.exe, false);
            changed = true;
            Log("routing of %s cleared", Utf8(app.exe).c_str());
        }
    }
    g_app.routedNow.clear();
    if (changed) {
        ScheduleSave();
    }
}

void SyncAppRouting() {
    const RoutingSettings& routing = g_app.settings.routing;
    if (routing.enabled && routing.autoRoute) {
        RouteApps();
    } else if (!routing.routedApps.empty()) {
        UnrouteApps();
    }
}

void RefreshApps() {
    std::vector<ui::AppItem> items;
    bool pathsChanged = false;
    for (const auto& app : g_app.mixer.ActiveApps()) {
        items.push_back({app.exe, app.path, DisplayName(app.path, app.exe), g_app.settings.GroupOf(app.exe), true});
        // Remember where configured apps live so their icons show while they are closed.
        const auto rule = g_app.settings.apps.find(app.exe);
        if (rule != g_app.settings.apps.end() && !app.path.empty() && rule->second.path != app.path) {
            rule->second.path = app.path;
            pathsChanged = true;
        }
    }
    for (const auto& [exe, rule] : g_app.settings.apps) {
        const bool listed = std::any_of(items.begin(), items.end(), [&](const ui::AppItem& item) {
            return _wcsicmp(item.exe.c_str(), exe.c_str()) == 0;
        });
        if (listed) {
            continue;
        }
        // A default chat app is listed only while it is installed; its rule still applies when it first runs.
        std::wstring path = rule.path;
        if (IsDefaultChatApp(exe) && (path.empty() || GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)) {
            path = InstalledAppPath(exe);
            if (path.empty()) {
                continue;
            }
        }
        items.push_back({exe, path, DisplayName(path, exe), rule.group, false});
    }
    if (pathsChanged) {
        Save();
    }

    // Chat first, then Game, then Media; running apps before closed ones.
    auto order = [](AppGroup group) { return group == AppGroup::Chat ? 0 : group == AppGroup::Game ? 1 : 2; };
    std::sort(items.begin(), items.end(), [&](const ui::AppItem& a, const ui::AppItem& b) {
        if (order(a.group) != order(b.group)) {
            return order(a.group) < order(b.group);
        }
        if (a.running != b.running) {
            return a.running;
        }
        return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    g_app.panel->SetApps(std::move(items));
}

// A connected headset has a ChatMix dial; the balance then keeps to its positions, otherwise any
// percent is possible.
bool HasBalanceDial() {
    return g_app.device.IsOpen();
}

int BalanceGrid() {
    return HasBalanceDial() ? kBalanceSteps : kCoarseSteps;
}

Levels DialLevels(Levels levels) {
    return HasBalanceDial() ? LevelsFromBalance(SnapBalance(BalanceFromLevels(levels), kBalanceSteps)) : levels;
}

// The balance with chat mute and ducking applied; the dial and the panel keep showing the balance itself.
Levels EffectiveLevels() {
    Levels levels = g_app.levels;
    // Each side's own volume scales its ChatMix level; Windows volumes stay at 100%.
    levels.game = static_cast<int>(std::lround(levels.game * g_app.settings.gameVolume / 100.0f));
    levels.chat = static_cast<int>(std::lround(levels.chat * g_app.settings.chatVolume / 100.0f));
    if (g_app.chatMuted) {
        levels.chat = 0;
    }
    if (g_app.gameMuted) {
        levels.game = 0;
    }
    if (g_app.duck > 0.0f) {
        const float reduction = g_app.settings.ducking.amount / 100.0f * g_app.duck;
        levels.game = static_cast<int>(std::lround(levels.game * (1.0f - reduction)));
    }
    return levels;
}

void ApplyLevels() {
    const Levels levels = EffectiveLevels();
    // Virtual device mode mixes the streams itself; until it runs, app volumes keep ChatMix working.
    const float media = g_app.mediaMuted ? 0.0f : 1.0f;
    if (g_app.settings.routing.enabled && g_app.router.IsRunning()) {
        g_app.router.SetLevels(levels);
        g_app.router.SetMediaLevel(media);
        return;
    }
    g_app.mixer.Apply(levels, g_app.settings, media);
}

void UpdateDucking() {
    if (g_app.settings.ducking.enabled) {
        SetTimer(g_app.window, kTimerDucking, kDuckingTickMs, nullptr);
        return;
    }
    KillTimer(g_app.window, kTimerDucking);
    if (g_app.duck != 0.0f) {
        g_app.duck = 0.0f;
        ApplyLevels();
    }
}

void TickDucking() {
    const bool bridged = g_app.settings.routing.enabled && g_app.router.IsRunning();
    const float peak = bridged ? g_app.router.TakeChatPeak() : g_app.mixer.GroupPeak(g_app.settings, AppGroup::Chat);
    const ULONGLONG now = GetTickCount64();
    if (peak > kChatActivePeak && !g_app.chatMuted) {
        g_app.chatActiveTick = now;
    }
    const float target = g_app.chatActiveTick && now - g_app.chatActiveTick < kDuckingHoldMs ? 1.0f : 0.0f;
    // Quick to duck, slow to come back.
    float next = g_app.duck + (target - g_app.duck) * (target > g_app.duck ? 0.6f : 0.15f);
    if (std::fabs(target - next) < 0.01f) {
        next = target;
    }
    const int before = EffectiveLevels().game;
    g_app.duck = next;
    if (EffectiveLevels().game != before) {
        ApplyLevels();
    }
}

// fromDevice: the change came from the headset dial and must not be echoed back to it.
void SetLevels(Levels levels, bool fromDevice) {
    if (levels == g_app.levels) {
        return;
    }
    g_app.levels = levels;
    ApplyLevels();
    if (fromDevice) {
        g_app.levelsUnverified = false;
    } else {
        // The screensaver gives the display back first, so the GameDAC shows the change.
        NoteActivity();
        SendDeviceLevels();
    }
    UpdateTray(NIM_MODIFY);
    g_app.panel->SetLevels(levels);
}

void ApplyHotkey(Levels levels) {
    SetLevels(levels, false);
    if (g_app.settings.showPopup) {
        g_app.osd->Show(g_app.levels);
    }
}

// Modifier keys held right now; GetKeyState lags behind in a low-level hook.
UINT AsyncModifiers() {
    UINT modifiers = 0;
    auto held = [](int key) { return (GetAsyncKeyState(key) & 0x8000) != 0; };
    if (held(VK_CONTROL)) {
        modifiers |= MOD_CONTROL;
    }
    if (held(VK_MENU)) {
        modifiers |= MOD_ALT;
    }
    if (held(VK_SHIFT)) {
        modifiers |= MOD_SHIFT;
    }
    if (held(VK_LWIN) || held(VK_RWIN)) {
        modifiers |= MOD_WIN;
    }
    return modifiers;
}

// Takes the wheel and the wheel click while exactly the chosen keys are held; everything else passes on.
// The work happens in WM_APP_WHEEL, since Windows drops hooks that do not return quickly.
LRESULT CALLBACK MouseHookProc(int code, WPARAM wParam, LPARAM lParam) {
    static bool clickTaken = false;  // The button-up of a taken click is taken too.
    if (code == HC_ACTION && !g_app.hotkeysSuspended) {
        const auto* info = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
        const bool chosen = g_app.settings.wheelModifiers != 0 && AsyncModifiers() == g_app.settings.wheelModifiers;
        switch (wParam) {
        case WM_MOUSEWHEEL:
            if (chosen) {
                PostMessageW(g_app.window, WM_APP_WHEEL, 0, static_cast<SHORT>(HIWORD(info->mouseData)));
                return 1;
            }
            break;
        case WM_MBUTTONDOWN:
            if (chosen) {
                clickTaken = true;
                PostMessageW(g_app.window, WM_APP_WHEEL, 0, 0);
                return 1;
            }
            break;
        case WM_MBUTTONUP:
            if (clickTaken) {
                clickTaken = false;
                return 1;
            }
            break;
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

void UpdateMouseHook() {
    const bool wanted = g_app.settings.wheelModifiers != 0;
    if (wanted && !g_app.mouseHook) {
        g_app.mouseHook = SetWindowsHookExW(WH_MOUSE_LL, MouseHookProc, g_app.instance, 0);
        if (!g_app.mouseHook) {
            Log("mouse hook failed, error %lu", GetLastError());
        }
    } else if (!wanted && g_app.mouseHook) {
        UnhookWindowsHookEx(g_app.mouseHook);
        g_app.mouseHook = nullptr;
    }
    g_app.wheelRemainder = 0;
}

// A wheel notch moves the balance by one position, like the hotkeys; a click centers it.
void OnChatMixWheel(int delta) {
    // Held keys without a key press in between would open the Start menu (Win) or the menu bar (Alt)
    // once they are released; an unassigned key in between prevents that.
    INPUT mask[2]{};
    mask[0].type = mask[1].type = INPUT_KEYBOARD;
    mask[0].ki.wVk = mask[1].ki.wVk = 0xE8;
    mask[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, mask, sizeof(INPUT));
    if (delta == 0) {
        ApplyHotkey(Levels{});
        return;
    }
    g_app.wheelRemainder += delta;
    const int notches = g_app.wheelRemainder / WHEEL_DELTA;
    g_app.wheelRemainder -= notches * WHEEL_DELTA;
    if (notches != 0) {
        // Wheel up gives more chat, as on the panel's balance card.
        ApplyHotkey(LevelsFromBalance(StepBalance(BalanceFromLevels(g_app.levels), notches, BalanceGrid())));
    }
}

void RegisterHotkeys() {
    for (int action = 0; action < kHotkeyCount; ++action) {
        UnregisterHotKey(g_app.window, kHotkeyIdBase + action);
        g_app.hotkeyFailed[action] = false;
        const Hotkey& hotkey = g_app.settings.hotkeys[action];
        if (g_app.hotkeysSuspended || hotkey.IsEmpty()) {
            continue;
        }
        if (!RegisterHotKey(g_app.window, kHotkeyIdBase + action, hotkey.modifiers, hotkey.key)) {
            g_app.hotkeyFailed[action] = true;
            Log("hotkey %s is taken, error %lu", Utf8(HotkeyText(hotkey)).c_str(), GetLastError());
        }
    }
    g_app.panel->SetHotkeys(g_app.settings.hotkeys, g_app.hotkeyFailed);
}

void PushIdleScreen() {
    const IdleScreenSettings& idle = g_app.settings.idleScreen;
    ui::IdleScreenView view;
    view.animation = idle.animation;
    view.delaySeconds = idle.delaySeconds;
    view.imageName = idle.image.empty() ? L"" : FileNameOf(idle.image);
    view.showing = g_app.idleShowing;
    view.ggApps = g_app.ggScreen.names;
    view.bluetooth = g_app.features && g_app.features->bluetooth;
    view.bluetoothIcon = idle.bluetoothIcon;
    g_app.panel->SetIdleScreen(view);
}

// The base station's Bluetooth on the screensaver, in the chosen corner, while it is on. Like the headset's LED it
// stays steady with a device connected, blinks slowly while it waits for one and fast while it pairs.
void UpdateIdleBadge() {
    const IconCorner corner = g_app.settings.idleScreen.bluetoothIcon;
    const BluetoothState bluetooth = g_app.headset.bluetooth;
    const bool on = bluetooth == BluetoothState::On || bluetooth == BluetoothState::Connected ||
                    bluetooth == BluetoothState::Pairing;
    const bool shown = corner != IconCorner::Off && on && g_app.features && g_app.features->bluetooth;
    const OledCorner place = corner == IconCorner::TopLeft      ? OledCorner::TopLeft
                             : corner == IconCorner::BottomLeft  ? OledCorner::BottomLeft
                             : corner == IconCorner::BottomRight ? OledCorner::BottomRight
                                                                 : OledCorner::TopRight;
    const OledBlink blink = bluetooth == BluetoothState::Pairing ? OledBlink::Fast
                            : bluetooth == BluetoothState::On    ? OledBlink::Slow
                                                                 : OledBlink::None;
    g_app.display.SetBluetoothIcon(shown, blink, place);
}

const char* BluetoothName(BluetoothState state) {
    switch (state) {
    case BluetoothState::Unknown: return "unknown";
    case BluetoothState::Off: return "off";
    case BluetoothState::Pairing: return "pairing";
    case BluetoothState::On: return "on";
    case BluetoothState::Connected: return "connected";
    }
    return "";
}

// GG's helpers are watched only while the screensaver is on and one of them runs, and closely only while the
// screensaver plays. Without GG or its apps for the display nothing is checked.
void UpdateGgWatch() {
    const bool needed = g_app.display.IsOpen() && g_app.ggRunning && !g_app.ggHelpers.empty();
    g_app.ggWatch.Watch(needed ? g_app.ggHelpers : std::vector<GgHelperProcess>{});
    g_app.ggWatch.SetFast(needed && g_app.display.IsPlaying());
}

// Runs on the GG watch thread the moment a helper hands GG something: the screensaver stops drawing before its
// next report, and CheckIdleScreen then decides how long the hold lasts.
void OnGgEventNow() {
    if (g_app.display.IsPlaying()) {
        g_app.display.HoldNow();
    }
}

// How long GG shows what a helper of the app handed it.
ULONGLONG GgShowMs(GgScreenApp app) {
    switch (app) {
    case GgScreenApp::Discord: return kGgMessageMs;
    case GgScreenApp::Track: return kGgTrackMs;
    case GgScreenApp::Visualizer: return kGgFrameMs;
    }
    return 0;
}

const char* GgAppName(GgScreenApp app) {
    switch (app) {
    case GgScreenApp::Discord: return "Discord";
    case GgScreenApp::Track: return "Now Playing";
    case GgScreenApp::Visualizer: return "Audio Visualizer";
    }
    return "";
}

// Holds the screensaver still until the given tick, for what SteelSeries GG may show meanwhile.
void HoldIdleScreen(ULONGLONG now, ULONGLONG until, const char* reason) {
    if (!g_app.idleHoldUntil) {
        g_app.idleHoldStart = now;
        Log("screensaver gives way to SteelSeries GG: %s", reason);
    }
    g_app.idleHoldUntil = std::max(g_app.idleHoldUntil, until);
}

// What goes on the display when no add-on screen is over it.
void ShowOnDisplay(App::DisplayShows shows) {
    if (shows == g_app.displayShows) {
        return;
    }
    g_app.displayShows = shows;
    if (shows == App::DisplayShows::Visualizer) {
        if (!g_app.audioTap->IsRunning()) {
            g_app.audioTap->Start(g_app.mixer.OutputEndpointId());
        }
        if (!g_app.visualizer) {
            g_app.visualizer = CreateVisualizer(g_app.audioTap);
        }
        g_app.display.Play(g_app.visualizer);
        Log("visualizer on");
        return;
    }
    g_app.audioTap->Stop();
    if (shows == App::DisplayShows::Screensaver) {
        g_app.display.Play(g_app.idleAnimation);
        Log("screensaver on");
    } else {
        g_app.display.Pause();
        Log("display back to the headset");
    }
}

// The visualizer shows while something plays: any sound, or media an app tells Windows about. For a moment after
// the headset or the app is used it gives way to the GameDAC's own screen, so a volume change shows there.
bool VisualizerWanted(ULONGLONG now, ULONGLONG deviceEvent) {
    const VisualizerSettings& visualizer = g_app.settings.visualizer;
    if (!visualizer.enabled || !g_app.features || !g_app.features->oled) {
        return false;
    }
    if (visualizer.mediaOnly && now - g_app.mediaCheckTick >= kMediaCheckMs) {
        g_app.mediaCheckTick = now;
        if (g_app.media.AnyPlaying()) {
            g_app.mediaPlayingTick = now;
        }
    }
    const ULONGLONG used = std::max(deviceEvent, g_app.lastActivityTick);
    if (used && now - used < kDisplayYieldMs) {
        return false;
    }
    const ULONGLONG playing = visualizer.mediaOnly ? g_app.mediaPlayingTick : g_app.lastSoundTick;
    return playing && now - playing < kVisualizerLingerMs;
}

void StopIdleScreen() {
    KillTimer(g_app.window, kTimerIdleScreen);
    g_app.audioTap->Stop();
    g_app.displayShows = App::DisplayShows::Device;
    g_app.display.Close();
    UpdateGgWatch();
    g_app.idleAnimation.reset();
    if (g_app.idleShowing) {
        g_app.idleShowing = false;
        PushIdleScreen();
    }
}

// Plays the screensaver on the headset display while nothing plays and nobody uses the headset.
void CheckIdleScreen() {
    if (!g_app.display.IsOpen()) {
        return;
    }
    const ULONGLONG now = GetTickCount64();
    const bool sound = g_app.mixer.PeakLevel() > kSilencePeak;
    if (sound) {
        g_app.lastSoundTick = now;
    }
    const ULONGLONG deviceEvent = g_app.device.LastEventTick();
    // While one of SteelSeries GG's apps may show something, the screensaver holds still instead of drawing
    // over it; GG's frames, animated or not, stay on the display. After the hold the screensaver goes on or
    // gives the display back as usual. Using the headset or the app ends the hold.
    if (g_app.display.IsPlaying() && g_app.ggScreen.Any()) {
        // What a helper handed GG, with or without sound, since the headset or the app was last used.
        for (const GgScreenApp app : {GgScreenApp::Discord, GgScreenApp::Track, GgScreenApp::Visualizer}) {
            const ULONGLONG tick = g_app.ggWatch.LastEvent(app);
            if (tick > deviceEvent && tick > g_app.lastActivityTick && now < tick + GgShowMs(app)) {
                HoldIdleScreen(now, tick + GgShowMs(app), GgAppName(app));
            }
        }
        // Someone talking in a Discord voice channel stays on the display as long as they talk.
        if (g_app.ggScreen.discord && g_app.mixer.AppPeak(IsDiscordApp) > kChatActivePeak) {
            HoldIdleScreen(now, now + kGgMessageMs, "Discord sound");
        }
    }
    if (g_app.idleHoldUntil &&
        (now >= g_app.idleHoldUntil || deviceEvent > g_app.idleHoldStart || g_app.lastActivityTick > g_app.idleHoldStart)) {
        g_app.idleHoldUntil = 0;
    }
    g_app.display.Hold(g_app.idleHoldUntil != 0);
    if (g_app.idleHoldUntil) {
        UpdateGgWatch();
        return;
    }
    const bool used = deviceEvent >= g_app.idlePreviewStart || g_app.lastActivityTick > g_app.idlePreviewStart;
    if (sound || used) {
        g_app.idlePreviewUntil = 0;
    }
    if (used || (g_app.idleManualUntil && now >= g_app.idleManualUntil)) {
        if (g_app.idleManualUntil && now >= g_app.idleManualUntil && !g_app.idlePinned) {
            // After a test the screensaver waits its whole delay again.
            g_app.lastActivityTick = now;
        }
        g_app.idleManual = false;
        g_app.idleManualUntil = 0;
    }
    const ULONGLONG activity = std::max({g_app.lastSoundTick, g_app.lastActivityTick, deviceEvent});
    const ULONGLONG delay = static_cast<ULONGLONG>(g_app.settings.idleScreen.delaySeconds) * 1000;
    // Turned on by the hotkey, the screensaver stays; using the headset or the app shows the GameDAC's own screen
    // for a moment, so a volume change shows there.
    const ULONGLONG lastUse = std::max(deviceEvent, g_app.lastActivityTick);
    const bool pinned = g_app.idlePinned && !(lastUse && now - lastUse < kDisplayYieldMs);
    // SteelSeries GG may draw on the same display too; the screensaver runs regardless.
    const bool byHand = g_app.idleManual || pinned || now < g_app.idlePreviewUntil;
    const bool idle = g_app.idleAnimation && (byHand || now - activity >= delay);
    // The visualizer while something plays; a screensaver started by hand goes first.
    const bool visualize = !byHand && VisualizerWanted(now, deviceEvent);
    ShowOnDisplay(visualize ? App::DisplayShows::Visualizer
                  : idle    ? App::DisplayShows::Screensaver
                            : App::DisplayShows::Device);
    if (idle != g_app.idleShowing) {
        g_app.idleShowing = idle;
        PushIdleScreen();
    }
    UpdateGgWatch();
}

// Discord flashing on the taskbar has a message that GG's Discord app puts on the display: the screensaver
// holds still meanwhile, as it does for Discord's sound.
void OnWindowFlash(HWND window) {
    if (!g_app.display.IsPlaying() || !g_app.ggScreen.discord) {
        return;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    HANDLE process = pid ? OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid) : nullptr;
    if (!process) {
        return;
    }
    wchar_t path[MAX_PATH];
    DWORD length = MAX_PATH;
    const bool known = QueryFullProcessImageNameW(process, 0, path, &length);
    CloseHandle(process);
    if (!known) {
        return;
    }
    if (IsDiscordApp(FileNameOf(path))) {
        const ULONGLONG now = GetTickCount64();
        HoldIdleScreen(now, now + kGgMessageMs, "Discord flashes");
        CheckIdleScreen();
    }
}

// The screensaver hotkey: turns it on until the hotkey is pressed again, or off when it is on or shows.
// Without a chosen animation the popup says where to pick one.
void ToggleScreensaver() {
    if (!g_app.display.IsOpen() || !g_app.idleAnimation) {
        if (g_app.settings.showPopup) {
            g_app.osd->ShowMessage(kGlyphScreensaver, T(Text::ScreensaverOffTitle), T(Text::ScreensaverOffText));
        }
        return;
    }
    const ULONGLONG now = GetTickCount64();
    if (g_app.idlePinned || g_app.displayShows == App::DisplayShows::Screensaver) {
        g_app.idlePinned = false;
        g_app.idleManual = false;
        g_app.idleManualUntil = 0;
        g_app.idlePreviewUntil = 0;
        g_app.lastActivityTick = now;
    } else {
        g_app.idlePinned = true;
        g_app.idleManual = false;
        g_app.idleManualUntil = 0;
        g_app.idlePreviewStart = now;
    }
    Log("screensaver %s by the hotkey", g_app.idlePinned ? "on" : "off");
    CheckIdleScreen();
}

void StartIdleScreen(bool preview);

// The Test button: the chosen screensaver for a few seconds, also while sound plays.
void TestScreensaver() {
    StartIdleScreen(false);
    if (!g_app.display.IsOpen() || !g_app.idleAnimation) {
        return;
    }
    const ULONGLONG now = GetTickCount64();
    g_app.idleManual = true;
    g_app.idleManualUntil = now + kIdlePreviewMs;
    g_app.idlePreviewStart = now;
    CheckIdleScreen();
    PushIdleScreen();
}

// The add-on screen that belongs on the display now; force puts it there again, e.g. after the display reopened.
void UpdateAddOnScreen(bool force = false) {
    const ULONGLONG now = GetTickCount64();
    ULONGLONG next = 0;
    const std::optional<AddOnCard> card = g_app.addOnScreens.Current(now, next);
    if (force || card != g_app.addOnCard) {
        g_app.addOnCard = card;
        g_app.display.SetOverlay(card ? CreateAddOnScreen(*card) : nullptr);
    }
    if (next > now) {
        SetTimer(g_app.window, kTimerAddOnScreen, static_cast<UINT>(next - now + 1), nullptr);
    } else {
        KillTimer(g_app.window, kTimerAddOnScreen);
    }
}

// The display is open for the screensaver, for the visualizer, and for add-on screens while the add-ons run;
// its regular check runs while it is open.
void UpdateDisplayUse() {
    const bool wanted = g_app.idleAnimation || g_app.addOns.IsServing() || g_app.settings.visualizer.enabled;
    if (!g_app.display.IsOpen() && wanted) {
        const std::optional<DisplayCollection> collection =
            g_app.device.IsOpen() ? g_app.device.Display() : std::nullopt;
        if (collection && g_app.display.Open(*collection)) {
            UpdateIdleBadge();
            UpdateAddOnScreen(true);
        }
    } else if (g_app.display.IsOpen() && !wanted) {
        g_app.audioTap->Stop();
        g_app.displayShows = App::DisplayShows::Device;
        g_app.display.Close();
    }
    if (g_app.display.IsOpen()) {
        SetTimer(g_app.window, kTimerIdleScreen, kIdleCheckMs, nullptr);
    } else {
        KillTimer(g_app.window, kTimerIdleScreen);
    }
}

void PushAddOns() {
    ui::AddOnsView view;
    const bool display = g_app.device.IsOpen() && g_app.device.Display().has_value();
    view.state = g_app.addOns.IsServing() ? ui::AddOnsView::State::Running
                 : g_app.ggRunning        ? ui::AddOnsView::State::GgRunning
                 : !display               ? ui::AddOnsView::State::NoDisplay
                                          : ui::AddOnsView::State::Off;
    for (const AddOnSettings& addOn : g_app.settings.addOns) {
        view.running.push_back(g_app.addOns.IsRunning(addOn.path));
    }
    g_app.panel->SetAddOns(view);
}

// Third-party add-ons run while SteelSeries GG is closed and the headset has a display for them.
void UpdateAddOns() {
    const bool run = !g_app.ggRunning && g_app.device.IsOpen() && g_app.device.Display().has_value();
    const bool serving = g_app.addOns.IsServing();
    g_app.addOns.Update(run, g_app.settings.addOns, g_app.window, WM_APP_GAMESENSE);
    if (serving != g_app.addOns.IsServing()) {
        if (!g_app.addOns.IsServing()) {
            g_app.addOnScreens.Clear();
        }
        UpdateDisplayUse();
    }
    UpdateAddOnScreen();
    PushAddOns();
}

// (Re)starts the screensaver with the current settings; preview shows it right away.
void StartIdleScreen(bool preview) {
    StopIdleScreen();
    const IdleScreenSettings& idle = g_app.settings.idleScreen;
    const std::optional<DisplayCollection> collection = g_app.device.IsOpen() ? g_app.device.Display() : std::nullopt;
    if (!collection || idle.animation == IdleAnimationKind::Off) {
        if (idle.animation == IdleAnimationKind::Off) {
            g_app.idlePinned = false;
        }
        UpdateDisplayUse();
        return;
    }
    g_app.idleAnimation = CreateIdleAnimation(idle.animation, idle.image);
    if (!g_app.idleAnimation || !g_app.display.Open(*collection)) {
        g_app.idleAnimation.reset();
        UpdateDisplayUse();
        return;
    }
    UpdateIdleBadge();
    UpdateAddOnScreen(true);
    const ULONGLONG now = GetTickCount64();
    g_app.lastActivityTick = now;
    g_app.idlePreviewStart = now;
    g_app.idlePreviewUntil = preview ? now + kIdlePreviewMs : 0;
    g_app.idleManual = false;
    g_app.idleManualUntil = 0;
    SetTimer(g_app.window, kTimerIdleScreen, kIdleCheckMs, nullptr);
    CheckIdleScreen();
}

// With the log on and no supported headset, a SteelSeries headset OpenChatMix does not know is recognized by its
// audio devices, and its HID reports go into the log for adding it later.
void ProbeUnsupportedHeadsets() {
    if (!g_app.settings.log) {
        return;
    }
    std::vector<uint16_t> productIds;
    std::vector<AudioEndpoint> endpoints = ListRenderEndpoints(true);
    const std::vector<AudioEndpoint> captures = ListCaptureEndpoints();
    endpoints.insert(endpoints.end(), captures.begin(), captures.end());
    for (const AudioEndpoint& endpoint : endpoints) {
        const std::optional<uint16_t> productId = endpoint.steelSeriesProductId;
        if (productId && !FindDeviceModel(*productId) &&
            std::find(productIds.begin(), productIds.end(), *productId) == productIds.end()) {
            productIds.push_back(*productId);
        }
    }
    std::sort(productIds.begin(), productIds.end());
    g_app.probe.Start(productIds);
}

void ConnectDevice() {
    if (g_app.device.IsOpen()) {
        return;
    }
    if (g_app.device.Open(g_app.window)) {
        g_app.probe.Stop();
        const DeviceModel& model = *g_app.device.Model();
        Log("connected: %ls", model.name);
        g_app.features = &FeaturesFor(model.family);
        g_app.headset = HeadsetState();
        LoadHeadsetState(g_app.iniPath, model.productId, *g_app.features, g_app.headset);
        g_app.mixer.SetTarget(model.productId);
        g_app.levels = DialLevels(g_app.levels);
        g_app.panel->SetLevels(g_app.levels);
        g_app.device.Start(g_app.levels);
        StartIdleScreen(false);
        UpdateAddOns();
    } else {
        Log("no supported headset");
        g_app.mixer.SetTarget(std::nullopt);
        ProbeUnsupportedHeadsets();
    }
    g_app.panel->SetBalanceDial(HasBalanceDial());
    ApplyLevels();
    RefreshOutputName();
    UpdateStatus();
    PushHeadset();
    RefreshApps();
    NotifyGg();
    if (g_app.settings.routing.enabled && g_app.settings.routing.output.empty()) {
        StartRouting();
    }
}

// Sends the start sequence again, e.g. after SteelSeries GG released the headset.
void ReinitializeDevice(const char* reason) {
    if (g_app.device.IsOpen()) {
        Log("re-initializing headset: %s", reason);
        g_app.device.Start(g_app.levels);
    }
}

void ReconnectDevice() {
    Log("reconnect requested");
    StopIdleScreen();
    g_app.device.Close();
    ConnectDevice();
}

void HandleDeviceLost() {
    Log("headset lost");
    SaveHeadset();
    StopIdleScreen();
    g_app.device.Close();
    UpdateAddOns();
    g_app.features = nullptr;
    g_app.mixer.SetTarget(std::nullopt);
    g_app.panel->SetBalanceDial(false);
    ApplyLevels();
    RefreshOutputName();
    UpdateStatus();
    PushHeadset();
    RefreshApps();
    if (g_app.settings.routing.enabled) {
        UpdateRouting();
    }
    // One retry covers transient read errors; real unplug/replug is handled by WM_DEVICECHANGE.
    SetTimer(g_app.window, kTimerReconnect, kReconnectDelayMs, nullptr);
}

std::optional<NamedEq> FindEqPreset(const std::wstring& name, EqChannel channel) {
    for (const auto& list : {BuiltinEqPresets(channel), SonarEqPresets(channel), LoadParametricPresets()}) {
        for (const auto& preset : list) {
            if (_wcsicmp(preset.name.c_str(), name.c_str()) == 0) {
                return preset;
            }
        }
    }
    return std::nullopt;
}

void RestoreProfile() {
    App::ProfileRestore& restore = g_app.profileRestore;
    if (restore.levels) {
        SetLevels(*restore.levels, false);
    }
    bool equalizers = false;
    for (auto [saved, state] : {std::pair{&restore.gameEq, &g_app.settings.gameEq},
                                std::pair{&restore.chatEq, &g_app.settings.chatEq},
                                std::pair{&restore.mediaEq, &g_app.settings.mediaEq}}) {
        if (*saved) {
            *state = **saved;
            equalizers = true;
        }
    }
    if (equalizers) {
        ScheduleSave();
        PushEqualizers();
    }
    if (restore.headsetEq >= 0) {
        g_app.host.OnEqualizerPreset(restore.headsetEq);
    }
    restore = {};
    PushSettings();
}

// Also used when the running game's profile is edited: what the profile no longer sets goes back,
// and the values to restore stay the ones from before the game started.
void ApplyProfile(const GameProfile& profile) {
    App::ProfileRestore& restore = g_app.profileRestore;
    if (profile.balance) {
        if (!restore.levels) {
            restore.levels = g_app.levels;
        }
        SetLevels(DialLevels(LevelsFromBalance(*profile.balance)), false);
    } else if (restore.levels) {
        SetLevels(*restore.levels, false);
        restore.levels.reset();
    }
    // Each channel's preset; a channel the profile leaves alone gets back what it had before.
    auto applyEq = [](const std::wstring& name, EqChannel channel, EqChannelSettings& state,
                      std::optional<EqChannelSettings>& saved) {
        const auto preset = name.empty() ? std::nullopt : FindEqPreset(name, channel);
        if (preset) {
            if (!saved) {
                saved = state;
            }
            state.enabled = true;
            state.preset = preset->name;
            AssignBands(state.eq, preset->eq);
            return true;
        }
        if (saved) {
            state = *saved;
            saved.reset();
            return true;
        }
        return false;
    };
    bool equalizers = applyEq(profile.gameEq, EqChannel::Game, g_app.settings.gameEq, restore.gameEq);
    equalizers = applyEq(profile.chatEq, EqChannel::Chat, g_app.settings.chatEq, restore.chatEq) || equalizers;
    equalizers = applyEq(profile.mediaEq, EqChannel::Media, g_app.settings.mediaEq, restore.mediaEq) || equalizers;
    if (equalizers) {
        ScheduleSave();
        PushEqualizers();
    }
    const bool headset = g_app.features && g_app.device.IsOpen();
    if (profile.headsetEq >= 0 && headset) {
        if (restore.headsetEq < 0 && g_app.headset.eqPreset != profile.headsetEq) {
            restore.headsetEq = g_app.headset.eqPreset;
        }
        if (g_app.headset.eqPreset != profile.headsetEq) {
            g_app.host.OnEqualizerPreset(profile.headsetEq);
        }
    } else if (restore.headsetEq >= 0) {
        if (headset) {
            g_app.host.OnEqualizerPreset(restore.headsetEq);
        }
        restore.headsetEq = -1;
    }
    PushSettings();
}

// The first configured game that runs decides the profile; reapply applies an edited active profile again.
void CheckProfiles(const ProcessSet& processes, bool reapply = false) {
    const GameProfile* match = nullptr;
    for (const auto& profile : g_app.settings.profiles) {
        if (processes.count(profile.exe)) {
            match = &profile;
            break;
        }
    }
    const std::wstring exe = match ? match->exe : L"";
    if (_wcsicmp(exe.c_str(), g_app.activeProfile.c_str()) == 0) {
        if (match && reapply) {
            const GameProfile profile = *match;
            ApplyProfile(profile);
        }
        return;
    }
    if (!g_app.activeProfile.empty()) {
        Log("profile off: %s", Utf8(g_app.activeProfile).c_str());
        RestoreProfile();
    }
    g_app.activeProfile = exe;
    if (match) {
        Log("profile on: %s", Utf8(exe).c_str());
        const GameProfile profile = *match;
        ApplyProfile(profile);
    }
}

void WatchGg() {
    // The routing latency settles once audio flows; refresh it with the same low-frequency tick.
    if (g_app.settings.routing.enabled && g_app.panel->IsVisible()) {
        UpdateRouting();
    }
    if (g_app.settings.mic.enabled && g_app.panel->IsVisible()) {
        PushMic();
    }
    // The process list serves game profiles and, with a SteelSeries headset, noticing GG.
    if (g_app.settings.profiles.empty() && g_app.activeProfile.empty() && !g_app.device.IsOpen() && !g_app.ggRunning) {
        return;
    }
    std::vector<GgHelperProcess> helpers;
    const ProcessSet processes = RunningProcesses(&helpers);
    CheckProfiles(processes);
    const bool running = IsGgRunning(processes);
    // Without GG, helpers that run are OpenChatMix's own add-ons or left over: nothing GG draws.
    if (!running) {
        helpers.clear();
    }
    const GgScreenApps screenApps = FindGgScreenApps(helpers);
    g_app.ggHelpers = std::move(helpers);
    if (screenApps != g_app.ggScreen) {
        g_app.ggScreen = screenApps;
        Log("SteelSeries GG apps on the display: %s",
            screenApps.names.empty() ? "none" : Utf8(screenApps.names).c_str());
        PushIdleScreen();
    }
    UpdateGgWatch();
    if (g_app.addOns.CheckProcesses()) {
        PushAddOns();
    }
    if (running == g_app.ggRunning) {
        return;
    }
    g_app.ggRunning = running;
    Log("SteelSeries GG %s", running ? "started" : "stopped");
    // GG runs its own helpers; the add-ons give way to them before they start.
    UpdateAddOns();
    CheckIdleScreen();
    if (running) {
        NotifyGg();
    } else {
        g_app.ggDismissed = false;
        g_app.ggNotified = false;
        ReinitializeDevice("SteelSeries GG stopped");
    }
    UpdateStatus();
}

void ShowTrayMenu(int x, int y) {
    g_app.trayFlyout->HideCard();
    std::vector<ui::TrayFlyout::MenuEntry> entries{
        {kGlyphTrayOpen, T(Text::TrayOpen), IDM_OPEN},
        {kGlyphTraySettings, T(Text::TraySettings), IDM_SETTINGS},
        {kGlyphTrayReconnect, T(Text::TrayReconnect), IDM_RECONNECT},
        {kGlyphTrayStartup, T(Text::StartWithWindows), IDM_AUTOSTART, IsAutostartEnabled()},
        {},
        {kGlyphTrayQuit, T(Text::TrayExit), IDM_EXIT, false, true},
    };
    g_app.trayFlyout->ShowMenu(TrayIconRect(), {x, y}, DeviceName() + L" · " + LevelsText(), std::move(entries));
}

bool IsSupportedArrival(WPARAM event, LPARAM data) {
    if (event != DBT_DEVICEARRIVAL || !data) {
        return false;
    }
    const auto* header = reinterpret_cast<const DEV_BROADCAST_HDR*>(data);
    if (header->dbch_devicetype != DBT_DEVTYP_DEVICEINTERFACE) {
        return false;
    }
    const auto* deviceInterface = reinterpret_cast<const DEV_BROADCAST_DEVICEINTERFACE_W*>(data);
    const auto productId = ParseSteelSeriesProductId(deviceInterface->dbcc_name);
    return productId && FindDeviceModel(*productId);
}

// The NovaChatMix prototype drives the same headset; it must not keep running next to this app.
void CloseLegacyInstance() {
    HWND legacy = FindWindowW(kLegacyWindowClass, nullptr);
    if (!legacy) {
        return;
    }
    PostMessageW(legacy, WM_CLOSE, 0, 0);
    const ULONGLONG deadline = GetTickCount64() + kLegacyExitTimeoutMs;
    while (IsWindow(legacy) && GetTickCount64() < deadline) {
        Sleep(100);
    }
}

void Cleanup() {
    if (g_app.cleanedUp) {
        return;
    }
    g_app.cleanedUp = true;
    // Leaving takes a moment (plugins, devices) and is no hang.
    g_app.hangWatch.Stop();

    // Settings that a game profile replaced are not kept.
    if (!g_app.activeProfile.empty()) {
        RestoreProfile();
    }
    g_app.settings.levels = g_app.levels;
    UnrouteApps();
    Save();
    SaveHeadset();

    StopIdleScreen();
    g_app.ggWatch.Stop();
    g_app.addOns.Stop();
    g_app.panel.reset();
    g_app.osd.reset();
    g_app.router.Stop();
    ReleaseRoutingDevices(true);
    UnloadMicPlugins();
    g_app.mic.Stop();
    g_app.micLevels.Stop();
    g_app.pluginThread.Stop();
    g_app.probe.Stop();
    // Windows persists per-app volume, so every app changed by ChatMix goes back to full level.
    g_app.device.Stop();
    g_app.mixer.ResetVolumes();
    g_app.device.Close();
    g_app.mixer.Shutdown();

    for (int action = 0; action < kHotkeyCount; ++action) {
        UnregisterHotKey(g_app.window, kHotkeyIdBase + action);
    }
    if (g_app.mouseHook) {
        UnhookWindowsHookEx(g_app.mouseHook);
        g_app.mouseHook = nullptr;
    }
    WTSUnRegisterSessionNotification(g_app.window);
    RemoveTray();
    if (g_app.deviceNotify) {
        UnregisterDeviceNotification(g_app.deviceNotify);
        g_app.deviceNotify = nullptr;
    }
    Log("exit");
}

void Host::OnBalanceChanged(int balance) {
    SetLevels(DialLevels(LevelsFromBalance(balance)), false);
}

void Host::OnGroupChanged(const ui::AppItem& app, AppGroup group) {
    g_app.settings.SetGroup(app.exe, group, app.path);
    if (group == AppGroup::Media) {
        g_app.mixer.RestoreApp(app.exe);
    }
    Save();
    ApplyLevels();
    RefreshApps();
    RouteApps();
}

void Host::OnAppRemoved(const ui::AppItem& app) {
    g_app.settings.apps.erase(app.exe);
    // Its ChatMix level is recomputed for the Game side; in virtual device mode it moves to the Game device.
    g_app.mixer.RestoreApp(app.exe);
    Save();
    ApplyLevels();
    RefreshApps();
    RouteApps();
}

std::vector<ui::AppItem> Host::RunningApps() {
    std::vector<ProcessApp> processes = WindowedApps();
    for (const auto& app : g_app.mixer.ActiveApps()) {
        const bool known = std::any_of(processes.begin(), processes.end(), [&](const ProcessApp& process) {
            return _wcsicmp(process.exe.c_str(), app.exe.c_str()) == 0;
        });
        if (!known) {
            processes.push_back(app);
        }
    }
    std::vector<ui::AppItem> items;
    for (const auto& process : processes) {
        items.push_back({process.exe, process.path, DisplayName(process.path, process.exe),
                         g_app.settings.GroupOf(process.exe), true});
    }
    std::sort(items.begin(), items.end(), [](const ui::AppItem& a, const ui::AppItem& b) {
        return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    return items;
}

std::wstring Host::AppName(const std::wstring& path) {
    return DisplayName(path, FileNameOf(path));
}

void Host::OnHotkeyRecording(bool active) {
    g_app.hotkeysSuspended = active;
    RegisterHotkeys();
}

void Host::OnHotkeyChanged(int action, Hotkey hotkey) {
    // One combination drives one action; an older assignment of it is cleared.
    for (auto& other : g_app.settings.hotkeys) {
        if (!hotkey.IsEmpty() && other == hotkey) {
            other = {};
        }
    }
    g_app.settings.hotkeys[action] = hotkey;
    Save();
    RegisterHotkeys();
}

void Host::OnAutostartChanged(bool enabled) {
    SetAutostart(enabled);
    PushSettings();
}

void Host::OnLanguageChanged(Language language) {
    g_app.settings.language = language;
    SetLanguage(language);
    Save();
    UpdateStatus();
    g_app.panel->Refresh();
}

void Host::OnWarningDismissed() {
    g_app.ggDismissed = true;
    UpdateStatus();
}

void Host::OnHeadsetSetting(uint8_t command, int value) {
    if (!g_app.features || !g_app.device.IsOpen() || !g_app.features->FindSetting(command)) {
        return;
    }
    SendHeadsetCommand(SettingCommand(g_app.headset, command, value));
    g_app.headset.values[command] = static_cast<int16_t>(value);
    PushHeadset();
    ScheduleHeadsetSave();
}

void Host::OnEqualizerPreset(int index) {
    if (!g_app.features || !g_app.device.IsOpen()) {
        return;
    }
    const EqualizerSpec& eq = g_app.features->equalizer;
    const int custom = eq.CustomIndex();
    if (index < 0 || index > custom) {
        return;
    }
    if (eq.presetCommand) {
        const uint8_t payload[] = {eq.presetCommand, static_cast<uint8_t>(index)};
        SendHeadsetCommand(payload);
    } else if (index < custom) {
        SendEqualizerCurve(eq.presets[index].curve);
    } else if (g_app.headset.eqCurveKnown) {
        SendEqualizerCurve(g_app.headset.eqCurve);
    }
    g_app.headset.eqPreset = index;
    PushHeadset();
    ScheduleHeadsetSave();
}

void Host::OnEqualizerCurve(const EqCurve& curve) {
    if (!g_app.features || !g_app.device.IsOpen()) {
        return;
    }
    const EqualizerSpec& eq = g_app.features->equalizer;
    const int custom = eq.CustomIndex();
    if (eq.presetCommand && g_app.headset.eqPreset != custom) {
        const uint8_t payload[] = {eq.presetCommand, static_cast<uint8_t>(custom)};
        SendHeadsetCommand(payload);
    }
    SendEqualizerCurve(curve);
    g_app.headset.eqCurve = curve;
    g_app.headset.eqCurveKnown = true;
    g_app.headset.eqPreset = custom;
    PushHeadset();
    ScheduleHeadsetSave();
}

void Host::OnRoutingEnabled(bool enabled) {
    RoutingSettings& routing = g_app.settings.routing;
    if (enabled == routing.enabled) {
        return;
    }
    if (enabled) {
        // App volumes go back to 100% before the streams take over the balance.
        g_app.mixer.ResetVolumes();
        routing.enabled = true;
        Save();
        StartRouting();
    } else {
        UnrouteApps();
        g_app.router.Stop();
        ReleaseRoutingDevices(true);
        routing.enabled = false;
        Save();
        UpdateMixerEndpoints();
        ApplyLevels();
        RefreshApps();
        UpdateRouting();
        RefreshOutputName();
        UpdateStatus();
        PushSettings();
    }
}

void Host::OnRoutingDevice(ui::RoutingSlot slot, const std::wstring& id) {
    RoutingSettings& routing = g_app.settings.routing;
    if (slot == ui::RoutingSlot::MicInput || slot == ui::RoutingSlot::MicOutput) {
        return;  // The panel edits the microphone devices as settings.
    }
    const bool source = slot == ui::RoutingSlot::Game || slot == ui::RoutingSlot::Chat || slot == ui::RoutingSlot::Media;
    if (source && !id.empty() && !IsVirtualDeviceId(id)) {
        // A channel plays from a virtual cable only; a real device here would be looped and switched off.
        Log("routing: refused a device that is not a virtual cable");
        return;
    }
    if (source) {
        // The previous device is handed back before the new one is taken over.
        g_app.router.Stop();
        ReleaseRoutingDevices(true);
    }
    switch (slot) {
    case ui::RoutingSlot::Game: routing.game = id; break;
    case ui::RoutingSlot::Chat: routing.chat = id; break;
    case ui::RoutingSlot::Media: routing.media = id; break;
    case ui::RoutingSlot::Output: routing.output = id; break;
    case ui::RoutingSlot::GameOutput: routing.gameOutput = id; break;
    case ui::RoutingSlot::ChatOutput: routing.chatOutput = id; break;
    case ui::RoutingSlot::MediaOutput: routing.mediaOutput = id; break;
    case ui::RoutingSlot::MicInput:
    case ui::RoutingSlot::MicOutput: break;
    }
    Save();
    if (routing.enabled) {
        StartRouting();
    } else {
        UpdateRouting();
    }
    PushSettings();
}

void Host::OnRoutingManage(bool enabled) {
    RoutingSettings& routing = g_app.settings.routing;
    routing.manageDevices = enabled;
    Save();
    if (!enabled) {
        // The devices stay as they are; only the previous defaults come back.
        ReleaseRoutingDevices(false);
    } else if (routing.enabled) {
        AcquireRoutingDevices();
    }
    UpdateRouting();
}

void Host::OnIdleAnimation(IdleAnimationKind animation) {
    g_app.settings.idleScreen.animation = animation;
    Save();
    StartIdleScreen(false);
    PushIdleScreen();
}

void Host::OnIdleTest() {
    TestScreensaver();
}

void Host::OnAddOnPreview(const std::wstring& path, const std::wstring& event) {
    const auto addOn = std::find_if(g_app.settings.addOns.begin(), g_app.settings.addOns.end(),
                                    [&](const AddOnSettings& item) { return _wcsicmp(item.path.c_str(), path.c_str()) == 0; });
    if (addOn == g_app.settings.addOns.end() || !g_app.display.IsOpen()) {
        return;
    }
    const std::wstring self = g_app.addOnScreens.SelfName();
    std::map<std::wstring, std::wstring> fields{
        {L"title", T(Text::PreviewTitle)},
        {L"message", T(Text::PreviewMessage)},
        {L"user", self.empty() ? std::wstring(T(Text::PreviewUser)) : self},
        {L"channel", T(Text::PreviewChannel)},
    };
    g_app.addOnScreens.Preview(ScreenFor(*addOn, event), std::move(fields), GetTickCount64());
    UpdateAddOnScreen();
}

void Host::OnIdleBluetoothIcon(IconCorner corner) {
    g_app.settings.idleScreen.bluetoothIcon = corner;
    Save();
    UpdateIdleBadge();
    PushIdleScreen();
}

void Host::OnIdleDelay(int seconds) {
    g_app.settings.idleScreen.delaySeconds = seconds;
    Save();
    PushIdleScreen();
}

void Host::OnIdleImage(const std::wstring& path) {
    g_app.settings.idleScreen.image = path;
    g_app.settings.idleScreen.animation = IdleAnimationKind::Image;
    Save();
    StartIdleScreen(false);
    PushIdleScreen();
}

std::vector<AudioEndpoint> Host::RecordingDevices() {
    std::vector<AudioEndpoint> endpoints = ListCaptureEndpoints();
    // The cable's own recording side would feed the chain back into itself.
    const std::vector<AudioEndpoint> renders = ListRenderEndpoints();
    const std::wstring output = MicOutputId(renders);
    for (const auto& render : renders) {
        if (render.id == output) {
            const std::wstring loop = PairedCaptureEndpoint(render, endpoints);
            std::erase_if(endpoints, [&](const AudioEndpoint& endpoint) { return endpoint.id == loop; });
        }
    }
    std::erase_if(endpoints, IsSonarEndpoint);
    return endpoints;
}

void Host::OnMicPluginAdd(const std::wstring& path) {
    MicPlugin plugin{path, NewPluginId(), true};
    g_app.settings.mic.plugins.push_back(plugin);
    if (g_app.micPluginsLoaded) {
        g_app.micPlugins.push_back(LoadMicPlugin(plugin));
        PushMicPlugins();
        if (!g_app.micPlugins.back()) {
            Log("plugin not loaded: %s", Utf8(path).c_str());
        }
    }
    Save();
    PushSettings();
    PushMic();
}

void Host::OnMicPluginRemove(size_t index) {
    std::vector<MicPlugin>& plugins = g_app.settings.mic.plugins;
    if (index >= plugins.size()) {
        return;
    }
    DeleteFileW(PluginStatePath(plugins[index]).c_str());
    plugins.erase(plugins.begin() + index);
    if (index < g_app.micPlugins.size()) {
        std::unique_ptr<AudioPlugin> removed = std::move(g_app.micPlugins[index]);
        g_app.micPlugins.erase(g_app.micPlugins.begin() + index);
        // The audio thread lets go of it before it is destroyed on its own thread.
        PushMicPlugins();
        g_app.pluginThread.Run([&] { removed.reset(); });
    }
    Save();
    PushSettings();
    PushMic();
}

void Host::OnMicPluginMove(size_t from, size_t to) {
    std::vector<MicPlugin>& plugins = g_app.settings.mic.plugins;
    if (from >= plugins.size() || to >= plugins.size() || from == to) {
        return;
    }
    MoveItem(plugins, from, to);
    if (from < g_app.micPlugins.size() && to < g_app.micPlugins.size()) {
        MoveItem(g_app.micPlugins, from, to);
        PushMicPlugins();
    }
    Save();
    PushSettings();
    PushMic();
}

void Host::OnMicPluginEditor(size_t index) {
    if (index < g_app.micPlugins.size() && g_app.micPlugins[index]) {
        AudioPlugin* plugin = g_app.micPlugins[index].get();
        g_app.pluginThread.Run([plugin] { plugin->OpenEditor(); });
    }
}

void Host::OnMicPluginDeepFilter(size_t index, const DeepFilterSettings& settings) {
    if (index >= g_app.micPlugins.size() || !g_app.micPlugins[index] || index >= g_app.settings.mic.plugins.size()) {
        return;
    }
    AudioPlugin* plugin = g_app.micPlugins[index].get();
    const std::wstring path = PluginStatePath(g_app.settings.mic.plugins[index]);
    // Kept at once, like the settings: the plugin has no window whose closing would save it.
    g_app.pluginThread.Run([plugin, &settings, &path] {
        plugin->SetDeepFilter(settings);
        WriteFileBytes(path, plugin->SaveState());
    });
    PushMic();
}

std::vector<AudioEndpoint> Host::PlaybackDevices() {
    std::vector<AudioEndpoint> endpoints = ListRenderEndpoints();
    std::erase_if(endpoints, IsSonarEndpoint);
    return endpoints;
}

void Host::OnSettingsEdit(const std::function<void(Settings&)>& edit) {
    const Settings before = g_app.settings;
    edit(g_app.settings);
    ScheduleSave();
    const Settings& after = g_app.settings;
    if (!(before.appearance == after.appearance)) {
        ApplyAppearance();
    }
    if (before.mediaGroup != after.mediaGroup) {
        // With a Media device the bridge for it starts or stops with the group.
        if (after.routing.enabled && !after.routing.media.empty()) {
            StartRouting();
        }
        ApplyLevels();
        RefreshApps();
    } else if (before.gameVolume != after.gameVolume || before.chatVolume != after.chatVolume) {
        ApplyLevels();
    }
    if (before.routing.autoRoute != after.routing.autoRoute) {
        SyncAppRouting();
    }
    if (!(before.gameEq == after.gameEq) || !(before.chatEq == after.chatEq) || !(before.mediaEq == after.mediaEq) ||
        before.equalizer != after.equalizer) {
        PushEqualizers();
    }
    if (before.outputs.switchOnLoss != after.outputs.switchOnLoss ||
        before.outputs.returnToPreferred != after.outputs.returnToPreferred) {
        // The order as shown becomes the order kept, so it holds while devices come and go.
        std::vector<std::wstring>& order = g_app.settings.outputs.order;
        for (const auto& endpoint : PreferredOutputs()) {
            if (std::find(order.begin(), order.end(), endpoint.id) == order.end()) {
                order.push_back(endpoint.id);
            }
        }
    }
    if (!(before.addOns == after.addOns)) {
        UpdateAddOns();
    }
    if (!(before.visualizer == after.visualizer)) {
        UpdateDisplayUse();
        CheckIdleScreen();
    }
    if (before.wheelModifiers != after.wheelModifiers) {
        UpdateMouseHook();
    }
    if (before.mic.enabled != after.mic.enabled || before.mic.input != after.mic.input ||
        before.mic.output != after.mic.output || before.mic.keepLevels != after.mic.keepLevels ||
        before.mic.monitor != after.mic.monitor) {
        StartMic();
    } else if (before.mic.noise != after.mic.noise || before.mic.compressor != after.mic.compressor ||
               before.mic.order != after.mic.order ||
               !(before.micEq == after.micEq) || before.equalizer != after.equalizer) {
        g_app.mic.SetSettings(MicDsp());
    }
    if (before.mic.plugins != after.mic.plugins) {
        PushMicPlugins();
        PushMic();
    }
    if (!(before.ducking == after.ducking)) {
        UpdateDucking();
    }
    if (before.warnGg != after.warnGg) {
        UpdateStatus();
    }
    if (!before.checkUpdates && after.checkUpdates) {
        MaybeCheckForUpdate();
    }
    if (before.profiles != after.profiles) {
        CheckProfiles(RunningProcesses(), true);
    }
    PushSettings();
}

void ToggleChatMute() {
    g_app.chatMuted = !g_app.chatMuted;
    ApplyLevels();
    g_app.panel->SetChatMuted(g_app.chatMuted);
    UpdateTray(NIM_MODIFY);
    if (g_app.settings.showPopup) {
        g_app.osd->ShowMessage(g_app.chatMuted ? kGlyphMute : kGlyphChat,
                               T(g_app.chatMuted ? Text::ChatMuted : Text::ChatUnmuted),
                               std::wstring(T(Text::Chat)) + L" " + std::to_wstring(g_app.levels.chat) + L"%");
    }
}

void ToggleGameMute() {
    g_app.gameMuted = !g_app.gameMuted;
    ApplyLevels();
    g_app.panel->SetGameMuted(g_app.gameMuted);
    UpdateTray(NIM_MODIFY);
    if (g_app.settings.showPopup) {
        g_app.osd->ShowMessage(g_app.gameMuted ? kGlyphMute : kGlyphGame,
                               T(g_app.gameMuted ? Text::GameMuted : Text::GameUnmuted),
                               std::wstring(T(Text::Game)) + L" " + std::to_wstring(g_app.levels.game) + L"%");
    }
}

void ToggleMediaMute() {
    if (!g_app.settings.MediaAvailable()) {
        return;
    }
    g_app.mediaMuted = !g_app.mediaMuted;
    ApplyLevels();
    if (g_app.settings.showPopup) {
        g_app.osd->ShowMessage(g_app.mediaMuted ? kGlyphMute : kGlyphMedia,
                               T(g_app.mediaMuted ? Text::MediaMuted : Text::MediaUnmuted), T(Text::Media));
    }
}

bool IsExcludedOutput(const std::wstring& id) {
    const std::vector<std::wstring>& excluded = g_app.settings.outputs.excluded;
    return std::find(excluded.begin(), excluded.end(), id) != excluded.end();
}

// Active playback devices a person listens to (no virtual cables, no Game or Chat device), most
// preferred first; devices the order does not list follow in the order Windows reports them, and
// excluded devices come last.
std::vector<AudioEndpoint> PreferredOutputs() {
    std::vector<AudioEndpoint> endpoints = ListRenderEndpoints();
    std::erase_if(endpoints, [](const AudioEndpoint& endpoint) {
        return IsVirtualEndpoint(endpoint) || IsRoutingDevice(endpoint.id);
    });
    const std::vector<std::wstring>& order = g_app.settings.outputs.order;
    auto rank = [&](const AudioEndpoint& endpoint) {
        return std::find(order.begin(), order.end(), endpoint.id) - order.begin();
    };
    std::stable_sort(endpoints.begin(), endpoints.end(),
                     [&](const AudioEndpoint& a, const AudioEndpoint& b) { return rank(a) < rank(b); });
    std::stable_partition(endpoints.begin(), endpoints.end(),
                          [](const AudioEndpoint& endpoint) { return !IsExcludedOutput(endpoint.id); });
    return endpoints;
}

// The connected devices the order may switch to: PreferredOutputs() without the excluded ones.
std::vector<AudioEndpoint> AllowedOutputs() {
    std::vector<AudioEndpoint> endpoints = PreferredOutputs();
    std::erase_if(endpoints, [](const AudioEndpoint& endpoint) { return IsExcludedOutput(endpoint.id); });
    return endpoints;
}

// The routing output in virtual device mode, otherwise the Windows default device.
std::wstring CurrentOutput() {
    return g_app.settings.routing.enabled ? RoutingOutputId(ListRenderEndpoints()) : DefaultRenderEndpoint(eConsole);
}

void PushOutputs() {
    const std::wstring current = CurrentOutput();
    std::vector<ui::OutputView> views;
    for (const auto& endpoint : PreferredOutputs()) {
        views.push_back({endpoint.id, endpoint.name, endpoint.id == current, IsExcludedOutput(endpoint.id)});
    }
    g_app.panel->SetOutputs(std::move(views));
}

void SwitchOutput(const AudioEndpoint& target, bool popup) {
    RoutingSettings& routing = g_app.settings.routing;
    if (routing.enabled) {
        routing.output = target.id;
        Save();
        StartRouting();
        PushSettings();
    } else {
        for (const ERole role : {eConsole, eMultimedia, eCommunications}) {
            SetDefaultRenderEndpoint(target.id, role);
        }
    }
    g_app.lastOutput = target.id;
    Log("output device: %s", Utf8(target.name).c_str());
    // Listening to the microphone follows the output.
    if (g_app.settings.mic.enabled && g_app.settings.mic.monitor) {
        StartMic();
    }
    if (popup && g_app.settings.showPopup) {
        g_app.osd->ShowMessage(kGlyphSpeakers, target.name, T(Text::OutputDevice));
    }
    PushOutputs();
}

// The next device in the order of preference after the current one; excluded devices are skipped.
void NextOutputDevice() {
    const std::vector<AudioEndpoint> outputs = AllowedOutputs();
    const std::wstring current = CurrentOutput();
    const auto found = std::find_if(outputs.begin(), outputs.end(),
                                    [&](const AudioEndpoint& endpoint) { return endpoint.id == current; });
    const size_t next = found == outputs.end() ? 0 : (found - outputs.begin() + 1) % outputs.size();
    if (outputs.empty() || outputs[next].id == current) {
        if (g_app.settings.showPopup) {
            g_app.osd->ShowMessage(kGlyphSpeakers, T(Text::NoOtherOutput), DeviceName());
        }
        return;
    }
    SwitchOutput(outputs[next], true);
}

// Feeds the microphone chain from the next connected microphone, in the order Windows lists them. Virtual
// cables are left out, as in the microphone picker.
void NextMicrophone() {
    const bool popup = g_app.settings.showPopup;
    if (!g_app.settings.mic.enabled) {
        if (popup) {
            g_app.osd->ShowMessage(kGlyphMicrophone, T(Text::MicProcessingOff), T(Text::MicInput));
        }
        return;
    }
    std::vector<AudioEndpoint> microphones = g_app.host.RecordingDevices();
    std::erase_if(microphones, IsVirtualEndpoint);
    const auto found = std::find_if(microphones.begin(), microphones.end(),
                                    [](const AudioEndpoint& endpoint) { return endpoint.id == g_app.micInputId; });
    const size_t next = found == microphones.end() ? 0 : (found - microphones.begin() + 1) % microphones.size();
    if (microphones.empty() || microphones[next].id == g_app.micInputId) {
        if (popup) {
            g_app.osd->ShowMessage(kGlyphMicrophone, T(Text::NoOtherMicrophone), T(Text::MicInput));
        }
        return;
    }
    const AudioEndpoint& target = microphones[next];
    Log("microphone: %s", Utf8(target.name).c_str());
    g_app.settings.mic.input = target.id;
    Save();
    PushSettings();
    StartMic();
    if (popup) {
        g_app.osd->ShowMessage(kGlyphMicrophone, target.name, T(Text::MicInput));
    }
}

// After devices came or went: the most preferred device left when the output disconnected, and the
// most preferred device again when it reconnects. A device the user picks by hand stays otherwise.
void FollowOutputPriority() {
    const OutputSettings& settings = g_app.settings.outputs;
    const std::vector<AudioEndpoint> outputs = PreferredOutputs();
    std::vector<std::wstring> connected;
    for (const auto& endpoint : outputs) {
        connected.push_back(endpoint.id);
    }
    const std::wstring current = CurrentOutput();
    const std::optional<std::wstring> target = ChooseOutput(connected, g_app.knownOutputs, g_app.lastOutput,
                                                            settings.order, settings.switchOnLoss,
                                                            settings.returnToPreferred);
    g_app.knownOutputs = std::set<std::wstring>(connected.begin(), connected.end());
    // Excluded devices sort last, so the target is excluded only when no other device is connected.
    if (target && *target != current && !IsExcludedOutput(*target)) {
        SwitchOutput(outputs.front(), true);
        return;
    }
    g_app.lastOutput = current;
    PushOutputs();
}

void Host::OnOutputChoose(const std::wstring& id) {
    for (const auto& endpoint : PreferredOutputs()) {
        if (endpoint.id == id) {
            SwitchOutput(endpoint, false);
        }
    }
}

// Moves a device among the connected, not excluded ones; the full order keeps the others where they were.
void Host::OnOutputMove(size_t from, size_t to) {
    const std::vector<AudioEndpoint> outputs = AllowedOutputs();
    if (from >= outputs.size() || to >= outputs.size() || from == to) {
        return;
    }
    std::vector<std::wstring>& order = g_app.settings.outputs.order;
    for (const auto& endpoint : outputs) {
        if (std::find(order.begin(), order.end(), endpoint.id) == order.end()) {
            order.push_back(endpoint.id);
        }
    }
    // The connected devices keep the places they hold in the full order, filled in their new order.
    std::vector<std::wstring> listed;
    for (const auto& endpoint : outputs) {
        listed.push_back(endpoint.id);
    }
    MoveItem(listed, from, to);
    size_t next = 0;
    for (std::wstring& id : order) {
        if (std::find(listed.begin(), listed.end(), id) != listed.end()) {
            id = listed[next++];
        }
    }
    Save();
    PushSettings();
    PushOutputs();
}

void Host::OnOutputExclude(const std::wstring& id, bool excluded) {
    std::vector<std::wstring>& list = g_app.settings.outputs.excluded;
    std::erase(list, id);
    if (excluded) {
        list.push_back(id);
    }
    Save();
    PushSettings();
    PushOutputs();
}

// Warns once when the battery falls to the threshold, in the popup; charging or 5% above re-arms it.
void CheckBattery() {
    const BatteryAlertSettings& alert = g_app.settings.battery;
    const BatteryReading state = CurrentBattery();
    if (state.level < 0) {
        return;
    }
    // A battery that reports five steps (0, 25, 50…%) warns at its lowest step above empty.
    const DeviceModel* model = g_app.device.IsOpen() ? g_app.device.Model() : nullptr;
    const int step = model && model->protocol == DeviceProtocol::NovaStatus ? 100 / std::max<int>(model->statusBatteryMax, 1)
                                                                            : 1;
    const int threshold = std::max(alert.threshold, step);
    if (state.charging || state.level > threshold + 5) {
        g_app.batteryWarned = false;
        return;
    }
    if (!alert.enabled || g_app.batteryWarned || state.level > threshold) {
        return;
    }
    g_app.batteryWarned = true;
    Log("battery low: %d%%", state.level);
    const wchar_t glyph = static_cast<wchar_t>(kGlyphBattery0 + std::clamp(state.level / 10, 0, 10));
    g_app.osd->ShowMessage(glyph, std::wstring(T(Text::BatteryLowTitle)) + L" · " + std::to_wstring(state.level) + L"%",
                           DeviceName() + L" · " + T(Text::BatteryLowText), true);
}

void PushUpdate() {
    g_app.panel->SetUpdate(g_app.updateView);
}

void ShowTrayNotice(const std::wstring& title, const std::wstring& text, DWORD icon) {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = g_app.window;
    data.uID = kTrayId;
    data.uFlags = NIF_INFO;
    data.dwInfoFlags = icon | NIIF_RESPECT_QUIET_TIME;
    wcsncpy_s(data.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(data.szInfo, text.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

void StartUpdateCheck(bool manual) {
    using State = ui::UpdateView::State;
    if (g_app.updateView.state == State::Checking || g_app.updateView.state == State::Installing) {
        return;
    }
    g_app.updateView.state = State::Checking;
    PushUpdate();
    CheckForUpdate(g_app.window, WM_APP_UPDATE_CHECKED, manual);
}

// Automatic checks: at most once a day and only while they are turned on.
void MaybeCheckForUpdate() {
    if (g_app.settings.checkUpdates && _time64(nullptr) - g_app.settings.lastUpdateCheck >= kUpdateIntervalSeconds) {
        StartUpdateCheck(false);
    }
}

void OnUpdateChecked(std::unique_ptr<UpdateCheckResult> result) {
    using State = ui::UpdateView::State;
    if (result->ok) {
        g_app.settings.lastUpdateCheck = _time64(nullptr);
        ScheduleSave();
    }
    if (result->release) {
        g_app.update = result->release;
        g_app.updateView = {State::Available, result->release->version};
        if (!result->manual && !g_app.updateNotified) {
            g_app.updateNotified = true;
            g_app.updateBalloon = true;
            ShowTrayNotice(std::wstring(kAppName) + L" " + result->release->version, T(Text::UpdateBalloonText),
                           NIIF_INFO);
        }
    } else {
        g_app.updateView = {result->ok ? State::UpToDate : (result->manual ? State::CheckFailed : State::Idle), L""};
    }
    PushUpdate();
}

// Starts the executable again with flag (--after-update or --restart) and closes this instance; the new one waits
// until it has saved and released the headset. False when it did not start.
bool RestartApp(const wchar_t* flag) {
    std::wstring command = L"\"" + ExePath() + L"\" " + flag + L" " + std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
        Log("restart failed, error %lu", GetLastError());
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    PostMessageW(g_app.window, WM_CLOSE, 0, 0);
    return true;
}

// The new executable is in place: start it and close this instance, which it waits for.
void OnUpdateInstalled(bool ok) {
    using State = ui::UpdateView::State;
    if (ok) {
        if (RestartApp(L"--after-update")) {
            Log("update: restarting");
            return;
        }
    }
    g_app.updateView.state = State::Failed;
    PushUpdate();
}

void Host::OnCheckForUpdates() {
    StartUpdateCheck(true);
}

void Host::OnInstallUpdate() {
    if (!g_app.update || g_app.updateView.state == ui::UpdateView::State::Installing) {
        return;
    }
    g_app.updateView.state = ui::UpdateView::State::Installing;
    PushUpdate();
    InstallUpdate(g_app.window, WM_APP_UPDATE_INSTALLED, *g_app.update);
}

void Host::OnOpenReleasePage() {
    const std::wstring github = L"https://github.com/";
    const std::wstring page = g_app.update ? g_app.update->pageUrl : L"";
    // The page comes from GitHub's answer; only an address on GitHub without quotes goes to the browser.
    const bool trusted = page.starts_with(github) && page.find(L'"') == std::wstring::npos;
    OpenWithExplorer(trusted ? page : github + kUpdateRepository + L"/releases/latest");
}

ui::Meters Host::ReadMeters() {
    // As heard: after ChatMix and each side's volume. In virtual device mode the bridges know it;
    // otherwise the loudest app session of each group.
    ui::Meters meters;
    if (g_app.settings.routing.enabled && g_app.router.IsRunning()) {
        meters.game = g_app.router.TakeGameMeter();
        meters.chat = g_app.router.TakeChatMeter();
    } else {
        meters.game = g_app.mixer.GroupPeak(g_app.settings, AppGroup::Game, true);
        meters.chat = g_app.mixer.GroupPeak(g_app.settings, AppGroup::Chat, true);
    }
    meters.mic = g_app.mic.IsRunning() ? g_app.mic.TakePeak() : 0.0f;
    return meters;
}

void Host::OnQuit() {
    // Posted so the panel finishes its click handler before it is destroyed.
    PostMessageW(g_app.window, WM_CLOSE, 0, 0);
}

bool Host::OnSettingsExport(const std::wstring& path) {
    // The files get what is only in memory first; a running game's profile is left out, as on exit.
    Settings own = g_app.settings;
    const App::ProfileRestore& restore = g_app.profileRestore;
    own.levels = restore.levels.value_or(g_app.levels);
    for (auto [saved, state] : {std::pair{&restore.gameEq, &own.gameEq}, std::pair{&restore.chatEq, &own.chatEq},
                                std::pair{&restore.mediaEq, &own.mediaEq}}) {
        if (*saved) {
            *state = **saved;
        }
    }
    SaveSettings(g_app.iniPath, own);
    SaveHeadset();
    SaveMicPluginStates();
    const bool ok = ExportSettings(ConfigDirectory(), path);
    Log("settings %s to %s", ok ? "exported" : "not exported", Utf8(path).c_str());
    return ok;
}

// The archive waits beside the settings; the next instance puts it in place before it reads them, after
// this one has saved and closed.
bool Host::OnSettingsImport(const std::wstring& path) {
    if (!IsSettingsArchive(path)) {
        Log("settings import refused: %s", Utf8(path).c_str());
        return false;
    }
    const std::wstring pending = PendingImportPath(ConfigDirectory());
    if (!CopyFileW(path.c_str(), pending.c_str(), FALSE)) {
        return false;
    }
    if (!RestartApp(L"--restart")) {
        DeleteFileW(pending.c_str());
        return false;
    }
    Log("settings import from %s, restarting", Utf8(path).c_str());
    return true;
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == g_app.taskbarCreated && g_app.taskbarCreated != 0) {
        UpdateTray(NIM_ADD);
        return 0;
    }
    if (message == g_app.shellHook && g_app.shellHook != 0) {
        if (wParam == HSHELL_FLASH) {
            OnWindowFlash(reinterpret_cast<HWND>(lParam));
        }
        return 0;
    }

    switch (message) {
    case WM_CREATE: {
        g_app.window = window;
        DEV_BROADCAST_DEVICEINTERFACE_W filter{};
        filter.dbcc_size = sizeof(filter);
        filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
        HidD_GetHidGuid(&filter.dbcc_classguid);
        g_app.deviceNotify = RegisterDeviceNotificationW(window, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);
        WTSRegisterSessionNotification(window, NOTIFY_FOR_THIS_SESSION);
        return 0;
    }

    case WM_APP_TRAY:
        switch (LOWORD(lParam)) {
        case NIN_SELECT:
        case NIN_KEYSELECT:
            KillTimer(window, kTimerTrayCard);
            g_app.trayFlyout->HideCard();
            g_app.panel->Toggle();
            break;
        case NIN_POPUPOPEN:
            // The shell asks at once; the card waits the standard tooltip delay, as a tooltip would.
            SetTimer(window, kTimerTrayCard, GetDoubleClickTime(), nullptr);
            break;
        case NIN_POPUPCLOSE:
            KillTimer(window, kTimerTrayCard);
            g_app.trayFlyout->HideCard();
            break;
        case NIN_BALLOONUSERCLICK:
            if (g_app.updateBalloon) {
                g_app.updateBalloon = false;
                g_app.panel->Show(ui::Page::Settings);
            }
            break;
        case WM_CONTEXTMENU:
            KillTimer(window, kTimerTrayCard);
            ShowTrayMenu(GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam));
            break;
        }
        return 0;

    case WM_HOTKEY:
        switch (static_cast<int>(wParam) - kHotkeyIdBase) {
        case kHotkeyTowardGame:
            ApplyHotkey(LevelsFromBalance(StepBalance(BalanceFromLevels(g_app.levels), -1, BalanceGrid())));
            break;
        case kHotkeyTowardChat:
            ApplyHotkey(LevelsFromBalance(StepBalance(BalanceFromLevels(g_app.levels), 1, BalanceGrid())));
            break;
        case kHotkeyCenter:
            ApplyHotkey(Levels{});
            break;
        case kHotkeyMuteChat:
            ToggleChatMute();
            break;
        case kHotkeyMuteGame:
            ToggleGameMute();
            break;
        case kHotkeyMuteMedia:
            ToggleMediaMute();
            break;
        case kHotkeyNextOutput:
            NextOutputDevice();
            break;
        case kHotkeyNextMicrophone:
            NextMicrophone();
            break;
        case kHotkeyScreensaver:
            ToggleScreensaver();
            break;
        }
        return 0;

    case WM_APP_CHATMIX: {
        Levels levels;
        if (g_app.device.TakeLevels(levels) && g_app.device.IsOpen()) {
            // State queries report the levels too; only a turned wheel changes them.
            const bool turned = levels != g_app.levels;
            SetLevels(levels, true);
            ScheduleHeadsetVerify(kVerifyAfterDialMs);
            // Headsets without a display show nothing when their wheel turns.
            if (turned && g_app.settings.dialPopup && g_app.features && !g_app.features->oled) {
                g_app.osd->Show(g_app.levels);
            }
        }
        return 0;
    }

    case WM_APP_DIAL_MODE:
        UpdateStatus();
        return 0;

    case WM_APP_HEADSET:
        if (g_app.features) {
            for (const auto& update : g_app.device.TakeHeadsetUpdates()) {
                if (update.kind == HeadsetUpdate::Kind::ChatMix) {
                    VerifyDeviceLevels(update.value, update.chat);
                } else {
                    const BluetoothState bluetooth = g_app.headset.bluetooth;
                    ApplyUpdate(*g_app.features, g_app.headset, update);
                    if (g_app.headset.bluetooth != bluetooth) {
                        Log("Bluetooth %s (mode %d, link %d)", BluetoothName(g_app.headset.bluetooth), update.value,
                            update.chat);
                        UpdateIdleBadge();
                    }
                }
            }
            PushHeadset();
            UpdateStatus();
            ScheduleHeadsetSave();
            CheckBattery();
        }
        return 0;

    case WM_APP_ROUTING:
        Log("routing stopped");
        ApplyLevels();
        UpdateRouting();
        SetTimer(window, kTimerRoutingRetry, kRoutingRetryMs, nullptr);
        return 0;

    case WM_APP_DEVICE_LOST:
        HandleDeviceLost();
        return 0;

    case WM_APP_UPDATE_CHECKED:
        OnUpdateChecked(std::unique_ptr<UpdateCheckResult>(reinterpret_cast<UpdateCheckResult*>(lParam)));
        return 0;

    case WM_APP_UPDATE_INSTALLED:
        OnUpdateInstalled(wParam != 0);
        return 0;

    case WM_APP_MIC_STOPPED:
        Log("mic chain stopped");
        PushMic();
        SetTimer(window, kTimerMicRetry, kMicRetryMs, nullptr);
        return 0;

    case WM_APP_MIC_LEVEL:
        g_app.micLevels.Enforce();
        return 0;

    case WM_APP_WHEEL:
        OnChatMixWheel(static_cast<int>(lParam));
        return 0;

    case WM_APP_DEVICE_VOLUME:
        g_app.router.UpdateDeviceVolumes();
        return 0;

    case WM_APP_SESSIONS_CHANGED:
        g_app.mixer.ReleaseExpired();
        ApplyLevels();
        RefreshApps();
        SyncAppRouting();
        return 0;

    case WM_APP_ENDPOINTS_CHANGED:
        SetTimer(window, kTimerRebindAudio, kRebindDelayMs, nullptr);
        return 0;

    case WM_APP_SHOW_PANEL:
        g_app.panel->Show();
        return 0;

    case WM_APP_GG_EVENT:
        CheckIdleScreen();
        return 0;

    case WM_APP_GAMESENSE: {
        const ULONGLONG now = GetTickCount64();
        for (const GameSenseRequest& request : g_app.addOns.TakeRequests()) {
            g_app.addOnScreens.Handle(request, g_app.settings.addOns, now);
        }
        UpdateAddOnScreen();
        return 0;
    }

    case WM_APP_GG_HEARTBEAT:
        g_app.settings.ggHeartbeats = g_app.ggWatch.Heartbeats();
        Save();
        return 0;

    case WM_TIMER:
        if (wParam == kTimerGgWatch) {
            WatchGg();
            return 0;
        }
        if (wParam == kTimerAddOnScreen) {
            UpdateAddOnScreen();
            return 0;
        }
        if (wParam == kTimerIdleScreen) {
            CheckIdleScreen();
            return 0;
        }
        if (wParam == kTimerTrayMotion) {
            TickTray();
            return 0;
        }
        if (wParam == kTimerDucking) {
            TickDucking();
            return 0;
        }
        if (wParam == kTimerOutputBattery) {
            RefreshOutputBattery();
            return 0;
        }
        KillTimer(window, wParam);
        if (wParam == kTimerTrayCard) {
            ShowTrayCard();
        } else if (wParam == kTimerReconnect) {
            ConnectDevice();
        } else if (wParam == kTimerRebindAudio) {
            g_app.mixer.Rebind();
            ApplyLevels();
            RefreshApps();
            if (g_app.settings.routing.enabled && !g_app.router.IsRunning()) {
                StartRouting();
            }
            FollowOutputPriority();
            RefreshOutputName();
            UpdateStatus();
            // A cable or microphone that came back, or a new default microphone.
            if (g_app.settings.mic.enabled &&
                (!g_app.mic.IsRunning() ||
                 (g_app.settings.mic.input.empty() && DefaultCaptureEndpoint(eCommunications) != g_app.micInputId) ||
                 (g_app.settings.mic.monitor && CurrentOutput() != g_app.micMonitorId))) {
                StartMic();
            }
        } else if (wParam == kTimerSaveHeadset) {
            SaveHeadset();
        } else if (wParam == kTimerCommitHeadset) {
            CommitHeadset();
        } else if (wParam == kTimerVerifyHeadset) {
            g_app.device.QueryState();
        } else if (wParam == kTimerSaveSettings) {
            Save();
        } else if (wParam == kTimerSendLevels) {
            SendDeviceLevels();
        } else if (wParam == kTimerUpdateCheck) {
            SetTimer(window, kTimerUpdateCheck, kUpdateTimerMs, nullptr);
            MaybeCheckForUpdate();
        } else if (wParam == kTimerMicRetry) {
            if (g_app.settings.mic.enabled && !g_app.mic.IsRunning()) {
                StartMic();
            }
        } else if (wParam == kTimerSavePlugins) {
            SetTimer(window, kTimerSavePlugins, kSavePluginsMs, nullptr);
            SaveMicPluginStates();
        } else if (wParam == kTimerRoutingRetry) {
            if (g_app.settings.routing.enabled && !g_app.router.IsRunning()) {
                StartRouting();
            }
        }
        return 0;

    case WM_DEVICECHANGE:
        if (!g_app.device.IsOpen() && IsSupportedArrival(wParam, lParam)) {
            SetTimer(window, kTimerReconnect, kReconnectDelayMs, nullptr);
        }
        return TRUE;

    case WM_POWERBROADCAST:
        if (wParam == PBT_APMSUSPEND) {
            // A frozen frame must not stay on the display while the PC sleeps.
            StopIdleScreen();
        } else if (wParam == PBT_APMRESUMEAUTOMATIC) {
            Log("resume from sleep");
            StopIdleScreen();
            g_app.device.Close();
            UpdateStatus();
            SetTimer(window, kTimerReconnect, kResumeDelayMs, nullptr);
        }
        return TRUE;

    case WM_WTSSESSION_CHANGE:
        if (wParam == WTS_SESSION_UNLOCK) {
            ReinitializeDevice("session unlocked");
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDM_OPEN:
            g_app.panel->Show(ui::Page::Mixer);
            break;
        case IDM_SETTINGS:
            g_app.panel->Show(ui::Page::Settings);
            break;
        case IDM_RECONNECT:
            ReconnectDevice();
            break;
        case IDM_AUTOSTART:
            SetAutostart(!IsAutostartEnabled());
            PushSettings();
            break;
        case IDM_EXIT:
            DestroyWindow(window);
            break;
        }
        return 0;

    case WM_SETTINGCHANGE:
        // Light or dark mode and the accent color change the theme and the tray icon.
        if (lParam && CompareStringOrdinal(reinterpret_cast<LPCWSTR>(lParam), -1, L"ImmersiveColorSet", -1, TRUE) ==
                          CSTR_EQUAL) {
            ApplyAppearance();
        }
        break;

    case WM_DWMCOLORIZATIONCOLORCHANGED:
        if (g_app.settings.appearance.windowsAccent) {
            ApplyAppearance();
        }
        break;

    case WM_ENDSESSION:
        if (wParam) {
            Cleanup();
            // Restart Manager (installers, updates) expects the app to exit, not just to save state.
            if (lParam & ENDSESSION_CLOSEAPP) {
                PostQuitMessage(0);
            }
        }
        return 0;

    case WM_DESTROY:
        DeregisterShellHookWindow(window);
        Cleanup();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

}  // namespace

int WINAPI wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int) {
    // After an update or a settings import the new instance waits until the old one has released the headset
    // and saved its settings.
    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        for (int i = 1; i + 1 < argc; ++i) {
            if (wcscmp(argv[i], L"--after-update") == 0 || wcscmp(argv[i], L"--restart") == 0) {
                if (HANDLE previous = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(_wtoi(argv[i + 1])))) {
                    WaitForSingleObject(previous, kUpdateWaitMs);
                    CloseHandle(previous);
                }
            }
        }
        LocalFree(argv);
    }
    CleanUpAfterUpdate();

    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\OpenChatMix.Instance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // A second launch opens the panel of the running instance.
        if (HWND running = FindWindowW(kMainWindowClass, nullptr)) {
            DWORD pid = 0;
            GetWindowThreadProcessId(running, &pid);
            AllowSetForegroundWindow(pid);
            PostMessageW(running, WM_APP_SHOW_PANEL, 0, 0);
        }
        return 0;
    }
    CloseLegacyInstance();

    // SessionMixer requires MTA; the UI uses no OLE features, and the file dialog runs on its own STA thread.
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
        return 1;
    }
    g_app.appRouting.Init();

    g_app.instance = instance;
    const std::wstring configDirectory = ConfigDirectory();
    // Imported settings take their place before anything reads them.
    const std::wstring pendingImport = PendingImportPath(configDirectory);
    const bool importPending = GetFileAttributesW(pendingImport.c_str()) != INVALID_FILE_ATTRIBUTES;
    const bool imported = importPending && ImportSettings(configDirectory, pendingImport);
    if (importPending) {
        DeleteFileW(pendingImport.c_str());
    }
    g_app.iniPath = configDirectory + L"\\OpenChatMix.ini";
    const std::wstring legacyIni = ExeDirectory() + L"\\NovaChatMix.ini";
    const bool migrate = GetFileAttributesW(g_app.iniPath.c_str()) == INVALID_FILE_ATTRIBUTES &&
                         GetFileAttributesW(legacyIni.c_str()) != INVALID_FILE_ATTRIBUTES;
    g_app.settings = LoadSettings(migrate ? legacyIni : g_app.iniPath);
    SetLanguage(g_app.settings.language);
    if (g_app.settings.log) {
        LogOpen(configDirectory + L"\\OpenChatMix.log");
    }
    Log("start: %s", Utf8(ExePath()).c_str());
    if (importPending) {
        Log("settings import %s", imported ? "applied" : "failed");
    }
    if (migrate) {
        Log("settings migrated from %s", Utf8(legacyIni).c_str());
    }

    const bool legacyAutostart = RemoveLegacyAutostart();
    if (g_app.settings.firstRun || legacyAutostart || IsAutostartEnabled()) {
        // Also keeps the Run entry pointing at the current location if the executable was moved.
        SetAutostart(true);
    }
    Save();
    g_app.levels = g_app.settings.levels;
    ApplyAppearance();

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP));
    windowClass.lpszClassName = kMainWindowClass;
    RegisterClassExW(&windowClass);

    g_app.taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    g_app.shellHook = RegisterWindowMessageW(L"SHELLHOOK");
    // Hidden top-level window: message-only windows miss WM_POWERBROADCAST and TaskbarCreated.
    if (!CreateWindowExW(0, kMainWindowClass, kAppName, WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, instance,
                         nullptr)) {
        CoUninitialize();
        return 1;
    }

    RegisterShellHookWindow(g_app.window);
    if (g_app.settings.log) {
        g_app.hangWatch.Start(g_app.window, configDirectory);
    }
    g_app.panel = std::make_unique<ui::Panel>(instance, g_app.host);
    g_app.osd = std::make_unique<ui::Osd>(instance);
    g_app.panel->SetTrayAnchor(g_app.window, kTrayId);
    g_app.panel->SetLevels(g_app.levels);
    g_app.osd->SetReduceMotion(g_app.settings.appearance.reduceMotion);
    g_app.trayFlyout = std::make_unique<ui::TrayFlyout>(instance, g_app.window);
    g_app.trayFlyout->SetReduceMotion(g_app.settings.appearance.reduceMotion);
    PushSettings();
    PushIdleScreen();
    UpdateTray(NIM_ADD);
    RegisterHotkeys();
    UpdateMouseHook();
    UpdateDucking();
    const ProcessSet processes = RunningProcesses(&g_app.ggHelpers);
    g_app.ggRunning = IsGgRunning(processes);
    if (!g_app.ggRunning) {
        g_app.ggHelpers.clear();
    }
    g_app.ggScreen = FindGgScreenApps(g_app.ggHelpers);
    if (g_app.ggScreen.Any()) {
        Log("SteelSeries GG apps on the display: %s", Utf8(g_app.ggScreen.names).c_str());
    }
    PushIdleScreen();
    g_app.ggWatch.Init(g_app.window, WM_APP_GG_EVENT, WM_APP_GG_HEARTBEAT, &OnGgEventNow);
    g_app.ggWatch.SetHeartbeats(g_app.settings.ggHeartbeats);
    SetTimer(g_app.window, kTimerGgWatch, kGgWatchMs, nullptr);
    g_app.mixer.Init(g_app.window);
    ConnectDevice();
    // After ConnectDevice, which finds the headset that becomes the default output again.
    DropSonarDevices();
    if (g_app.settings.routing.enabled && !g_app.router.IsRunning()) {
        StartRouting();
    }
    UpdateRouting();
    FollowOutputPriority();
    StartMic();
    SyncAppRouting();
    CheckProfiles(processes);
    PushUpdate();
    SetTimer(g_app.window, kTimerUpdateCheck, kFirstUpdateCheckMs, nullptr);
    // The first run opens the panel on the tour, so the app does not just vanish into the tray.
    if (g_app.settings.firstRun) {
        g_app.panel->Show(ui::Page::Welcome);
    }

    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    LogClose();
    CoUninitialize();
    if (mutex) {
        CloseHandle(mutex);
    }
    // Plugin modules stay loaded until the end, and some crash in their own unload code: with everything saved
    // and released, the process ends without running it.
    if (PluginModulesLoaded()) {
        TerminateProcess(GetCurrentProcess(), static_cast<UINT>(message.wParam));
    }
    return static_cast<int>(message.wParam);
}
