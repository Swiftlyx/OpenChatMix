// OpenChatMix Preview: the real panel with a simulated headset, to look at every page and setting.
//
// It opens no device, changes no Windows setting (default devices, autostart, hotkeys, app routing) and runs no
// audio, microphone or add-on. Settings are copied from %APPDATA%\OpenChatMix at each start into the folder of the
// executable, where the preview keeps them (portable mode), so the real settings are never written.
// The tray icon's menu chooses the headset and its state: model, battery, Bluetooth, SteelSeries GG, updates.
//
// Arguments:
//   --screenshots  keeps the settings already next to the executable and lists a fixed set of apps instead of the
//                  running ones, so pictures for the README come out the same each time and show nothing private.
//   --headset N    starts with the N-th headset of the menu (0-based) instead of the Arctis Nova Pro Wireless.

#include <windows.h>
#include <shellapi.h>
#include <objbase.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../../src/app_info.h"
#include "../../src/audio_devices.h"
#include "../../src/balance.h"
#include "../../src/devices.h"
#include "../../src/headset.h"
#include "../../src/i18n.h"
#include "../../src/icon.h"
#include "../../src/settings.h"
#include "../../src/settings_transfer.h"
#include "../../src/ui/panel.h"
#include "../../src/ui/theme.h"
#include "../../src/version.h"

namespace {

constexpr wchar_t kWindowClass[] = L"OpenChatMix.Preview";
constexpr UINT kTrayId = 1;
constexpr UINT WM_APP_TRAY = WM_APP + 1;
constexpr UINT kIconId = 1;

// Menu commands.
enum : UINT {
    kCmdShow = 100,
    kCmdQuit,
    kCmdGg,
    kCmdCharging,
    kCmdHeadsetOff,
    kCmdHeadsetBase = 200,    // + headset choice
    kCmdBatteryBase = 300,    // + index in kBatteryLevels
    kCmdBluetoothBase = 400,  // + BluetoothState
    kCmdUpdateBase = 500,     // + UpdateView::State
};

// What the preview shows instead of a SteelSeries headset.
enum class Other { UsbHeadset, BluetoothHeadset, Speakers, None };

struct Choice {
    DeviceFamily family = DeviceFamily::NovaProWired;
    Other other = Other::None;
    bool steelSeries = false;
    const wchar_t* name = L"";
};

// One model of each family covers every set of features; the others share them.
const Choice kChoices[] = {
    {DeviceFamily::NovaProWired, Other::None, true, L"Arctis Nova Pro Wired"},
    {DeviceFamily::NovaProWireless, Other::None, true, L"Arctis Nova Pro Wireless"},
    {DeviceFamily::Nova7, Other::None, true, L"Arctis Nova 7"},
    {DeviceFamily::Nova7Gen2, Other::None, true, L"Arctis Nova 7 Gen 2"},
    {DeviceFamily::Arctis7Plus, Other::None, true, L"Arctis 7+"},
    {DeviceFamily::Nova5X, Other::None, true, L"Arctis Nova 5X"},
    {DeviceFamily::NovaElite, Other::None, true, L"Arctis Nova Elite"},
    {DeviceFamily::NovaProWired, Other::UsbHeadset, false, L"Headset (USB Audio Device)"},
    {DeviceFamily::NovaProWired, Other::BluetoothHeadset, false, L"Headphones (Bluetooth)"},
    {DeviceFamily::NovaProWired, Other::Speakers, false, L"Speakers (High Definition Audio Device)"},
    {DeviceFamily::NovaProWired, Other::None, false, L""},
};
constexpr int kBatteryLevels[] = {100, 75, 50, 15, 5};

// Menu texts in the panel's language.
struct MenuTexts {
    const wchar_t* title;
    const wchar_t* headset;
    const wchar_t* noHeadset;
    const wchar_t* battery;
    const wchar_t* charging;
    const wchar_t* headsetOff;
    const wchar_t* bluetooth;
    const wchar_t* bluetoothStates[5];
    const wchar_t* gg;
    const wchar_t* update;
    const wchar_t* updateStates[7];
    const wchar_t* show;
    const wchar_t* quit;
    const wchar_t* importNote;
    const wchar_t* pluginNote;
};

const MenuTexts kEnglish = {
    L"OpenChatMix " APP_VERSION_WSTRING L" preview",
    L"Headset",
    L"No headset",
    L"Battery",
    L"Charging",
    L"Headset switched off",
    L"Bluetooth",
    {L"Unknown", L"Off", L"Pairing", L"On", L"Connected"},
    L"SteelSeries GG is running",
    L"Update",
    {L"Not checked", L"Checking", L"Up to date", L"Available", L"Installing", L"Failed", L"Check failed"},
    L"Show the panel",
    L"Quit the preview",
    L"This file holds OpenChatMix settings. OpenChatMix takes them and restarts; the preview leaves its settings as "
    L"they are.",
    L"Plugin windows open in OpenChatMix itself; the preview loads no plugins.",
};

const MenuTexts kUkrainian = {
    L"OpenChatMix " APP_VERSION_WSTRING L": перегляд",
    L"Гарнітура",
    L"Без гарнітури",
    L"Батарея",
    L"Заряджається",
    L"Гарнітуру вимкнено",
    L"Bluetooth",
    {L"Невідомо", L"Вимкнено", L"Сполучення", L"Увімкнено", L"Підключено"},
    L"Запущено SteelSeries GG",
    L"Оновлення",
    {L"Не перевірялося", L"Перевірка", L"Остання версія", L"Доступне", L"Встановлення", L"Помилка",
     L"Не вдалося перевірити"},
    L"Показати панель",
    L"Закрити перегляд",
    L"У цьому файлі є налаштування OpenChatMix. OpenChatMix бере їх і перезапускається; перегляд свої налаштування не "
    L"змінює.",
    L"Вікна плагінів відкриваються в самому OpenChatMix; перегляд плагіни не завантажує.",
};

const MenuTexts& Texts() {
    return CurrentLanguage() == Language::Ukrainian ? kUkrainian : kEnglish;
}

class PreviewHost;

struct Preview {
    HINSTANCE instance = nullptr;
    HWND window = nullptr;
    UINT taskbarCreated = 0;
    HICON trayIcon = nullptr;
    std::wstring iniPath;
    Settings settings;
    Levels levels;
    int choice = 0;
    int battery = 75;
    DeepFilterSettings deepFilter;  // Settings of a DeepFilterNet DLL in the plugin list.
    NvidiaAfxSettings nvidiaAfx{.status = NvidiaAfxSettings::Status::Ready};  // Of an NVAudioEffects.dll.
    bool charging = false;
    bool headsetOff = false;
    BluetoothState bluetooth = BluetoothState::Connected;
    bool ggRunning = false;
    bool ggDismissed = false;
    ui::UpdateView update;
    bool autostart = false;
    HeadsetState headset;
    std::wstring defaultOutput;  // The Windows default output, then the device chosen in the preview.
    bool screenshots = false;    // --screenshots
    std::unique_ptr<PreviewHost> host;
    std::unique_ptr<ui::Panel> panel;
};

Preview g;

const Choice& Current() {
    return kChoices[g.choice];
}

const HeadsetFeatures* Features() {
    return Current().steelSeries ? &FeaturesFor(Current().family) : nullptr;
}

// Bluetooth headphones report their level through Windows, never charging and never off.
bool BluetoothBattery() {
    return Current().other == Other::BluetoothHeadset;
}

bool BatteryShown() {
    const HeadsetFeatures* features = Features();
    return BluetoothBattery() || (features && features->batteryQuery && !g.headsetOff);
}

// Plausible values for every native setting of the model: the middle of a slider, the second choice.
HeadsetState MakeHeadsetState(const HeadsetFeatures& features) {
    HeadsetState state;
    for (const SettingSection& section : features.sections) {
        for (const SettingSpec& spec : section.settings) {
            const std::vector<uint8_t> values = SettingValues(spec);
            if (values.empty()) {
                continue;
            }
            const size_t index = spec.kind == SettingKind::Slider ? values.size() / 2 : std::min<size_t>(1, values.size() - 1);
            state.values[spec.command] = values[index];
        }
    }
    if (features.equalizer.bandsCommand) {
        state.eqPreset = features.equalizer.CustomIndex();
        state.eqCurve = {7, 12, 3, 0, 0, 0, 0, 2, 9, 12};
        state.eqCurveKnown = true;
    }
    return state;
}

uint32_t AccentSeed() {
    const AppearanceSettings& appearance = g.settings.appearance;
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

// The tray icon as OpenChatMix draws it, without the animation.
void UpdateTray(DWORD action) {
    const AppearanceSettings& appearance = g.settings.appearance;
    const bool light = TaskbarIsLight();
    LevelIconColors colors{0x1F1F1F, 0x1F1F1F, 0x1F1F1F};
    if (appearance.tray == TrayStyle::Monochrome) {
        if (!light) {
            colors = {0xFFFFFF, 0xFFFFFF, 0xFFFFFF};
        }
    } else {
        const ui::Palette palette = ui::MakePalette(AccentSeed(), !light, appearance.chatAccent, appearance.contrast);
        const float charge = g.battery / 100.0f;
        const D2D1_COLOR_F battery =
            appearance.trayBattery == BatteryTint::Theme ? palette.primary : ui::BatteryColor(charge, !light);
        colors = {PackRgb(palette.primary), PackRgb(palette.tertiary), PackRgb(battery)};
    }
    BalancedIcon mark = BalanceFromLevels(g.levels) == 0 ? appearance.balancedIcon : BalancedIcon::Bars;
    if (mark == BalancedIcon::Battery && !BatteryShown()) {
        mark = BalancedIcon::Bars;
    }
    HICON icon = CreateLevelIcon(GetSystemMetrics(SM_CXSMICON), static_cast<float>(g.levels.game),
                                 static_cast<float>(g.levels.chat), colors, mark, mark == BalancedIcon::Bars ? 0.0f : 1.0f,
                                 {g.battery / 100.0f, g.charging && !BluetoothBattery()});
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = g.window;
    data.uID = kTrayId;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data.uCallbackMessage = WM_APP_TRAY;
    data.hIcon = icon;
    wcsncpy_s(data.szTip, Texts().title, _TRUNCATE);
    if (Shell_NotifyIconW(action, &data) && action == NIM_ADD) {
        data.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &data);
    }
    if (g.trayIcon) {
        DestroyIcon(g.trayIcon);
    }
    g.trayIcon = icon;
}

std::wstring EndpointName(const std::wstring& id, const std::vector<AudioEndpoint>& endpoints) {
    for (const AudioEndpoint& endpoint : endpoints) {
        if (endpoint.id == id) {
            return endpoint.name;
        }
    }
    return {};
}

std::wstring EnvironmentPath(const wchar_t* name, const wchar_t* fallback) {
    wchar_t value[MAX_PATH] = {};
    const DWORD length = GetEnvironmentVariableW(name, value, MAX_PATH);
    return length && length < MAX_PATH ? value : fallback;
}

// The running apps, or with --screenshots two games and two chat apps, with their icons where they are installed.
std::vector<ProcessApp> RunningApps() {
    if (!g.screenshots) {
        return WindowedApps();
    }
    std::vector<ProcessApp> apps;
    auto add = [&](const wchar_t* exe, std::wstring path) {
        if (path.empty()) {
            const auto rule = g.settings.apps.find(exe);
            path = rule != g.settings.apps.end() ? rule->second.path : InstalledAppPath(exe);
        }
        if (!path.empty() && GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
            path.clear();
        }
        apps.push_back({exe, path});
    };
    add(L"steam.exe", EnvironmentPath(L"ProgramFiles(x86)", L"C:\\Program Files (x86)") + L"\\Steam\\steam.exe");
    add(L"chrome.exe", EnvironmentPath(L"ProgramFiles", L"C:\\Program Files") + L"\\Google\\Chrome\\Application\\chrome.exe");
    add(L"Discord.exe", L"");
    add(L"Telegram.exe", L"");
    return apps;
}

void PushStatus() {
    const Choice& choice = Current();
    std::wstring device = choice.name;
    std::wstring detail;
    std::wstring connection;
    bool connected = true;
    if (choice.steelSeries) {
        detail = T(Text::DialChatMix);
        if (choice.family == DeviceFamily::NovaProWired) {
            connection = L"USB";
        } else if (!g.headsetOff) {
            connection = T(Text::Link24Ghz);
            if (Features()->bluetooth && g.bluetooth == BluetoothState::Connected) {
                connection += L" + BT";
            }
        }
    } else {
        switch (choice.other) {
        case Other::UsbHeadset: connection = L"USB"; break;
        case Other::BluetoothHeadset: connection = L"Bluetooth"; break;
        case Other::Speakers: connection = T(Text::LinkJack); break;
        case Other::None:
            device = T(Text::NoHeadsetStatus);
            connected = false;
            break;
        }
        if (connected) {
            detail = T(Text::HotkeysOnly);
        }
    }
    const bool warn = g.ggRunning && choice.steelSeries && g.settings.warnGg && !g.ggDismissed;
    g.panel->SetStatus(device, detail, connection, connected, warn ? T(Text::GgRunning) : L"");
}

void PushHeadset() {
    const HeadsetFeatures* features = Features();
    g.headset.battery = BatteryShown() ? g.battery : -1;
    // The base stations of the Nova Pro Wireless and Nova Elite hold a spare battery, shown full.
    const bool station = Current().family == DeviceFamily::NovaElite || Current().family == DeviceFamily::NovaProWireless;
    g.headset.spareBattery = BatteryShown() && station ? 100 : -1;
    g.headset.charging = g.charging && !BluetoothBattery();
    g.headset.headsetOff = features && features->batteryQuery && g.headsetOff;
    g.headset.bluetooth = features && features->bluetooth ? g.bluetooth : BluetoothState::Unknown;
    const bool visible = features && features->HasSettings();
    g.panel->SetHeadset(Current().steelSeries ? Current().name : L"", visible ? features : nullptr, g.headset);
    g.panel->SetBalanceDial(Current().steelSeries);
}

void PushRouting() {
    const RoutingSettings& routing = g.settings.routing;
    const std::vector<AudioEndpoint> endpoints = ListRenderEndpoints(true);
    ui::RoutingView view;
    view.enabled = routing.enabled;
    auto set = [&](ui::RoutingSlot slot, const std::wstring& id, const std::wstring& name) {
        view.ids[static_cast<int>(slot)] = id;
        view.names[static_cast<int>(slot)] = name;
    };
    auto named = [&](const std::wstring& id, const wchar_t* fallback) {
        const std::wstring name = EndpointName(id, endpoints);
        return name.empty() ? std::wstring(fallback) : name;
    };
    set(ui::RoutingSlot::Game, routing.game, named(routing.game, L"CABLE-A Input (VB-Audio Cable A)"));
    set(ui::RoutingSlot::Chat, routing.chat, named(routing.chat, L"CABLE-B Input (VB-Audio Cable B)"));
    set(ui::RoutingSlot::Media, routing.media, routing.media.empty() ? L"" : named(routing.media, L""));
    set(ui::RoutingSlot::Output, routing.output,
        routing.output.empty() ? std::wstring(T(Text::AutomaticHeadset)) + L" · " + Current().name
                               : named(routing.output, L""));
    for (const auto& [slot, id] : {std::pair{ui::RoutingSlot::GameOutput, routing.gameOutput},
                                   std::pair{ui::RoutingSlot::ChatOutput, routing.chatOutput},
                                   std::pair{ui::RoutingSlot::MediaOutput, routing.mediaOutput}}) {
        set(slot, id,
            id.empty() ? std::wstring(T(routing.enabled ? Text::SameAsOutput : Text::WindowsDefaultOutput)) : named(id, L""));
    }
    view.mediaRouted = routing.enabled && g.settings.mediaGroup && !routing.media.empty();
    view.manageDevices = routing.manageDevices;
    if (routing.enabled) {
        view.running = true;
        view.status = std::wstring(T(Text::RoutingRunning)) + L" · " + T(Text::AddsLatency) + L" ~12 " +
                      T(Text::MillisecondsShort);
    }
    g.panel->SetRouting(view);
}

void PushMic() {
    const MicSettings& mic = g.settings.mic;
    ui::MicView view;
    view.inputName = mic.input.empty() ? L"" : EndpointName(mic.input, ListCaptureEndpoints());
    if (!mic.input.empty() && view.inputName.empty()) {
        view.inputName = T(Text::NotSelected);
    }
    view.outputName = mic.output.empty() ? L"CABLE Input (VB-Audio Virtual Cable)"
                                         : EndpointName(mic.output, ListRenderEndpoints());
    if (mic.enabled) {
        view.status = std::wstring(T(Text::RoutingRunning)) + L" · " + T(Text::AddsLatency) + L" ~6 " +
                      T(Text::MillisecondsShort);
    }
    g.panel->SetMic(view);
    // Plugins are listed by their file, as loaded; the preview loads none. DeepFilterNet and NVIDIA Audio Effects DLLs
    // show their settings.
    for (const EqChannel channel : {EqChannel::Mic, EqChannel::Game, EqChannel::Chat, EqChannel::Media}) {
        ui::PluginChainView chain;
        for (const ChainPlugin& plugin : PluginsOf(g.settings, channel)) {
            std::wstring name = FileNameOf(plugin.path);
            name = name.substr(0, name.find_last_of(L'.'));
            ui::PluginView row{name, plugin.path, plugin.enabled, true, true};
            if (name.find(L"DeepFilterNet") != std::wstring::npos) {
                row.name = L"DeepFilterNet";
                row.hasEditor = false;
                row.deepFilter = g.deepFilter;
                chain.latencyMs += channel == EqChannel::Mic ? 20 : 30;
            } else if (_wcsicmp(name.c_str(), L"NVAudioEffects") == 0) {
                row.name = L"NVIDIA Audio Effects";
                row.hasEditor = false;
                row.nvidiaAfx = g.nvidiaAfx;
            }
            chain.plugins.push_back(std::move(row));
        }
        g.panel->SetPlugins(channel, chain);
    }
}

void PushIdleScreen() {
    const IdleScreenSettings& idle = g.settings.idleScreen;
    ui::IdleScreenView view;
    view.animation = idle.animation;
    view.delaySeconds = idle.delaySeconds;
    view.imageName = idle.image.empty() ? L"" : FileNameOf(idle.image);
    view.ggApps = g.ggRunning ? L"Discord, Now Playing" : L"";
    const HeadsetFeatures* features = Features();
    view.bluetooth = features && features->bluetooth;
    view.bluetoothIcon = idle.bluetoothIcon;
    g.panel->SetIdleScreen(view);
}

void PushAddOns() {
    const HeadsetFeatures* features = Features();
    ui::AddOnsView view;
    view.state = g.ggRunning                   ? ui::AddOnsView::State::GgRunning
                 : !features || !features->oled ? ui::AddOnsView::State::NoDisplay
                                                : ui::AddOnsView::State::Running;
    for (const AddOnSettings& addOn : g.settings.addOns) {
        view.running.push_back(view.state == ui::AddOnsView::State::Running && addOn.enabled);
    }
    g.panel->SetAddOns(view);
}

void PushOutputs() {
    std::vector<AudioEndpoint> endpoints = ListRenderEndpoints();
    std::erase_if(endpoints, [](const AudioEndpoint& endpoint) { return IsVirtualEndpoint(endpoint); });
    const std::vector<std::wstring>& order = g.settings.outputs.order;
    auto rank = [&](const AudioEndpoint& endpoint) { return std::find(order.begin(), order.end(), endpoint.id) - order.begin(); };
    std::stable_sort(endpoints.begin(), endpoints.end(),
                     [&](const AudioEndpoint& a, const AudioEndpoint& b) { return rank(a) < rank(b); });
    const std::vector<std::wstring>& excluded = g.settings.outputs.excluded;
    auto isExcluded = [&](const std::wstring& id) { return std::find(excluded.begin(), excluded.end(), id) != excluded.end(); };
    std::stable_partition(endpoints.begin(), endpoints.end(),
                          [&](const AudioEndpoint& endpoint) { return !isExcluded(endpoint.id); });
    std::vector<ui::OutputView> views;
    for (const AudioEndpoint& endpoint : endpoints) {
        views.push_back({endpoint.id, endpoint.name, endpoint.id == g.defaultOutput, isExcluded(endpoint.id)});
    }
    g.panel->SetOutputs(std::move(views));
}

void PushApps() {
    std::vector<ui::AppItem> items;
    for (const ProcessApp& app : RunningApps()) {
        items.push_back({app.exe, app.path, AppDisplayName(app.path, app.exe), g.settings.GroupOf(app.exe), true});
    }
    for (const auto& [exe, rule] : g.settings.apps) {
        const bool listed = std::any_of(items.begin(), items.end(),
                                        [&](const ui::AppItem& item) { return _wcsicmp(item.exe.c_str(), exe.c_str()) == 0; });
        if (!listed) {
            items.push_back({exe, rule.path, AppDisplayName(rule.path, exe), rule.group, false});
        }
    }
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
    g.panel->SetApps(std::move(items));
}

void PushSettings() {
    g.panel->SetAutostart(g.autostart);
    g.panel->SetSettings(g.settings);
    g.panel->SetHotkeys(g.settings.hotkeys, {});
}

void ApplyAppearance() {
    if (!g.panel) {
        return;
    }
    const AppearanceSettings& appearance = g.settings.appearance;
    ui::SetTheme(appearance.theme, AccentSeed(), appearance.chatAccent, appearance.contrast);
    g.panel->Refresh();
    UpdateTray(NIM_MODIFY);
}

// Everything the panel shows, after the headset or its state changed.
void PushAll() {
    PushStatus();
    PushHeadset();
    PushSettings();
    PushRouting();
    PushMic();
    PushIdleScreen();
    PushAddOns();
    PushOutputs();
    g.panel->SetUpdate(g.update);
    g.panel->SetLevels(g.levels);
    UpdateTray(NIM_MODIFY);
}

void Save() {
    SaveSettings(g.iniPath, g.settings);
}

class PreviewHost final : public ui::PanelHost {
public:
    void OnBalanceChanged(int balance) override {
        g.levels = LevelsFromBalance(Current().steelSeries ? SnapBalance(balance, kBalanceSteps) : balance);
        g.panel->SetLevels(g.levels);
        UpdateTray(NIM_MODIFY);
    }
    void OnGroupChanged(const ui::AppItem& app, AppGroup group) override {
        g.settings.SetGroup(app.exe, group, app.path);
        Save();
        PushApps();
    }
    void OnAppRemoved(const ui::AppItem& app) override {
        g.settings.apps.erase(app.exe);
        Save();
        PushApps();
    }
    std::vector<ui::AppItem> RunningApps() override {
        std::vector<ui::AppItem> items;
        for (const ProcessApp& app : ::RunningApps()) {
            items.push_back({app.exe, app.path, AppDisplayName(app.path, app.exe), g.settings.GroupOf(app.exe), true});
        }
        std::sort(items.begin(), items.end(),
                  [](const ui::AppItem& a, const ui::AppItem& b) { return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0; });
        return items;
    }
    std::wstring AppName(const std::wstring& path) override { return AppDisplayName(path, FileNameOf(path)); }
    void OnHotkeyRecording(bool) override {}
    void OnHotkeyChanged(int action, Hotkey hotkey) override {
        for (auto& other : g.settings.hotkeys) {
            if (!hotkey.IsEmpty() && other == hotkey) {
                other = {};
            }
        }
        g.settings.hotkeys[action] = hotkey;
        Save();
        PushSettings();
    }
    void OnAutostartChanged(bool enabled) override {
        g.autostart = enabled;
        PushSettings();
    }
    void OnLanguageChanged(Language language) override {
        g.settings.language = language;
        SetLanguage(language);
        Save();
        PushAll();
        g.panel->Refresh();
    }
    void OnWarningDismissed() override {
        g.ggDismissed = true;
        PushStatus();
    }
    void OnHeadsetSetting(uint8_t command, int value) override {
        g.headset.values[command] = static_cast<int16_t>(value);
        PushHeadset();
    }
    void OnEqualizerPreset(int index) override {
        g.headset.eqPreset = index;
        PushHeadset();
        CountDownSave();
    }
    void OnEqualizerCurve(const EqCurve& curve) override {
        g.headset.eqCurve = curve;
        g.headset.eqCurveKnown = true;
        if (const HeadsetFeatures* features = Features()) {
            // As the app does: a parametric headset takes the curve as filters.
            if (features->equalizer.Parametric()) {
                OnEqualizerFilters(CurveFilters(curve, features->equalizer.range));
                return;
            }
            g.headset.eqPreset = features->equalizer.CustomIndex();
        }
        PushHeadset();
        CountDownSave();
    }
    void OnEqualizerFilters(const EqFilters& filters) override {
        if (const HeadsetFeatures* features = Features()) {
            // As the app does: within the headset's limits.
            ParametricEq eq;
            eq.bands = filters;
            g.headset.eqFilters = PresetFilters(eq, features->equalizer.notch);
            g.headset.eqFiltersKnown = true;
            g.headset.eqPreset = features->equalizer.CustomIndex();
        }
        PushHeadset();
        CountDownSave();
    }
    void OnBluetoothEqualizerPreset(int index) override {
        g.headset.btEqPreset = index;
        PushHeadset();
        CountDownSave();
    }
    void OnBluetoothEqualizerCurve(const EqCurve& curve) override {
        g.headset.btEqCurve = curve;
        g.headset.btEqCurveKnown = true;
        if (const HeadsetFeatures* features = Features()) {
            g.headset.btEqPreset = features->bluetoothEqualizer.CustomIndex();
        }
        PushHeadset();
        CountDownSave();
    }
    // As the app does for headsets that save once changes settle: a countdown, then Done.
    static void CountDownSave() {
        const HeadsetFeatures* features = Features();
        if (!features || !features->saveDelayMs) {
            return;
        }
        static UINT_PTR timer = 0;
        if (timer) {
            KillTimer(nullptr, timer);
        }
        g.panel->SetHeadsetSaving(GetTickCount64() + features->saveDelayMs, features->saveDelayMs);
        timer = SetTimer(nullptr, 0, features->saveDelayMs, [](HWND, UINT, UINT_PTR id, DWORD) {
            KillTimer(nullptr, id);
            timer = 0;
            g.panel->SetHeadsetSaved();
        });
    }
    void OnRoutingEnabled(bool enabled) override {
        g.settings.routing.enabled = enabled;
        Save();
        PushSettings();
        PushRouting();
    }
    void OnRoutingDevice(ui::RoutingSlot slot, const std::wstring& id) override {
        RoutingSettings& routing = g.settings.routing;
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
        PushSettings();
        PushRouting();
    }
    void OnRoutingManage(bool enabled) override {
        g.settings.routing.manageDevices = enabled;
        Save();
        PushRouting();
    }
    void OnIdleAnimation(IdleAnimationKind animation) override {
        g.settings.idleScreen.animation = animation;
        Save();
        PushIdleScreen();
    }
    void OnIdleDelay(int seconds) override {
        g.settings.idleScreen.delaySeconds = seconds;
        Save();
        PushIdleScreen();
    }
    void OnIdleImage(const std::wstring& path) override {
        g.settings.idleScreen.image = path;
        g.settings.idleScreen.animation = IdleAnimationKind::Image;
        Save();
        PushIdleScreen();
    }
    void OnIdleTest() override { MessageBeep(MB_OK); }
    void OnIdleBluetoothIcon(IconCorner corner) override {
        g.settings.idleScreen.bluetoothIcon = corner;
        Save();
        PushIdleScreen();
    }
    void OnAddOnPreview(const std::wstring&, const std::wstring&) override { MessageBeep(MB_OK); }
    void OnSettingsEdit(const std::function<void(Settings&)>& edit) override {
        const AppearanceSettings before = g.settings.appearance;
        edit(g.settings);
        Save();
        if (!(before == g.settings.appearance)) {
            ApplyAppearance();
        }
        PushSettings();
        PushRouting();
        PushMic();
        PushAddOns();
        PushOutputs();
        PushApps();
    }
    std::vector<AudioEndpoint> PlaybackDevices() override {
        std::vector<AudioEndpoint> endpoints = ListRenderEndpoints();
        std::erase_if(endpoints, IsSonarEndpoint);
        return endpoints;
    }
    std::vector<AudioEndpoint> RecordingDevices() override {
        std::vector<AudioEndpoint> endpoints = ListCaptureEndpoints();
        std::erase_if(endpoints, IsSonarEndpoint);
        return endpoints;
    }
    void OnPluginAdd(EqChannel channel, const std::wstring& path) override {
        static int added = 0;
        PluginsOf(g.settings, channel).push_back({path, L"preview-" + std::to_wstring(++added), true});
        Save();
        PushSettings();
        PushMic();
    }
    void OnPluginRemove(EqChannel channel, size_t index) override {
        std::vector<ChainPlugin>& plugins = PluginsOf(g.settings, channel);
        if (index < plugins.size()) {
            plugins.erase(plugins.begin() + static_cast<std::ptrdiff_t>(index));
            Save();
            PushSettings();
            PushMic();
        }
    }
    void OnPluginMove(EqChannel channel, size_t from, size_t to) override {
        std::vector<ChainPlugin>& plugins = PluginsOf(g.settings, channel);
        if (from >= plugins.size() || to >= plugins.size() || from == to) {
            return;
        }
        ChainPlugin moved = plugins[from];
        plugins.erase(plugins.begin() + static_cast<std::ptrdiff_t>(from));
        plugins.insert(plugins.begin() + static_cast<std::ptrdiff_t>(to), moved);
        Save();
        PushSettings();
        PushMic();
    }
    void OnPluginEditor(EqChannel, size_t) override {
        MessageBoxW(g.panel->Handle(), Texts().pluginNote, Texts().title, MB_OK | MB_ICONINFORMATION);
    }
    void OnPluginDeepFilter(EqChannel, size_t, const DeepFilterSettings& settings) override {
        g.deepFilter = settings;
        PushMic();
    }
    void OnPluginNvidiaAfx(EqChannel, size_t, const NvidiaAfxSettings& settings) override {
        g.nvidiaAfx = settings;
        g.nvidiaAfx.status = NvidiaAfxSettings::Status::Ready;
        PushMic();
    }
    void OnOutputChoose(const std::wstring& id) override {
        g.defaultOutput = id;
        PushOutputs();
    }
    void OnOutputMove(size_t from, size_t to) override {
        std::vector<AudioEndpoint> endpoints = ListRenderEndpoints();
        std::erase_if(endpoints, [](const AudioEndpoint& endpoint) { return IsVirtualEndpoint(endpoint); });
        std::vector<std::wstring>& order = g.settings.outputs.order;
        for (const AudioEndpoint& endpoint : endpoints) {
            if (std::find(order.begin(), order.end(), endpoint.id) == order.end()) {
                order.push_back(endpoint.id);
            }
        }
        if (from < order.size() && to < order.size() && from != to) {
            const std::wstring moved = order[from];
            order.erase(order.begin() + static_cast<std::ptrdiff_t>(from));
            order.insert(order.begin() + static_cast<std::ptrdiff_t>(to), moved);
        }
        Save();
        PushSettings();
        PushOutputs();
    }
    void OnOutputExclude(const std::wstring& id, bool excluded) override {
        std::erase(g.settings.outputs.excluded, id);
        if (excluded) {
            g.settings.outputs.excluded.push_back(id);
        }
        Save();
        PushSettings();
        PushOutputs();
    }
    void OnCheckForUpdates() override {
        g.update = {ui::UpdateView::State::UpToDate, L""};
        g.panel->SetUpdate(g.update);
    }
    void OnInstallUpdate() override {
        g.update.state = ui::UpdateView::State::Installing;
        g.panel->SetUpdate(g.update);
    }
    void OnOpenReleasePage() override {}
    void OnQuit() override { PostMessageW(g.window, WM_CLOSE, 0, 0); }
    bool OnSettingsExport(const std::wstring& path) override {
        Save();
        return ExportSettings(ConfigDirectory(), path);
    }
    bool OnSettingsImport(const std::wstring& path) override {
        if (!IsSettingsArchive(path)) {
            return false;
        }
        MessageBoxW(g.panel->Handle(), Texts().importNote, Texts().title, MB_OK | MB_ICONINFORMATION);
        return true;
    }
    ui::Meters ReadMeters() override {
        // Levels that move as sound would: Game steady with a beat, Chat in bursts of speech.
        const double t = static_cast<double>(GetTickCount64()) / 1000.0;
        ui::Meters meters;
        meters.game = static_cast<float>(0.35 + 0.2 * std::fabs(std::sin(t * 2.1)) + 0.05 * std::sin(t * 13.0));
        meters.chat = std::fmod(t, 6.0) < 2.5 ? static_cast<float>(0.25 + 0.2 * std::fabs(std::sin(t * 7.3))) : 0.0f;
        meters.mic = static_cast<float>(0.2 + 0.15 * std::fabs(std::sin(t * 5.7)));
        return meters;
    }
};

void ChooseHeadset(int choice) {
    g.choice = choice;
    if (const HeadsetFeatures* features = Features()) {
        g.headset = MakeHeadsetState(*features);
    } else {
        g.headset = HeadsetState();
    }
    g.ggDismissed = false;
    if (Current().steelSeries) {
        g.levels = LevelsFromBalance(SnapBalance(BalanceFromLevels(g.levels), kBalanceSteps));
    }
    PushAll();
}

void ShowMenu() {
    const MenuTexts& texts = Texts();
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, texts.title);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    HMENU headsets = CreatePopupMenu();
    for (int i = 0; i < static_cast<int>(std::size(kChoices)); ++i) {
        if (i == 6) {
            AppendMenuW(headsets, MF_SEPARATOR, 0, nullptr);
        }
        const wchar_t* name = kChoices[i].name[0] ? kChoices[i].name : texts.noHeadset;
        AppendMenuW(headsets, MF_STRING | (i == g.choice ? MF_CHECKED : 0), kCmdHeadsetBase + i, name);
    }
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(headsets), texts.headset);

    const HeadsetFeatures* features = Features();
    const bool wireless = BluetoothBattery() || (features && features->batteryQuery);
    HMENU battery = CreatePopupMenu();
    for (int i = 0; i < static_cast<int>(std::size(kBatteryLevels)); ++i) {
        const std::wstring label = std::to_wstring(kBatteryLevels[i]) + L"%";
        AppendMenuW(battery, MF_STRING | (kBatteryLevels[i] == g.battery ? MF_CHECKED : 0), kCmdBatteryBase + i,
                    label.c_str());
    }
    AppendMenuW(battery, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(battery, MF_STRING | (g.charging ? MF_CHECKED : 0), kCmdCharging, texts.charging);
    AppendMenuW(battery, MF_STRING | (g.headsetOff ? MF_CHECKED : 0), kCmdHeadsetOff, texts.headsetOff);
    AppendMenuW(menu, MF_POPUP | (wireless ? 0 : MF_GRAYED), reinterpret_cast<UINT_PTR>(battery), texts.battery);

    HMENU bluetooth = CreatePopupMenu();
    for (int state = 1; state <= 4; ++state) {
        AppendMenuW(bluetooth, MF_STRING | (static_cast<int>(g.bluetooth) == state ? MF_CHECKED : 0),
                    kCmdBluetoothBase + state, texts.bluetoothStates[state]);
    }
    AppendMenuW(menu, MF_POPUP | (features && features->bluetooth ? 0 : MF_GRAYED), reinterpret_cast<UINT_PTR>(bluetooth),
                texts.bluetooth);

    HMENU update = CreatePopupMenu();
    for (int state = 0; state < 7; ++state) {
        AppendMenuW(update, MF_STRING | (static_cast<int>(g.update.state) == state ? MF_CHECKED : 0),
                    kCmdUpdateBase + state, texts.updateStates[state]);
    }
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(update), texts.update);
    AppendMenuW(menu, MF_STRING | (g.ggRunning ? MF_CHECKED : 0), kCmdGg, texts.gg);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdShow, texts.show);
    AppendMenuW(menu, MF_STRING, kCmdQuit, texts.quit);

    POINT cursor{};
    GetCursorPos(&cursor);
    SetForegroundWindow(g.window);
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, cursor.x, cursor.y, 0,
                                        g.window, nullptr);
    DestroyMenu(menu);
    if (command >= kCmdHeadsetBase && command < kCmdHeadsetBase + std::size(kChoices)) {
        ChooseHeadset(static_cast<int>(command - kCmdHeadsetBase));
    } else if (command >= kCmdBatteryBase && command < kCmdBatteryBase + std::size(kBatteryLevels)) {
        g.battery = kBatteryLevels[command - kCmdBatteryBase];
        PushAll();
    } else if (command >= kCmdBluetoothBase && command <= kCmdBluetoothBase + 4) {
        g.bluetooth = static_cast<BluetoothState>(command - kCmdBluetoothBase);
        PushAll();
    } else if (command >= kCmdUpdateBase && command < kCmdUpdateBase + 7) {
        g.update.state = static_cast<ui::UpdateView::State>(command - kCmdUpdateBase);
        g.update.version = g.update.state == ui::UpdateView::State::Available ||
                                   g.update.state == ui::UpdateView::State::Installing
                               ? L"2.1.0"
                               : L"";
        g.panel->SetUpdate(g.update);
    } else if (command == kCmdCharging) {
        g.charging = !g.charging;
        PushAll();
    } else if (command == kCmdHeadsetOff) {
        g.headsetOff = !g.headsetOff;
        PushAll();
    } else if (command == kCmdGg) {
        g.ggRunning = !g.ggRunning;
        g.ggDismissed = false;
        PushAll();
    } else if (command == kCmdShow) {
        g.panel->Show();
    } else if (command == kCmdQuit) {
        PostMessageW(g.window, WM_CLOSE, 0, 0);
    }
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == g.taskbarCreated && g.taskbarCreated != 0) {
        UpdateTray(NIM_ADD);
        return 0;
    }
    switch (message) {
    case WM_APP_TRAY:
        switch (LOWORD(lParam)) {
        case NIN_SELECT:
        case NIN_KEYSELECT:
            g.panel->Toggle();
            break;
        case WM_CONTEXTMENU:
            ShowMenu();
            break;
        }
        return 0;
    case WM_SETTINGCHANGE:
    case WM_DWMCOLORIZATIONCOLORCHANGED:
        ApplyAppearance();
        break;
    case WM_DESTROY: {
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = window;
        data.uID = kTrayId;
        Shell_NotifyIconW(NIM_DELETE, &data);
        g.panel.reset();
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

// The real settings, copied next to the executable, so the preview keeps its own (portable mode).
bool PrepareSettings() {
    const std::wstring folder = ExeDirectory();
    g.iniPath = folder + L"\\OpenChatMix.ini";
    wchar_t appData[MAX_PATH] = {};
    const DWORD length = GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH);
    const std::wstring real = length && length < MAX_PATH ? std::wstring(appData) + L"\\OpenChatMix" : L"";
    if (g.screenshots || real.empty() || _wcsicmp(real.c_str(), folder.c_str()) == 0 ||
        !CopyFileW((real + L"\\OpenChatMix.ini").c_str(), g.iniPath.c_str(), FALSE)) {
        // Without settings to copy the preview starts with the defaults; the file still marks the folder as its own.
        if (GetFileAttributesW(g.iniPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            HANDLE file = CreateFileW(g.iniPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file != INVALID_HANDLE_VALUE) {
                const BYTE bom[] = {0xFF, 0xFE};
                DWORD written = 0;
                WriteFile(file, bom, sizeof(bom), &written, nullptr);
                CloseHandle(file);
            }
        }
    }
    if (!real.empty() && !g.screenshots) {
        // Presets show in the panel's lists.
        const std::wstring presets = folder + L"\\Presets";
        CreateDirectoryW(presets.c_str(), nullptr);
        CreateDirectoryW((presets + L"\\Parametric").c_str(), nullptr);
        for (const wchar_t* sub : {L"", L"\\Parametric"}) {
            WIN32_FIND_DATAW found{};
            const std::wstring from = real + L"\\Presets" + sub;
            HANDLE find = FindFirstFileW((from + L"\\*.json").c_str(), &found);
            if (find == INVALID_HANDLE_VALUE) {
                continue;
            }
            do {
                CopyFileW((from + L"\\" + found.cFileName).c_str(), (presets + sub + L"\\" + found.cFileName).c_str(),
                          FALSE);
            } while (FindNextFileW(find, &found));
            FindClose(find);
        }
    }
    // Everything the panel writes must land in this folder, never in the real one.
    return _wcsicmp(ConfigDirectory().c_str(), folder.c_str()) == 0;
}

}  // namespace

int WINAPI wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int) {
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\OpenChatMix.Preview");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND running = FindWindowW(kWindowClass, nullptr)) {
            PostMessageW(running, WM_APP_TRAY, 0, NIN_SELECT);
        }
        return 0;
    }
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
        return 1;
    }
    int headset = 1;
    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        for (int i = 1; i < argc; ++i) {
            if (wcscmp(argv[i], L"--screenshots") == 0) {
                g.screenshots = true;
            } else if (wcscmp(argv[i], L"--headset") == 0 && i + 1 < argc) {
                headset = std::clamp(_wtoi(argv[++i]), 0, static_cast<int>(std::size(kChoices)) - 1);
            }
        }
        LocalFree(argv);
    }
    if (!PrepareSettings()) {
        MessageBoxW(nullptr, L"The preview could not prepare its own settings folder next to its executable.",
                    L"OpenChatMix preview", MB_OK | MB_ICONERROR);
        return 1;
    }
    g.instance = instance;
    g.settings = LoadSettings(g.iniPath);
    g.levels = g.settings.levels;
    g.autostart = IsAutostartEnabled();
    g.defaultOutput = DefaultRenderEndpoint(eConsole);
    SetLanguage(g.settings.language);
    const AppearanceSettings& appearance = g.settings.appearance;
    ui::SetTheme(appearance.theme, AccentSeed(), appearance.chatAccent, appearance.contrast);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(kIconId));
    windowClass.lpszClassName = kWindowClass;
    RegisterClassExW(&windowClass);
    g.taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    g.window = CreateWindowExW(0, kWindowClass, L"OpenChatMix preview", WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr,
                               instance, nullptr);
    if (!g.window) {
        CoUninitialize();
        return 1;
    }
    g.host = std::make_unique<PreviewHost>();
    g.panel = std::make_unique<ui::Panel>(instance, *g.host);
    g.panel->SetTrayAnchor(g.window, kTrayId);
    // The panel stays open while the tray menu changes the headset; a click on the tray icon hides it.
    g.panel->SetAutoHide(false);
    UpdateTray(NIM_ADD);
    PushApps();
    ChooseHeadset(headset);
    g.panel->Show();

    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (g.trayIcon) {
        DestroyIcon(g.trayIcon);
    }
    CoUninitialize();
    CloseHandle(mutex);
    return static_cast<int>(message.wParam);
}
