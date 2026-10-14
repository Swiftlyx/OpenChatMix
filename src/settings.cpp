#include "settings.h"

#include <windows.h>

#include <shlobj.h>

#include <algorithm>
#include <iterator>
#include <vector>

namespace {

constexpr wchar_t kMainSection[] = L"ChatMix";
constexpr wchar_t kAppsSection[] = L"Apps";
constexpr wchar_t kRoutingSection[] = L"Routing";
constexpr wchar_t kIdleSection[] = L"IdleScreen";
constexpr wchar_t kAppearanceSection[] = L"Appearance";
constexpr wchar_t kDuckingSection[] = L"Ducking";
constexpr wchar_t kBatterySection[] = L"Battery";
constexpr wchar_t kProfilesSection[] = L"Profiles";
constexpr wchar_t kGameEqSection[] = L"Equalizer.Game";
constexpr wchar_t kChatEqSection[] = L"Equalizer.Chat";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"OpenChatMix";
constexpr wchar_t kLegacyRunValue[] = L"NovaChatMix";
constexpr wchar_t kConfigFile[] = L"OpenChatMix.ini";
constexpr wchar_t kPathSeparator = L'|';
constexpr const wchar_t* kHotkeyKeys[kHotkeyCount] = {L"HotkeyTowardGame", L"HotkeyTowardChat", L"HotkeyCenter",
                                                       L"HotkeyMuteChat", L"HotkeyNextPreset"};
constexpr const wchar_t* kThemeNames[] = {L"system", L"light", L"dark"};
constexpr const wchar_t* kBackdropNames[] = {L"acrylic", L"mica", L"solid"};
constexpr const wchar_t* kTrayNames[] = {L"color", L"monochrome"};
// Indexed by IdleAnimationKind.
constexpr const wchar_t* kIdleAnimationNames[] = {L"off", L"stars", L"bounce", L"clock", L"life", L"blank", L"image"};

std::wstring ReadString(const std::wstring& path, const wchar_t* section, const wchar_t* key,
                        const wchar_t* fallback) {
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetPrivateProfileStringW(section, key, fallback, buffer.data(),
                                                  static_cast<DWORD>(buffer.size()), path.c_str());
    buffer.resize(length);
    return buffer;
}

int ReadInt(const std::wstring& path, const wchar_t* key, int fallback) {
    return static_cast<int>(GetPrivateProfileIntW(kMainSection, key, fallback, path.c_str()));
}

void WriteString(const std::wstring& path, const wchar_t* key, const wchar_t* value) {
    WritePrivateProfileStringW(kMainSection, key, value, path.c_str());
}

std::wstring Trim(std::wstring text) {
    text.erase(0, text.find_first_not_of(L" \t"));
    text.erase(text.find_last_not_of(L" \t") + 1);
    return text;
}

template <class Enum, size_t Count>
Enum ParseName(const std::wstring& text, const wchar_t* const (&names)[Count], Enum fallback) {
    for (size_t i = 0; i < Count; ++i) {
        if (_wcsicmp(text.c_str(), names[i]) == 0) {
            return static_cast<Enum>(i);
        }
    }
    return fallback;
}

int ReadSectionInt(const std::wstring& path, const wchar_t* section, const wchar_t* key, int fallback) {
    return static_cast<int>(GetPrivateProfileIntW(section, key, fallback, path.c_str()));
}

void WriteSectionString(const std::wstring& path, const wchar_t* section, const wchar_t* key,
                        const std::wstring& value) {
    WritePrivateProfileStringW(section, key, value.c_str(), path.c_str());
}

void LoadEqChannel(const std::wstring& path, const wchar_t* section, EqChannelSettings& channel) {
    channel.enabled = ReadSectionInt(path, section, L"Enabled", 0) != 0;
    channel.preset = ReadString(path, section, L"Preset", L"");
    channel.eq = FlatEq();
    channel.eq.preamp = static_cast<float>(_wtof(ReadString(path, section, L"Preamp", L"0").c_str()));
    for (int i = 0; i < kParametricBands; ++i) {
        const std::wstring key = L"Band" + std::to_wstring(i + 1);
        if (const auto band = ParseBand(ReadString(path, section, key.c_str(), L""))) {
            channel.eq.bands[i] = *band;
        }
    }
    auto effect = [&](const wchar_t* key) {
        const float value = static_cast<float>(_wtof(ReadString(path, section, key, L"0").c_str()));
        return std::clamp(value, 0.0f, kEffectMaxGain);
    };
    EqEffects& effects = channel.eq.effects;
    effects.bassBoost = effect(L"BassBoost");
    effects.trebleBoost = effect(L"TrebleBoost");
    effects.voiceClarity = effect(L"VoiceClarity");
    effects.smartVolume = ParseSmartVolume(ReadString(path, section, L"SmartVolume", L"off"));
}

void SaveEqChannel(const std::wstring& path, const wchar_t* section, const EqChannelSettings& channel) {
    WriteSectionString(path, section, L"Enabled", channel.enabled ? L"1" : L"0");
    WriteSectionString(path, section, L"Preset", channel.preset);
    wchar_t preamp[32];
    swprintf_s(preamp, L"%.2f", channel.eq.preamp);
    WriteSectionString(path, section, L"Preamp", preamp);
    for (int i = 0; i < kParametricBands; ++i) {
        const std::wstring key = L"Band" + std::to_wstring(i + 1);
        WriteSectionString(path, section, key.c_str(), FormatBand(channel.eq.bands[i]));
    }
    auto effect = [&](const wchar_t* key, float value) {
        wchar_t text[16];
        swprintf_s(text, L"%.1f", value);
        WriteSectionString(path, section, key, text);
    };
    const EqEffects& effects = channel.eq.effects;
    effect(L"BassBoost", effects.bassBoost);
    effect(L"TrebleBoost", effects.trebleBoost);
    effect(L"VoiceClarity", effects.voiceClarity);
    WriteSectionString(path, section, L"SmartVolume", SmartVolumeKey(effects.smartVolume));
}

// [Profiles] exe=path|balance|gameEq|headsetEq; an empty balance leaves it unchanged.
void LoadProfiles(const std::wstring& path, Settings& settings) {
    std::vector<wchar_t> buffer(65536, L'\0');
    const DWORD length = GetPrivateProfileSectionW(kProfilesSection, buffer.data(), static_cast<DWORD>(buffer.size()),
                                                   path.c_str());
    for (size_t offset = 0; offset < length;) {
        const std::wstring entry(buffer.data() + offset);
        offset += entry.size() + 1;
        const size_t equals = entry.find(L'=');
        if (equals == std::wstring::npos) {
            continue;
        }
        GameProfile profile;
        profile.exe = Trim(entry.substr(0, equals));
        std::vector<std::wstring> fields;
        const std::wstring value = entry.substr(equals + 1);
        for (size_t start = 0;;) {
            const size_t end = value.find(kPathSeparator, start);
            fields.push_back(value.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start));
            if (end == std::wstring::npos) {
                break;
            }
            start = end + 1;
        }
        fields.resize(4);
        profile.path = fields[0];
        // Percent with a "%" sign; a bare number is a dial step of earlier versions.
        if (!fields[1].empty()) {
            const int number = _wtoi(fields[1].c_str());
            profile.balance = fields[1].back() == L'%' ? std::clamp(number, -kBalanceRange, kBalanceRange)
                                                       : BalanceAt(number, kBalanceSteps);
        }
        profile.gameEq = fields[2];
        profile.headsetEq = fields[3].empty() ? -1 : _wtoi(fields[3].c_str());
        if (!profile.exe.empty()) {
            settings.profiles.push_back(std::move(profile));
        }
    }
}

void SaveProfiles(const std::wstring& path, const Settings& settings) {
    std::wstring section;
    for (const auto& profile : settings.profiles) {
        section += profile.exe + L"=" + profile.path + kPathSeparator +
                   (profile.balance ? std::to_wstring(*profile.balance) + L"%" : L"") + kPathSeparator + profile.gameEq +
                   kPathSeparator + (profile.headsetEq >= 0 ? std::to_wstring(profile.headsetEq) : L"");
        section += L'\0';
    }
    section += L'\0';
    WritePrivateProfileSectionW(kProfilesSection, section.c_str(), path.c_str());
}

std::vector<std::wstring> SplitList(const std::wstring& text) {
    std::vector<std::wstring> items;
    for (size_t start = 0; start < text.size();) {
        size_t end = text.find(kPathSeparator, start);
        if (end == std::wstring::npos) {
            end = text.size();
        }
        if (end > start) {
            items.push_back(text.substr(start, end - start));
        }
        start = end + 1;
    }
    return items;
}

IdleAnimationKind ParseIdleAnimation(const std::wstring& text) {
    for (size_t i = 0; i < std::size(kIdleAnimationNames); ++i) {
        if (_wcsicmp(text.c_str(), kIdleAnimationNames[i]) == 0) {
            return static_cast<IdleAnimationKind>(i);
        }
    }
    return IdleAnimationKind::Off;
}

const wchar_t* GroupName(AppGroup group) {
    switch (group) {
    case AppGroup::Chat: return L"chat";
    case AppGroup::Media: return L"media";
    case AppGroup::Game: break;
    }
    return L"game";
}

AppGroup ParseGroup(const std::wstring& text) {
    if (_wcsicmp(text.c_str(), L"chat") == 0) {
        return AppGroup::Chat;
    }
    if (_wcsicmp(text.c_str(), L"media") == 0) {
        return AppGroup::Media;
    }
    return AppGroup::Game;
}

void LoadApps(const std::wstring& path, Settings& settings) {
    std::vector<wchar_t> buffer(65536, L'\0');
    const DWORD length = GetPrivateProfileSectionW(kAppsSection, buffer.data(), static_cast<DWORD>(buffer.size()),
                                                   path.c_str());
    // Section data is a sequence of "key=value\0" entries terminated by an extra \0.
    for (size_t offset = 0; offset < length;) {
        const std::wstring entry(buffer.data() + offset);
        offset += entry.size() + 1;
        const size_t equals = entry.find(L'=');
        if (equals == std::wstring::npos) {
            continue;
        }
        const std::wstring exe = Trim(entry.substr(0, equals));
        const std::wstring value = entry.substr(equals + 1);
        const size_t separator = value.find(kPathSeparator);
        const AppGroup group = ParseGroup(Trim(value.substr(0, separator)));
        const std::wstring exePath = separator == std::wstring::npos ? L"" : Trim(value.substr(separator + 1));
        if (!exe.empty()) {
            settings.SetGroup(exe, group, exePath);
        }
    }

    // Version 1 stored chat apps as a single semicolon-separated list.
    const std::wstring legacy = ReadString(path, kMainSection, L"ChatApps", L"");
    for (size_t start = 0; start < legacy.size();) {
        size_t end = legacy.find(L';', start);
        if (end == std::wstring::npos) {
            end = legacy.size();
        }
        const std::wstring exe = Trim(legacy.substr(start, end - start));
        if (!exe.empty() && settings.GroupOf(exe) == AppGroup::Game) {
            settings.SetGroup(exe, AppGroup::Chat, L"");
        }
        start = end + 1;
    }
}

}  // namespace

bool NoCaseLess::operator()(const std::wstring& a, const std::wstring& b) const {
    return _wcsicmp(a.c_str(), b.c_str()) < 0;
}

AppGroup Settings::GroupOf(const std::wstring& exe) const {
    const auto it = apps.find(exe);
    return it == apps.end() ? AppGroup::Game : it->second.group;
}

AppGroup Settings::EffectiveGroupOf(const std::wstring& exe) const {
    const AppGroup group = GroupOf(exe);
    return group == AppGroup::Media && !MediaAvailable() ? AppGroup::Game : group;
}

void Settings::SetGroup(const std::wstring& exe, AppGroup group, const std::wstring& path) {
    if (group == AppGroup::Game) {
        apps.erase(exe);
        return;
    }
    AppRule& rule = apps[exe];
    rule.group = group;
    if (!path.empty()) {
        rule.path = path;
    }
}

std::wstring ExePath() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length < path.size()) {
            path.resize(length);
            return path;
        }
        path.resize(path.size() * 2);
    }
}

std::wstring ExeDirectory() {
    const std::wstring path = ExePath();
    return path.substr(0, path.find_last_of(L"\\/"));
}

std::wstring ConfigDirectory() {
    const std::wstring portable = ExeDirectory();
    if (GetFileAttributesW((portable + L"\\" + kConfigFile).c_str()) != INVALID_FILE_ATTRIBUTES) {
        return portable;
    }
    PWSTR appData = nullptr;
    std::wstring directory = portable;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData))) {
        directory = std::wstring(appData) + L"\\OpenChatMix";
        CreateDirectoryW(directory.c_str(), nullptr);
    }
    CoTaskMemFree(appData);
    return directory;
}

Settings LoadSettings(const std::wstring& path) {
    Settings settings;
    settings.firstRun = GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES;
    for (int action = 0; action < kHotkeyCount; ++action) {
        settings.hotkeys[action] = DefaultHotkey(action);
    }
    if (settings.firstRun) {
        settings.SetGroup(L"Discord.exe", AppGroup::Chat, L"");
        return settings;
    }

    LoadApps(path, settings);
    settings.levels = ClampLevels(ReadInt(path, L"Game", 100), ReadInt(path, L"Chat", 100));
    settings.log = ReadInt(path, L"Log", 0) != 0;
    settings.checkUpdates = ReadInt(path, L"CheckUpdates", 1) != 0;
    settings.lastUpdateCheck = _wtoi64(ReadString(path, kMainSection, L"LastUpdateCheck", L"0").c_str());
    settings.showPopup = ReadInt(path, L"ShowPopup", 1) != 0;
    settings.dialPopup = ReadInt(path, L"DialPopup", 1) != 0;
    settings.warnGg = ReadInt(path, L"WarnGg", 1) != 0;
    settings.mediaGroup = ReadInt(path, L"MediaGroup", 1) != 0;
    settings.equalizer = ReadInt(path, L"Equalizer", 1) != 0;

    AppearanceSettings& appearance = settings.appearance;
    appearance.theme = ParseName(ReadString(path, kAppearanceSection, L"Theme", L"system"), kThemeNames,
                                 ThemeMode::System);
    const std::wstring accent = ReadString(path, kAppearanceSection, L"Accent", L"");
    appearance.windowsAccent = _wcsicmp(accent.c_str(), L"windows") == 0;
    if (accent.size() == 7 && accent[0] == L'#') {
        appearance.accent = static_cast<uint32_t>(wcstoul(accent.c_str() + 1, nullptr, 16)) & 0xFFFFFF;
    }
    // Version 1 had a single translucency switch.
    const bool translucent = ReadInt(path, L"Translucent", 1) != 0;
    appearance.backdrop = ParseName(ReadString(path, kAppearanceSection, L"Backdrop", translucent ? L"acrylic" : L"solid"),
                                    kBackdropNames, Backdrop::Acrylic);
    appearance.reduceMotion = ReadSectionInt(path, kAppearanceSection, L"ReduceMotion", 0) != 0;
    appearance.tray = ParseName(ReadString(path, kAppearanceSection, L"TrayIcon", L"color"), kTrayNames, TrayStyle::Color);

    settings.ducking.enabled = ReadSectionInt(path, kDuckingSection, L"Enabled", 0) != 0;
    settings.ducking.amount = std::clamp(ReadSectionInt(path, kDuckingSection, L"Amount", 50), 10, 90);
    settings.battery.enabled = ReadSectionInt(path, kBatterySection, L"Alert", 1) != 0;
    settings.battery.threshold = std::clamp(ReadSectionInt(path, kBatterySection, L"Threshold", 15), 5, 50);
    LoadEqChannel(path, kGameEqSection, settings.gameEq);
    LoadEqChannel(path, kChatEqSection, settings.chatEq);
    LoadProfiles(path, settings);
    settings.routing.enabled = GetPrivateProfileIntW(kRoutingSection, L"Enabled", 0, path.c_str()) != 0;
    settings.routing.game = ReadString(path, kRoutingSection, L"Game", L"");
    settings.routing.chat = ReadString(path, kRoutingSection, L"Chat", L"");
    settings.routing.output = ReadString(path, kRoutingSection, L"Output", L"");
    settings.routing.manageDevices = GetPrivateProfileIntW(kRoutingSection, L"ManageDevices", 1, path.c_str()) != 0;
    settings.routing.savedDefault = ReadString(path, kRoutingSection, L"SavedDefault", L"");
    settings.routing.savedCommunications = ReadString(path, kRoutingSection, L"SavedCommunications", L"");
    settings.routing.autoRoute = GetPrivateProfileIntW(kRoutingSection, L"AutoRoute", 1, path.c_str()) != 0;
    settings.routing.routedApps = SplitList(ReadString(path, kRoutingSection, L"RoutedApps", L""));
    settings.idleScreen.animation = ParseIdleAnimation(ReadString(path, kIdleSection, L"Animation", L"off"));
    settings.idleScreen.delaySeconds =
        std::clamp(static_cast<int>(GetPrivateProfileIntW(kIdleSection, L"Delay", 60, path.c_str())), 5, 3600);
    settings.idleScreen.image = ReadString(path, kIdleSection, L"Image", L"");
    settings.language =
        _wcsicmp(ReadString(path, kMainSection, L"Language", L"en").c_str(), L"uk") == 0 ? Language::Ukrainian
                                                                                          : Language::English;
    for (int action = 0; action < kHotkeyCount; ++action) {
        const std::wstring value = ReadString(path, kMainSection, kHotkeyKeys[action], L"");
        if (!value.empty()) {
            settings.hotkeys[action] = UnpackHotkey(static_cast<uint32_t>(wcstoul(value.c_str(), nullptr, 0)));
        }
    }
    return settings;
}

void SaveSettings(const std::wstring& path, const Settings& settings) {
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        // A UTF-16LE BOM makes the profile API store values as Unicode instead of the ANSI code page.
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            const WORD bom = 0xFEFF;
            DWORD written = 0;
            WriteFile(file, &bom, sizeof(bom), &written, nullptr);
            CloseHandle(file);
        }
    }

    WriteString(path, L"Game", std::to_wstring(settings.levels.game).c_str());
    WriteString(path, L"Chat", std::to_wstring(settings.levels.chat).c_str());
    WriteString(path, L"Log", settings.log ? L"1" : L"0");
    WriteString(path, L"CheckUpdates", settings.checkUpdates ? L"1" : L"0");
    WriteString(path, L"LastUpdateCheck", std::to_wstring(settings.lastUpdateCheck).c_str());
    WriteString(path, L"ShowPopup", settings.showPopup ? L"1" : L"0");
    WriteString(path, L"DialPopup", settings.dialPopup ? L"1" : L"0");
    WriteString(path, L"WarnGg", settings.warnGg ? L"1" : L"0");
    WriteString(path, L"MediaGroup", settings.mediaGroup ? L"1" : L"0");
    WriteString(path, L"Equalizer", settings.equalizer ? L"1" : L"0");
    WriteString(path, L"Translucent", nullptr);

    const AppearanceSettings& appearance = settings.appearance;
    WriteSectionString(path, kAppearanceSection, L"Theme", kThemeNames[static_cast<int>(appearance.theme)]);
    wchar_t accent[16];
    swprintf_s(accent, L"#%06X", appearance.accent & 0xFFFFFF);
    WriteSectionString(path, kAppearanceSection, L"Accent", appearance.windowsAccent ? L"windows" : accent);
    WriteSectionString(path, kAppearanceSection, L"Backdrop", kBackdropNames[static_cast<int>(appearance.backdrop)]);
    WriteSectionString(path, kAppearanceSection, L"ReduceMotion", appearance.reduceMotion ? L"1" : L"0");
    WriteSectionString(path, kAppearanceSection, L"TrayIcon", kTrayNames[static_cast<int>(appearance.tray)]);

    WriteSectionString(path, kDuckingSection, L"Enabled", settings.ducking.enabled ? L"1" : L"0");
    WriteSectionString(path, kDuckingSection, L"Amount", std::to_wstring(settings.ducking.amount));
    WriteSectionString(path, kBatterySection, L"Alert", settings.battery.enabled ? L"1" : L"0");
    WriteSectionString(path, kBatterySection, L"Threshold", std::to_wstring(settings.battery.threshold));
    SaveEqChannel(path, kGameEqSection, settings.gameEq);
    SaveEqChannel(path, kChatEqSection, settings.chatEq);
    SaveProfiles(path, settings);
    WritePrivateProfileStringW(kRoutingSection, L"Enabled", settings.routing.enabled ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(kRoutingSection, L"Game", settings.routing.game.c_str(), path.c_str());
    WritePrivateProfileStringW(kRoutingSection, L"Chat", settings.routing.chat.c_str(), path.c_str());
    WritePrivateProfileStringW(kRoutingSection, L"Output", settings.routing.output.c_str(), path.c_str());
    WritePrivateProfileStringW(kRoutingSection, L"ManageDevices", settings.routing.manageDevices ? L"1" : L"0",
                               path.c_str());
    WritePrivateProfileStringW(kRoutingSection, L"SavedDefault", settings.routing.savedDefault.c_str(), path.c_str());
    WritePrivateProfileStringW(kRoutingSection, L"SavedCommunications", settings.routing.savedCommunications.c_str(),
                               path.c_str());
    WritePrivateProfileStringW(kRoutingSection, L"AutoRoute", settings.routing.autoRoute ? L"1" : L"0", path.c_str());
    std::wstring routed;
    for (const auto& exe : settings.routing.routedApps) {
        routed += (routed.empty() ? L"" : std::wstring(1, kPathSeparator)) + exe;
    }
    WritePrivateProfileStringW(kRoutingSection, L"RoutedApps", routed.c_str(), path.c_str());
    WritePrivateProfileStringW(kIdleSection, L"Animation",
                               kIdleAnimationNames[static_cast<size_t>(settings.idleScreen.animation)], path.c_str());
    WritePrivateProfileStringW(kIdleSection, L"Delay", std::to_wstring(settings.idleScreen.delaySeconds).c_str(),
                               path.c_str());
    WritePrivateProfileStringW(kIdleSection, L"Image", settings.idleScreen.image.c_str(), path.c_str());
    WriteString(path, L"Language", settings.language == Language::Ukrainian ? L"uk" : L"en");
    for (int action = 0; action < kHotkeyCount; ++action) {
        wchar_t value[16];
        swprintf_s(value, L"0x%06X", PackHotkey(settings.hotkeys[action]));
        WriteString(path, kHotkeyKeys[action], value);
    }
    // Keys from version 1.
    WriteString(path, L"ChatApps", nullptr);
    WriteString(path, L"EngineEq", nullptr);

    // WritePrivateProfileSection takes "key=value\0" entries terminated by an extra \0.
    std::wstring section;
    for (const auto& [exe, rule] : settings.apps) {
        section += exe + L"=" + GroupName(rule.group);
        if (!rule.path.empty()) {
            section += kPathSeparator + rule.path;
        }
        section += L'\0';
    }
    section += L'\0';
    WritePrivateProfileSectionW(kAppsSection, section.c_str(), path.c_str());
}

namespace {

std::wstring HeadsetSection(uint16_t productId) {
    wchar_t section[32];
    swprintf_s(section, L"Headset.%04X", productId);
    return section;
}

}  // namespace

void LoadHeadsetState(const std::wstring& path, uint16_t productId, const HeadsetFeatures& features,
                      HeadsetState& state) {
    const std::wstring section = HeadsetSection(productId);
    for (const auto& group : features.sections) {
        for (const auto& setting : group.settings) {
            wchar_t key[16];
            swprintf_s(key, L"Setting%02X", setting.command);
            const int value = static_cast<int>(GetPrivateProfileIntW(section.c_str(), key, -1, path.c_str()));
            if (value >= 0) {
                state.values[setting.command] = static_cast<int16_t>(value);
            }
        }
    }
    const int preset = static_cast<int>(GetPrivateProfileIntW(section.c_str(), L"EqPreset", -1, path.c_str()));
    if (preset >= 0 && preset <= features.equalizer.CustomIndex()) {
        state.eqPreset = preset;
    }
    const std::wstring curve = ReadString(path, section.c_str(), L"EqCurve", L"");
    int band = 0;
    for (size_t start = 0; band < kEqBands && start < curve.size(); ++band) {
        size_t end = curve.find(L',', start);
        if (end == std::wstring::npos) {
            end = curve.size();
        }
        state.eqCurve[band] = static_cast<int8_t>(std::clamp(_wtoi(curve.substr(start, end - start).c_str()),
                                                             -static_cast<int>(features.equalizer.range),
                                                             static_cast<int>(features.equalizer.range)));
        start = end + 1;
    }
    state.eqCurveKnown = band == kEqBands;
}

void SaveHeadsetState(const std::wstring& path, uint16_t productId, const HeadsetState& state) {
    const std::wstring section = HeadsetSection(productId);
    for (size_t command = 0; command < state.values.size(); ++command) {
        if (state.values[command] >= 0) {
            wchar_t key[16];
            swprintf_s(key, L"Setting%02X", static_cast<unsigned>(command));
            WritePrivateProfileStringW(section.c_str(), key, std::to_wstring(state.values[command]).c_str(),
                                       path.c_str());
        }
    }
    if (state.eqPreset >= 0) {
        WritePrivateProfileStringW(section.c_str(), L"EqPreset", std::to_wstring(state.eqPreset).c_str(), path.c_str());
    }
    if (state.eqCurveKnown) {
        std::wstring curve;
        for (const int8_t band : state.eqCurve) {
            if (!curve.empty()) {
                curve += L',';
            }
            curve += std::to_wstring(band);
        }
        WritePrivateProfileStringW(section.c_str(), L"EqCurve", curve.c_str(), path.c_str());
    }
}

bool RemoveLegacyAutostart() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
        return false;
    }
    const bool removed = RegDeleteValueW(key, kLegacyRunValue) == ERROR_SUCCESS;
    RegCloseKey(key);
    // Explorer keeps the enabled/disabled state of Run entries separately.
    RegDeleteKeyValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run",
                       kLegacyRunValue);
    return removed;
}

bool IsAutostartEnabled() {
    return RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, RRF_RT_REG_SZ, nullptr, nullptr, nullptr) ==
           ERROR_SUCCESS;
}

bool SetAutostart(bool enable) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS) {
        return false;
    }
    LSTATUS status;
    if (enable) {
        const std::wstring command = L"\"" + ExePath() + L"\"";
        status = RegSetValueExW(key, kRunValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                                static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        status = RegDeleteValueW(key, kRunValue);
        if (status == ERROR_FILE_NOT_FOUND) {
            status = ERROR_SUCCESS;
        }
    }
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}
