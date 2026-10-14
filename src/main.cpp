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
#include "headset.h"
#include "i18n.h"
#include "icon.h"
#include "idle_animations.h"
#include "log.h"
#include "oled.h"
#include "parametric_eq.h"
#include "resource.h"
#include "session_mixer.h"
#include "settings.h"
#include "ui/osd.h"
#include "ui/panel.h"
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
constexpr wchar_t kGlyphEqualizer = 0xE9E9;
constexpr UINT kSaveHeadsetDelayMs = 1000;
constexpr UINT kCommitHeadsetDelayMs = 600;  // Device save commands write flash; wait until edits settle.
constexpr UINT kRoutingRetryMs = 3000;
constexpr UINT kReconnectDelayMs = 1500;  // Lets all interfaces of a freshly plugged device enumerate.
constexpr UINT kResumeDelayMs = 3000;     // Headsets re-enumerate after sleep.
constexpr UINT kRebindDelayMs = 500;      // Coalesces bursts of endpoint notifications.
constexpr UINT kGgWatchMs = 5000;
constexpr UINT kIdleCheckMs = 250;
constexpr ULONGLONG kIdlePreviewMs = 8000;  // A newly chosen animation shows right away for this long.
constexpr float kSilencePeak = 0.0001f;     // About -80 dBFS.
// The headset is asked for its settings again after changes, so the panel shows what it really applied.
constexpr UINT kVerifyAfterCommandMs = 500;
constexpr UINT kVerifyAfterDialMs = 1500;
constexpr UINT kVerifyAfterBalanceMs = 600;
// The GameDAC can miss balance commands that follow each other closely, e.g. while the slider is dragged.
constexpr ULONGLONG kLevelsSendGapMs = 40;
constexpr DWORD kLegacyExitTimeoutMs = 3000;

// SteelSeries GG takes over the headset while any of these run.
constexpr const wchar_t* kGgProcesses[] = {L"SteelSeriesGG.exe", L"SteelSeriesEngine.exe", L"SteelSeriesSonar.exe"};

class Host final : public ui::PanelHost {
public:
    void OnBalanceChanged(int steps) override;
    void OnGroupChanged(const ui::AppItem& app, AppGroup group) override;
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
    void OnSettingsEdit(const std::function<void(Settings&)>& edit) override;
    std::vector<AudioEndpoint> PlaybackDevices() override;
    void OnCheckForUpdates() override;
    void OnInstallUpdate() override;
    void OnOpenReleasePage() override;
    void OnQuit() override;
};

struct App {
    HINSTANCE instance = nullptr;
    HWND window = nullptr;
    UINT taskbarCreated = 0;
    HDEVNOTIFY deviceNotify = nullptr;
    HICON trayIcon = nullptr;
    std::wstring iniPath;
    Settings settings;
    ChatMixDevice device;
    SessionMixer mixer;
    Levels levels;
    std::array<bool, kHotkeyCount> hotkeyFailed{};
    bool hotkeysSuspended = false;
    bool ggRunning = false;
    bool ggDismissed = false;  // The user hid the warning for the current GG session.
    bool ggNotified = false;   // The tray notification was shown for the current GG session.
    const HeadsetFeatures* features = nullptr;
    HeadsetState headset;
    AudioRouter router;
    bool routingDevicesAcquired = false;  // The Game and Chat devices were switched on for the running mode.
    bool routingDeviceError = false;
    OledDisplay display;
    std::shared_ptr<IdleAnimation> idleAnimation;
    ULONGLONG lastSoundTick = 0;
    ULONGLONG lastActivityTick = 0;  // Changes made in OpenChatMix that the headset display shows.
    ULONGLONG idlePreviewStart = 0;
    ULONGLONG idlePreviewUntil = 0;
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
    float duck = 0.0f;  // Share of the ducking amount applied to the Game side, 0-1.
    ULONGLONG chatActiveTick = 0;
    bool batteryWarned = false;
    std::wstring activeProfile;  // Executable of the game profile in effect.
    // What a game profile replaced, put back when the game exits.
    struct ProfileRestore {
        std::optional<Levels> levels;
        std::optional<EqChannelSettings> gameEq;
        int headsetEq = -1;
    } profileRestore;
    Host host;
    std::unique_ptr<ui::Panel> panel;
    std::unique_ptr<ui::Osd> osd;
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

// Theme colors, or the taskbar's own text color for the monochrome icon.
LevelIconColors TrayColors() {
    const bool light = TaskbarIsLight();
    if (g_app.settings.appearance.tray == TrayStyle::Monochrome) {
        return light ? LevelIconColors{0x1F1F1F, 0x1F1F1F} : LevelIconColors{0xFFFFFF, 0xFFFFFF};
    }
    const ui::Palette palette = ui::MakePalette(AccentSeed(), !light);
    return {PackRgb(palette.primary), PackRgb(palette.tertiary)};
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
    return g_app.device.IsOpen() ? g_app.device.Model()->name : T(Text::NoHeadset);
}

std::wstring LevelsText() {
    return std::wstring(T(Text::Game)) + L" " + std::to_wstring(g_app.levels.game) + L"% · " + T(Text::Chat) + L" " +
           std::to_wstring(g_app.levels.chat) + L"%";
}

using ProcessSet = std::set<std::wstring, NoCaseLess>;

ProcessSet RunningProcesses() {
    ProcessSet processes;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return processes;
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry)) {
        processes.insert(entry.szExeFile);
    }
    CloseHandle(snapshot);
    return processes;
}

bool IsGgRunning(const ProcessSet& processes) {
    return std::any_of(std::begin(kGgProcesses), std::end(kGgProcesses),
                       [&](const wchar_t* name) { return processes.count(name) != 0; });
}

void UpdateTray(DWORD action) {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = g_app.window;
    data.uID = kTrayId;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    data.uCallbackMessage = WM_APP_TRAY;
    HICON icon = CreateLevelIcon(GetSystemMetrics(SM_CXSMICON), g_app.levels.game, g_app.chatMuted ? 0 : g_app.levels.chat,
                                 TrayColors());
    data.hIcon = icon;
    const std::wstring tip = std::wstring(kAppName) + L"\n" + DeviceName() + L"\n" + LevelsText();
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
    const std::wstring device = g_app.device.IsOpen() ? DeviceName() : T(Text::NoHeadsetStatus);
    const bool warn = g_app.ggRunning && g_app.device.IsOpen() && g_app.settings.warnGg && !g_app.ggDismissed;
    g_app.panel->SetStatus(device, detail, g_app.device.IsOpen(), warn ? T(Text::GgRunning) : L"");
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
    const bool visible = g_app.device.IsOpen() && g_app.features && g_app.features->HasSettings();
    g_app.panel->SetHeadset(g_app.device.IsOpen() ? g_app.device.Model()->name : L"", visible ? g_app.features : nullptr,
                            g_app.headset);
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
    const std::vector<uint8_t> command = EqualizerBandsCommand(g_app.features->equalizer, curve);
    SendHeadsetCommand(command);
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

bool IsRoutingDevice(const std::wstring& id) {
    const RoutingSettings& routing = g_app.settings.routing;
    return !id.empty() && (id == routing.game || id == routing.chat);
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
        ok = SetEndpointEnabled(routing.game, false) && ok;
        ok = SetEndpointEnabled(routing.chat, false) && ok;
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
    view.ids[0] = routing.game;
    view.ids[1] = routing.chat;
    view.ids[2] = routing.output;
    view.names[0] = nameOf(routing.game);
    view.names[1] = nameOf(routing.chat);
    view.manageDevices = routing.manageDevices;
    view.cablesMissing = (routing.game.empty() || routing.chat.empty()) && !FindVirtualDevices(endpoints);
    const std::wstring outputId = RoutingOutputId(ListRenderEndpoints());
    view.names[2] = routing.output.empty() ? std::wstring(T(Text::AutomaticHeadset)) +
                                                 (outputId.empty() ? L"" : L" · " + nameOf(outputId))
                                           : nameOf(routing.output);
    if (routing.enabled) {
        if (g_app.router.IsRunning()) {
            view.status = T(Text::RoutingRunning);
            if (const int latency = g_app.router.LatencyMs(); latency > 0) {
                view.status += L" · ~" + std::to_wstring(latency) + L" " + T(Text::MillisecondsShort);
            }
        } else {
            view.error = true;
            if (routing.game.empty() || routing.chat.empty() || outputId.empty()) {
                view.status = T(Text::RoutingNeedsDevices);
            } else if (routing.game == routing.chat || routing.game == outputId || routing.chat == outputId) {
                view.status = T(Text::RoutingSameDevice);
            } else {
                view.status = T(Text::RoutingDeviceMissing);
            }
        }
        if (g_app.routingDeviceError) {
            view.status += std::wstring(L" · ") + T(Text::RoutingManageFailed);
            view.error = true;
        }
    }
    g_app.panel->SetRouting(view);
}

Levels EffectiveLevels();
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
        UpdateMixerEndpoints();
        const std::wstring outputId = RoutingOutputId(ListRenderEndpoints());
        const bool distinct = routing.game != routing.chat && routing.game != outputId && routing.chat != outputId;
        if (!routing.game.empty() && !routing.chat.empty() && !outputId.empty() && distinct &&
            g_app.router.Start(routing.game, routing.chat, outputId, g_app.window)) {
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
}

void PushSettings() {
    g_app.panel->SetAutostart(IsAutostartEnabled());
    g_app.panel->SetSettings(g_app.settings);
}

void ApplyAppearance() {
    const AppearanceSettings& appearance = g_app.settings.appearance;
    ui::SetTheme(appearance.theme, AccentSeed());
    if (g_app.osd) {
        g_app.osd->SetReduceMotion(appearance.reduceMotion);
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
    g_app.router.SetEqualizers(equalizer(g_app.settings.gameEq), equalizer(g_app.settings.chatEq));
}

// Apps on the Game and Chat devices are listed (and routed) in virtual device mode.
void UpdateMixerEndpoints() {
    const RoutingSettings& routing = g_app.settings.routing;
    std::vector<std::wstring> extra;
    if (routing.enabled) {
        for (const std::wstring* id : {&routing.game, &routing.chat}) {
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

// Chat apps play on the Chat device; the others follow the default device, which is the Game device while
// devices are managed, or get the Game device explicitly otherwise.
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
        const bool chat = g_app.settings.EffectiveGroupOf(app.exe) == AppGroup::Chat;
        const std::wstring target = chat ? routing.chat : (routing.manageDevices ? L"" : routing.game);
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
            Log("routed %s to %s", Utf8(app.exe).c_str(), target.empty() ? "the default device" : chat ? "Chat" : "Game");
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
        if (!listed) {
            items.push_back({exe, rule.path, DisplayName(rule.path, exe), rule.group, false});
        }
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
    if (g_app.chatMuted) {
        levels.chat = 0;
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
    if (g_app.settings.routing.enabled && g_app.router.IsRunning()) {
        g_app.router.SetLevels(levels);
        return;
    }
    g_app.mixer.Apply(levels, g_app.settings);
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
    g_app.panel->SetIdleScreen(view);
}

void StopIdleScreen() {
    KillTimer(g_app.window, kTimerIdleScreen);
    g_app.display.Close();
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
    if (sound || deviceEvent >= g_app.idlePreviewStart || g_app.lastActivityTick > g_app.idlePreviewStart) {
        g_app.idlePreviewUntil = 0;
    }
    const ULONGLONG activity = std::max({g_app.lastSoundTick, g_app.lastActivityTick, deviceEvent});
    const ULONGLONG delay = static_cast<ULONGLONG>(g_app.settings.idleScreen.delaySeconds) * 1000;
    // SteelSeries GG draws on the same display.
    const bool idle = !g_app.ggRunning && (now < g_app.idlePreviewUntil || now - activity >= delay);
    if (idle != g_app.display.IsPlaying()) {
        if (idle) {
            g_app.display.Play(g_app.idleAnimation);
        } else {
            g_app.display.Pause();
        }
        Log("screensaver %s", idle ? "on" : "off");
    }
    if (idle != g_app.idleShowing) {
        g_app.idleShowing = idle;
        PushIdleScreen();
    }
}

// (Re)starts the screensaver with the current settings; preview shows it right away.
void StartIdleScreen(bool preview) {
    StopIdleScreen();
    const IdleScreenSettings& idle = g_app.settings.idleScreen;
    const std::optional<DisplayCollection> collection = g_app.device.IsOpen() ? g_app.device.Display() : std::nullopt;
    if (!collection || idle.animation == IdleAnimationKind::Off) {
        return;
    }
    g_app.idleAnimation = CreateIdleAnimation(idle.animation, idle.image);
    if (!g_app.idleAnimation || !g_app.display.Open(*collection)) {
        g_app.idleAnimation.reset();
        return;
    }
    const ULONGLONG now = GetTickCount64();
    g_app.lastActivityTick = now;
    g_app.idlePreviewStart = now;
    g_app.idlePreviewUntil = preview ? now + kIdlePreviewMs : 0;
    SetTimer(g_app.window, kTimerIdleScreen, kIdleCheckMs, nullptr);
    CheckIdleScreen();
}

void ConnectDevice() {
    if (g_app.device.IsOpen()) {
        return;
    }
    if (g_app.device.Open(g_app.window)) {
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
    } else {
        Log("no supported headset");
        g_app.mixer.SetTarget(std::nullopt);
    }
    g_app.panel->SetBalanceDial(HasBalanceDial());
    ApplyLevels();
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
    g_app.features = nullptr;
    g_app.mixer.SetTarget(std::nullopt);
    g_app.panel->SetBalanceDial(false);
    ApplyLevels();
    UpdateStatus();
    PushHeadset();
    RefreshApps();
    if (g_app.settings.routing.enabled) {
        UpdateRouting();
    }
    // One retry covers transient read errors; real unplug/replug is handled by WM_DEVICECHANGE.
    SetTimer(g_app.window, kTimerReconnect, kReconnectDelayMs, nullptr);
}

std::optional<NamedEq> FindGameEqPreset(const std::wstring& name) {
    for (const auto& list : {BuiltinEqPresets(EqChannel::Game), SonarEqPresets(EqChannel::Game), LoadParametricPresets()}) {
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
    if (restore.gameEq) {
        g_app.settings.gameEq = *restore.gameEq;
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
    const auto preset = profile.gameEq.empty() ? std::nullopt : FindGameEqPreset(profile.gameEq);
    if (preset) {
        if (!restore.gameEq) {
            restore.gameEq = g_app.settings.gameEq;
        }
        EqChannelSettings& state = g_app.settings.gameEq;
        state.enabled = true;
        state.preset = preset->name;
        AssignBands(state.eq, preset->eq);
        ScheduleSave();
        PushEqualizers();
    } else if (restore.gameEq) {
        g_app.settings.gameEq = *restore.gameEq;
        restore.gameEq.reset();
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
    const ProcessSet processes = RunningProcesses();
    CheckProfiles(processes);
    const bool running = IsGgRunning(processes);
    if (running == g_app.ggRunning) {
        return;
    }
    g_app.ggRunning = running;
    Log("SteelSeries GG %s", running ? "started" : "stopped");
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
    HMENU menu = CreatePopupMenu();
    const std::wstring status = DeviceName() + L" · " + LevelsText();
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, status.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_OPEN, T(Text::TrayOpen));
    AppendMenuW(menu, MF_STRING, IDM_SETTINGS, T(Text::TraySettings));
    AppendMenuW(menu, MF_STRING, IDM_RECONNECT, T(Text::TrayReconnect));
    AppendMenuW(menu, MF_STRING | (IsAutostartEnabled() ? MF_CHECKED : MF_UNCHECKED), IDM_AUTOSTART,
                T(Text::StartWithWindows));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_EXIT, T(Text::TrayExit));
    SetMenuDefaultItem(menu, IDM_OPEN, FALSE);

    // Required so the menu closes when the user clicks elsewhere.
    SetForegroundWindow(g_app.window);
    const UINT align = GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN;
    TrackPopupMenuEx(menu, align | TPM_BOTTOMALIGN | TPM_RIGHTBUTTON, x, y, g_app.window, nullptr);
    PostMessageW(g_app.window, WM_NULL, 0, 0);
    DestroyMenu(menu);
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

    // Settings that a game profile replaced are not kept.
    if (!g_app.activeProfile.empty()) {
        RestoreProfile();
    }
    g_app.settings.levels = g_app.levels;
    UnrouteApps();
    Save();
    SaveHeadset();

    StopIdleScreen();
    g_app.panel.reset();
    g_app.osd.reset();
    g_app.router.Stop();
    ReleaseRoutingDevices(true);
    // Windows persists per-app volume, so every app changed by ChatMix goes back to full level.
    g_app.device.Stop();
    g_app.mixer.ResetVolumes();
    g_app.device.Close();
    g_app.mixer.Shutdown();

    for (int action = 0; action < kHotkeyCount; ++action) {
        UnregisterHotKey(g_app.window, kHotkeyIdBase + action);
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
    const uint8_t payload[] = {command, static_cast<uint8_t>(value)};
    SendHeadsetCommand(payload);
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
        PushSettings();
    }
}

void Host::OnRoutingDevice(ui::RoutingSlot slot, const std::wstring& id) {
    RoutingSettings& routing = g_app.settings.routing;
    if (slot != ui::RoutingSlot::Output) {
        // The previous Game or Chat device is handed back before the new one is taken over.
        g_app.router.Stop();
        ReleaseRoutingDevices(true);
    }
    switch (slot) {
    case ui::RoutingSlot::Game:
        routing.game = id;
        break;
    case ui::RoutingSlot::Chat:
        routing.chat = id;
        break;
    case ui::RoutingSlot::Output:
        routing.output = id;
        break;
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
    StartIdleScreen(true);
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
    StartIdleScreen(true);
    PushIdleScreen();
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
        ApplyLevels();
        RefreshApps();
    }
    if (before.routing.autoRoute != after.routing.autoRoute) {
        SyncAppRouting();
    }
    if (!(before.gameEq == after.gameEq) || !(before.chatEq == after.chatEq) || before.equalizer != after.equalizer) {
        PushEqualizers();
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

// Next Game equalizer preset in virtual device mode, otherwise the headset's next preset.
void NextEqPreset() {
    if (g_app.settings.routing.enabled && g_app.settings.equalizer) {
        std::vector<NamedEq> presets = BuiltinEqPresets(EqChannel::Game);
        for (auto&& list : {SonarEqPresets(EqChannel::Game), LoadParametricPresets()}) {
            presets.insert(presets.end(), list.begin(), list.end());
        }
        EqChannelSettings& state = g_app.settings.gameEq;
        const auto current = std::find_if(presets.begin(), presets.end(), [&](const NamedEq& preset) {
            return _wcsicmp(preset.name.c_str(), state.preset.c_str()) == 0;
        });
        const size_t next = current == presets.end() ? 0 : (current - presets.begin() + 1) % presets.size();
        state.enabled = true;
        state.preset = presets[next].name;
        AssignBands(state.eq, presets[next].eq);
        ScheduleSave();
        PushEqualizers();
        PushSettings();
        if (g_app.settings.showPopup) {
            g_app.osd->ShowMessage(kGlyphEqualizer, presets[next].name,
                                   std::wstring(T(Text::Game)) + L" · " + T(Text::Equalizers));
        }
        return;
    }
    if (!g_app.features || !g_app.features->equalizer.bandsCommand || !g_app.device.IsOpen()) {
        return;
    }
    const EqualizerSpec& eq = g_app.features->equalizer;
    const int next = (std::max(g_app.headset.eqPreset, -1) + 1) % (eq.CustomIndex() + 1);
    g_app.host.OnEqualizerPreset(next);
    if (g_app.settings.showPopup) {
        g_app.osd->ShowMessage(kGlyphEqualizer, next == eq.CustomIndex() ? T(Text::EqCustom) : eq.presets[next].name,
                               DeviceName());
    }
}

void CheckBattery() {
    const BatteryAlertSettings& alert = g_app.settings.battery;
    const HeadsetState& state = g_app.headset;
    if (state.battery < 0 || state.headsetOff) {
        return;
    }
    if (state.charging || state.battery > alert.threshold + 5) {
        g_app.batteryWarned = false;
        return;
    }
    if (!alert.enabled || g_app.batteryWarned || state.battery > alert.threshold) {
        return;
    }
    g_app.batteryWarned = true;
    g_app.updateBalloon = false;
    ShowTrayNotice(T(Text::BatteryLowTitle),
                   std::wstring(T(Text::BatteryLowText)) + L" " + std::to_wstring(state.battery) + L"%", NIIF_WARNING);
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

// The new executable is in place: start it and close this instance, which it waits for.
void OnUpdateInstalled(bool ok) {
    using State = ui::UpdateView::State;
    if (ok) {
        std::wstring command = L"\"" + ExePath() + L"\" --after-update " + std::to_wstring(GetCurrentProcessId());
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            Log("update: restarting");
            PostMessageW(g_app.window, WM_CLOSE, 0, 0);
            return;
        }
        Log("update: new version did not start, error %lu", GetLastError());
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
    const std::wstring url = g_app.update && !g_app.update->pageUrl.empty()
                                 ? g_app.update->pageUrl
                                 : std::wstring(L"https://github.com/") + kUpdateRepository + L"/releases/latest";
    // explorer.exe hands the address to the default browser and needs no COM apartment.
    std::wstring command = L"explorer.exe \"" + url + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
}

void Host::OnQuit() {
    // Posted so the panel finishes its click handler before it is destroyed.
    PostMessageW(g_app.window, WM_CLOSE, 0, 0);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == g_app.taskbarCreated && g_app.taskbarCreated != 0) {
        UpdateTray(NIM_ADD);
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
            g_app.panel->Toggle();
            break;
        case NIN_BALLOONUSERCLICK:
            if (g_app.updateBalloon) {
                g_app.updateBalloon = false;
                g_app.panel->Show(ui::Page::Settings);
            }
            break;
        case WM_CONTEXTMENU:
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
        case kHotkeyNextPreset:
            NextEqPreset();
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
                    ApplyUpdate(*g_app.features, g_app.headset, update);
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

    case WM_TIMER:
        if (wParam == kTimerGgWatch) {
            WatchGg();
            return 0;
        }
        if (wParam == kTimerIdleScreen) {
            CheckIdleScreen();
            return 0;
        }
        if (wParam == kTimerDucking) {
            TickDucking();
            return 0;
        }
        KillTimer(window, wParam);
        if (wParam == kTimerReconnect) {
            ConnectDevice();
        } else if (wParam == kTimerRebindAudio) {
            g_app.mixer.Rebind();
            ApplyLevels();
            RefreshApps();
            if (g_app.settings.routing.enabled && !g_app.router.IsRunning()) {
                StartRouting();
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
        Cleanup();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

}  // namespace

int WINAPI wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int) {
    // After an update the new executable waits until the old instance has released the headset.
    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        for (int i = 1; i + 1 < argc; ++i) {
            if (wcscmp(argv[i], L"--after-update") == 0) {
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
    // Hidden top-level window: message-only windows miss WM_POWERBROADCAST and TaskbarCreated.
    if (!CreateWindowExW(0, kMainWindowClass, kAppName, WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, instance,
                         nullptr)) {
        CoUninitialize();
        return 1;
    }

    g_app.panel = std::make_unique<ui::Panel>(instance, g_app.host);
    g_app.osd = std::make_unique<ui::Osd>(instance);
    g_app.panel->SetTrayAnchor(g_app.window, kTrayId);
    g_app.panel->SetLevels(g_app.levels);
    g_app.osd->SetReduceMotion(g_app.settings.appearance.reduceMotion);
    PushSettings();
    PushIdleScreen();
    UpdateTray(NIM_ADD);
    RegisterHotkeys();
    UpdateDucking();
    const ProcessSet processes = RunningProcesses();
    g_app.ggRunning = IsGgRunning(processes);
    SetTimer(g_app.window, kTimerGgWatch, kGgWatchMs, nullptr);
    g_app.mixer.Init(g_app.window);
    ConnectDevice();
    // After ConnectDevice, which finds the headset that becomes the default output again.
    DropSonarDevices();
    if (g_app.settings.routing.enabled && !g_app.router.IsRunning()) {
        StartRouting();
    }
    UpdateRouting();
    SyncAppRouting();
    CheckProfiles(processes);
    PushUpdate();
    SetTimer(g_app.window, kTimerUpdateCheck, kFirstUpdateCheckMs, nullptr);

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
    return static_cast<int>(message.wParam);
}
