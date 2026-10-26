#include "settings.h"

#include <windows.h>

#include <shlobj.h>

#include <algorithm>
#include <cstdio>
#include <cwctype>
#include <optional>
#include <iterator>
#include <vector>

namespace {

constexpr wchar_t kMainSection[] = L"ChatMix";
constexpr wchar_t kAppsSection[] = L"Apps";
constexpr wchar_t kGgSection[] = L"GGHeartbeats";
constexpr wchar_t kAddOnsSection[] = L"AddOns";
constexpr wchar_t kVisualizerSection[] = L"Visualizer";
constexpr wchar_t kRoutingSection[] = L"Routing";
constexpr wchar_t kIdleSection[] = L"IdleScreen";
constexpr wchar_t kAppearanceSection[] = L"Appearance";
constexpr wchar_t kDuckingSection[] = L"Ducking";
constexpr wchar_t kBatterySection[] = L"Battery";
constexpr wchar_t kProfilesSection[] = L"Profiles";
constexpr wchar_t kGameEqSection[] = L"Equalizer.Game";
constexpr wchar_t kChatEqSection[] = L"Equalizer.Chat";
constexpr wchar_t kMicEqSection[] = L"Equalizer.Mic";
constexpr wchar_t kMediaEqSection[] = L"Equalizer.Media";
constexpr wchar_t kAuxEqSection[] = L"Equalizer.Aux";
constexpr wchar_t kMicSection[] = L"Microphone";
constexpr wchar_t kGamePluginsSection[] = L"Plugins.Game";
constexpr wchar_t kChatPluginsSection[] = L"Plugins.Chat";
constexpr wchar_t kMediaPluginsSection[] = L"Plugins.Media";
constexpr wchar_t kAuxPluginsSection[] = L"Plugins.Aux";
constexpr wchar_t kOutputsSection[] = L"Outputs";
constexpr const wchar_t* kNoiseNames[] = {L"off", L"standard", L"strong"};
constexpr const wchar_t* kCompressorNames[] = {L"off", L"light", L"medium", L"strong"};
constexpr const wchar_t* kMicStageNames[] = {L"noise", L"eq", L"compressor", L"plugins"};  // Indexed by MicStage.
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"OpenChatMix";
constexpr wchar_t kLegacyRunValue[] = L"NovaChatMix";
constexpr wchar_t kConfigFile[] = L"OpenChatMix.ini";
constexpr wchar_t kPathSeparator = L'|';
constexpr const wchar_t* kHotkeyKeys[kHotkeyCount] = {L"HotkeyTowardGame", L"HotkeyTowardChat", L"HotkeyCenter",
                                                       L"HotkeyMuteGame", L"HotkeyMuteChat", L"HotkeyMuteMedia",
                                                       L"HotkeyMuteAux", L"HotkeyNextOutput", L"HotkeyNextMicrophone",
                                                       L"HotkeyScreensaver"};
constexpr const wchar_t* kThemeNames[] = {L"system", L"light", L"dark"};
constexpr const wchar_t* kBackdropNames[] = {L"acrylic", L"mica", L"solid"};
constexpr const wchar_t* kContrastNames[] = {L"standard", L"medium", L"high"};
constexpr const wchar_t* kTrayNames[] = {L"color", L"monochrome"};
constexpr const wchar_t* kBalancedIconNames[] = {L"bars", L"square", L"ring", L"equals", L"battery"};
constexpr const wchar_t* kBatteryTintNames[] = {L"theme", L"charge"};
// Indexed by IdleAnimationKind; the removed "bounce" reads as off.
constexpr const wchar_t* kIdleAnimationNames[] = {L"off", L"stars", L"clock", L"life", L"blank", L"image"};
constexpr const wchar_t* kIconCornerNames[] = {L"off", L"top-left", L"top-right", L"bottom-left", L"bottom-right"};

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

// [Profiles] exe=path|balance|gameEq|headsetEq|chatEq|mediaEq|auxEq; an empty field leaves that unchanged.
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
        fields.resize(7);
        profile.path = fields[0];
        // Percent with a "%" sign; a bare number is a dial step of earlier versions.
        if (!fields[1].empty()) {
            const int number = _wtoi(fields[1].c_str());
            profile.balance = fields[1].back() == L'%' ? std::clamp(number, -kBalanceRange, kBalanceRange)
                                                       : BalanceAt(number, kBalanceSteps);
        }
        profile.gameEq = fields[2];
        profile.headsetEq = fields[3].empty() ? -1 : _wtoi(fields[3].c_str());
        profile.chatEq = fields[4];
        profile.mediaEq = fields[5];
        profile.auxEq = fields[6];
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
                   kPathSeparator + (profile.headsetEq >= 0 ? std::to_wstring(profile.headsetEq) : L"") +
                   kPathSeparator + profile.chatEq + kPathSeparator + profile.mediaEq + kPathSeparator + profile.auxEq;
        section += L'\0';
    }
    section += L'\0';
    WritePrivateProfileSectionW(kProfilesSection, section.c_str(), path.c_str());
}

// Plugin1=enabled|id|path, numbered from 1 without gaps.
void LoadPlugins(const std::wstring& path, const wchar_t* section, std::vector<ChainPlugin>& plugins) {
    for (int index = 1;; ++index) {
        const std::wstring key = L"Plugin" + std::to_wstring(index);
        const std::wstring value = ReadString(path, section, key.c_str(), L"");
        const size_t first = value.find(kPathSeparator);
        const size_t second = first == std::wstring::npos ? std::wstring::npos : value.find(kPathSeparator, first + 1);
        if (second == std::wstring::npos) {
            break;
        }
        ChainPlugin plugin;
        plugin.enabled = value.substr(0, first) != L"0";
        plugin.id = value.substr(first + 1, second - first - 1);
        plugin.path = value.substr(second + 1);
        if (!plugin.path.empty() && !plugin.id.empty()) {
            plugins.push_back(std::move(plugin));
        }
    }
}

// Appends the Plugin keys to a whole-section string.
void AddPlugins(std::wstring& section, const std::vector<ChainPlugin>& plugins) {
    for (size_t index = 0; index < plugins.size(); ++index) {
        const ChainPlugin& plugin = plugins[index];
        section += L"Plugin" + std::to_wstring(index + 1) + L"=" + (plugin.enabled ? L"1" : L"0") + kPathSeparator +
                   plugin.id + kPathSeparator + plugin.path;
        section += L'\0';
    }
}

// A channel's plugins as a section of their own, written whole so removed plugins do not linger; no section while
// there are none.
void SavePlugins(const std::wstring& path, const wchar_t* section, const std::vector<ChainPlugin>& plugins) {
    if (plugins.empty()) {
        WritePrivateProfileStringW(section, nullptr, nullptr, path.c_str());
        return;
    }
    std::wstring text;
    AddPlugins(text, plugins);
    text += L'\0';
    WritePrivateProfileSectionW(section, text.c_str(), path.c_str());
}

// [Microphone] Plugin1=enabled|id|path
void LoadMic(const std::wstring& path, MicSettings& mic) {
    mic.enabled = ReadSectionInt(path, kMicSection, L"Enabled", 0) != 0;
    mic.input = ReadString(path, kMicSection, L"Input", L"");
    mic.output = ReadString(path, kMicSection, L"Output", L"");
    mic.keepLevels = ReadSectionInt(path, kMicSection, L"KeepLevels", 1) != 0;
    mic.noise = ParseName(ReadString(path, kMicSection, L"NoiseSuppression", L"standard"), kNoiseNames,
                          NoiseSuppression::Standard);
    mic.compressor = ParseName(ReadString(path, kMicSection, L"Compressor", L"off"), kCompressorNames, MicCompressor::Off);
    mic.monitor = ReadSectionInt(path, kMicSection, L"Monitor", 0) != 0;
    // "noise,eq,compressor,plugins"; anything but each step exactly once keeps the default order.
    const std::wstring order = ReadString(path, kMicSection, L"Order", L"");
    MicStageOrder parsed{};
    int count = 0;
    bool seen[kMicStageCount] = {};
    for (size_t start = 0; start <= order.size() && count < kMicStageCount;) {
        const size_t end = std::min(order.find(L',', start), order.size());
        const std::wstring name = Trim(order.substr(start, end - start));
        for (int stage = 0; stage < kMicStageCount; ++stage) {
            if (_wcsicmp(name.c_str(), kMicStageNames[stage]) == 0 && !seen[stage]) {
                seen[stage] = true;
                parsed[count++] = static_cast<MicStage>(stage);
            }
        }
        start = end + 1;
    }
    if (count == kMicStageCount) {
        mic.order = parsed;
    }
    LoadPlugins(path, kMicSection, mic.plugins);
}

void SaveMic(const std::wstring& path, const MicSettings& mic) {
    // The section is written whole, so removed plugins do not linger.
    std::wstring section;
    auto add = [&](const std::wstring& key, const std::wstring& value) {
        section += key + L"=" + value;
        section += L'\0';
    };
    add(L"Enabled", mic.enabled ? L"1" : L"0");
    add(L"Input", mic.input);
    add(L"Output", mic.output);
    add(L"KeepLevels", mic.keepLevels ? L"1" : L"0");
    add(L"NoiseSuppression", kNoiseNames[static_cast<int>(mic.noise)]);
    add(L"Compressor", kCompressorNames[static_cast<int>(mic.compressor)]);
    add(L"Monitor", mic.monitor ? L"1" : L"0");
    std::wstring order;
    for (const MicStage stage : mic.order) {
        order += (order.empty() ? L"" : L",") + std::wstring(kMicStageNames[static_cast<int>(stage)]);
    }
    add(L"Order", order);
    AddPlugins(section, mic.plugins);
    section += L'\0';
    WritePrivateProfileSectionW(kMicSection, section.c_str(), path.c_str());
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
    case AppGroup::Aux: return L"aux";
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
    if (_wcsicmp(text.c_str(), L"aux") == 0) {
        return AppGroup::Aux;
    }
    return AppGroup::Game;
}

void LoadGgHeartbeats(const std::wstring& path, Settings& settings) {
    std::vector<wchar_t> buffer(4096, L'\0');
    const DWORD length =
        GetPrivateProfileSectionW(kGgSection, buffer.data(), static_cast<DWORD>(buffer.size()), path.c_str());
    for (size_t offset = 0; offset < length;) {
        const std::wstring entry(buffer.data() + offset);
        offset += entry.size() + 1;
        const size_t equals = entry.find(L'=');
        if (equals == std::wstring::npos) {
            continue;
        }
        // Sizes separated by commas.
        std::vector<unsigned long long> sizes;
        for (const wchar_t* at = entry.c_str() + equals + 1; *at;) {
            wchar_t* end = nullptr;
            const unsigned long long bytes = wcstoull(at, &end, 10);
            if (end == at) {
                break;
            }
            if (bytes) {
                sizes.push_back(bytes);
            }
            at = *end == L',' ? end + 1 : end;
        }
        if (!sizes.empty()) {
            settings.ggHeartbeats[Trim(entry.substr(0, equals))] = sizes;
        }
    }
}

// Add-on option values may hold any text; characters the [AddOns] lines use as separators are written as %XX.
std::wstring EscapeOption(const std::wstring& value) {
    std::wstring escaped;
    for (const wchar_t ch : value) {
        if (ch < 0x20 || ch == L'%' || ch == L';' || ch == L'|' || ch == L'=') {
            wchar_t code[4];
            swprintf_s(code, L"%%%02X", static_cast<unsigned>(ch));
            escaped += code;
        } else {
            escaped += ch;
        }
    }
    return escaped;
}

std::wstring UnescapeOption(const std::wstring& value) {
    std::wstring text;
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == L'%' && i + 2 < value.size() && iswxdigit(value[i + 1]) && iswxdigit(value[i + 2])) {
            text += static_cast<wchar_t>(wcstoul(value.substr(i + 1, 2).c_str(), nullptr, 16));
            i += 2;
        } else {
            text += value[i];
        }
    }
    return text;
}

// An event screen as one option value: its settings, then each display, separated by control characters.
constexpr wchar_t kRecordSeparator = 0x1E;
constexpr wchar_t kFieldSeparator = 0x1F;
constexpr wchar_t kScreenPrefix[] = L"screen.";

std::vector<std::wstring> SplitOn(const std::wstring& text, wchar_t separator) {
    std::vector<std::wstring> parts;
    size_t start = 0;
    for (size_t at = text.find(separator); at != std::wstring::npos; at = text.find(separator, start)) {
        parts.push_back(text.substr(start, at - start));
        start = at + 1;
    }
    parts.push_back(text.substr(start));
    return parts;
}

std::wstring EncodeScreen(const AddOnEventScreen& screen) {
    std::wstring value = std::wstring(screen.enabled ? L"1" : L"0") + kRecordSeparator + (screen.whileOn ? L"w" : L"t") +
                         kRecordSeparator + std::to_wstring(screen.times);
    for (const AddOnDisplay& display : screen.displays) {
        value += kRecordSeparator + std::to_wstring(display.icon) + kFieldSeparator +
                 std::to_wstring(display.lengthMs) + kFieldSeparator + display.heading + kFieldSeparator +
                 display.text + kFieldSeparator + display.image;
    }
    return value;
}

std::optional<AddOnEventScreen> DecodeScreen(const std::wstring& value) {
    const std::vector<std::wstring> records = SplitOn(value, kRecordSeparator);
    if (records.size() < 3) {
        return std::nullopt;
    }
    AddOnEventScreen screen;
    screen.enabled = records[0] != L"0";
    screen.whileOn = records[1] == L"w";
    screen.times = std::clamp(_wtoi(records[2].c_str()), 1, 99);
    for (size_t i = 3; i < records.size(); ++i) {
        const std::vector<std::wstring> fields = SplitOn(records[i], kFieldSeparator);
        if (fields.size() < 5) {
            continue;
        }
        AddOnDisplay display;
        display.icon = std::max(0, _wtoi(fields[0].c_str()));
        display.lengthMs = std::clamp(_wtoi(fields[1].c_str()), 500, 60000);
        display.heading = fields[2];
        display.text = fields[3];
        display.image = fields[4];
        screen.displays.push_back(std::move(display));
    }
    return screen;
}

// [AddOns] holds one add-on per numbered key: "enabled|path|key=value;key=value", values escaped.
void LoadAddOns(const std::wstring& path, Settings& settings) {
    std::vector<wchar_t> buffer(32768, L'\0');
    const DWORD length =
        GetPrivateProfileSectionW(kAddOnsSection, buffer.data(), static_cast<DWORD>(buffer.size()), path.c_str());
    for (size_t offset = 0; offset < length;) {
        const std::wstring entry(buffer.data() + offset);
        offset += entry.size() + 1;
        const size_t equals = entry.find(L'=');
        if (equals == std::wstring::npos) {
            continue;
        }
        const std::wstring value = entry.substr(equals + 1);
        const size_t first = value.find(kPathSeparator);
        if (first == std::wstring::npos) {
            continue;
        }
        const size_t second = value.find(kPathSeparator, first + 1);
        AddOnSettings addOn;
        addOn.enabled = value.substr(0, first) != L"0";
        addOn.path = Trim(value.substr(first + 1, second == std::wstring::npos ? std::wstring::npos : second - first - 1));
        if (second != std::wstring::npos) {
            const std::wstring options = value.substr(second + 1);
            for (size_t start = 0; start < options.size();) {
                size_t end = options.find(L';', start);
                if (end == std::wstring::npos) {
                    end = options.size();
                }
                const std::wstring pair = options.substr(start, end - start);
                const size_t at = pair.find(L'=');
                if (at != std::wstring::npos && at > 0) {
                    const std::wstring key = pair.substr(0, at);
                    const std::wstring option = UnescapeOption(pair.substr(at + 1));
                    if (key.starts_with(kScreenPrefix)) {
                        if (std::optional<AddOnEventScreen> screen = DecodeScreen(option)) {
                            addOn.screens[key.substr(std::size(kScreenPrefix) - 1)] = std::move(*screen);
                        }
                    } else {
                        addOn.options[key] = option;
                    }
                }
                start = end + 1;
            }
        }
        if (!addOn.path.empty()) {
            settings.addOns.push_back(std::move(addOn));
        }
    }
}

void SaveAddOns(const std::wstring& path, const Settings& settings) {
    std::wstring section;
    int index = 0;
    for (const AddOnSettings& addOn : settings.addOns) {
        std::wstring options;
        for (const auto& [key, value] : addOn.options) {
            options += (options.empty() ? L"" : L";") + key + L"=" + EscapeOption(value);
        }
        for (const auto& [event, screen] : addOn.screens) {
            options += (options.empty() ? L"" : L";") + std::wstring(kScreenPrefix) + event + L"=" +
                       EscapeOption(EncodeScreen(screen));
        }
        section += std::to_wstring(++index) + L"=" + (addOn.enabled ? L"1" : L"0") + kPathSeparator + addOn.path +
                   kPathSeparator + options;
        section.push_back(L'\0');
    }
    section.push_back(L'\0');
    WritePrivateProfileSectionW(kAddOnsSection, section.c_str(), path.c_str());
}

void SaveGgHeartbeats(const std::wstring& path, const Settings& settings) {
    std::wstring section;
    for (const auto& [exe, sizes] : settings.ggHeartbeats) {
        std::wstring list;
        for (const unsigned long long bytes : sizes) {
            list += (list.empty() ? L"" : L",") + std::to_wstring(bytes);
        }
        section += exe + L"=" + list;
        section.push_back(L'\0');
    }
    section.push_back(L'\0');
    WritePrivateProfileSectionW(kGgSection, section.c_str(), path.c_str());
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
    if ((group == AppGroup::Media && !MediaAvailable()) || (group == AppGroup::Aux && !AuxAvailable())) {
        return AppGroup::Game;
    }
    return group;
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

EqChannelSettings& EqStateOf(Settings& settings, EqChannel channel) {
    switch (channel) {
    case EqChannel::Chat: return settings.chatEq;
    case EqChannel::Mic: return settings.micEq;
    case EqChannel::Media: return settings.mediaEq;
    case EqChannel::Aux: return settings.auxEq;
    case EqChannel::Game: break;
    }
    return settings.gameEq;
}

const EqChannelSettings& EqStateOf(const Settings& settings, EqChannel channel) {
    return EqStateOf(const_cast<Settings&>(settings), channel);
}

std::vector<ChainPlugin>& PluginsOf(Settings& settings, EqChannel channel) {
    switch (channel) {
    case EqChannel::Chat: return settings.chatPlugins;
    case EqChannel::Mic: return settings.mic.plugins;
    case EqChannel::Media: return settings.mediaPlugins;
    case EqChannel::Aux: return settings.auxPlugins;
    case EqChannel::Game: break;
    }
    return settings.gamePlugins;
}

const std::vector<ChainPlugin>& PluginsOf(const Settings& settings, EqChannel channel) {
    return PluginsOf(const_cast<Settings&>(settings), channel);
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

void SaveLogSetting(const std::wstring& path, bool log) {
    WriteString(path, L"Log", log ? L"1" : L"0");
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

// Apps that start in the Chat group, as SteelSeries GG puts them. A settings file written before an
// app joined this list gets it once (ChatDefaults counts how many were known when it was saved).
// Their rules are kept even when the app isn't installed, so it lands in Chat when it first runs.
constexpr const wchar_t* kChatApps[] = {L"Discord.exe", L"XboxPartyChatHost.exe"};
constexpr int kChatAppsKnown = static_cast<int>(std::size(kChatApps));

bool IsDefaultChatApp(const std::wstring& exe) {
    return std::any_of(std::begin(kChatApps), std::end(kChatApps),
                       [&](const wchar_t* app) { return _wcsicmp(app, exe.c_str()) == 0; });
}

Settings LoadSettings(const std::wstring& path) {
    Settings settings;
    settings.firstRun = GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES;
    for (int action = 0; action < kHotkeyCount; ++action) {
        settings.hotkeys[action] = DefaultHotkey(action);
    }
    if (settings.firstRun) {
        for (const wchar_t* exe : kChatApps) {
            settings.SetGroup(exe, AppGroup::Chat, L"");
        }
        return settings;
    }

    LoadApps(path, settings);
    LoadGgHeartbeats(path, settings);
    LoadAddOns(path, settings);
    // Chat apps added to the defaults since the file was saved; one removed by hand stays removed.
    const int chatAppsKnown = ReadInt(path, L"ChatDefaults", 1);
    for (int index = std::max(chatAppsKnown, 0); index < kChatAppsKnown; ++index) {
        if (!settings.apps.contains(kChatApps[index])) {
            settings.SetGroup(kChatApps[index], AppGroup::Chat, L"");
        }
    }
    settings.levels = ClampLevels(ReadInt(path, L"Game", 100), ReadInt(path, L"Chat", 100));
    settings.log = ReadInt(path, L"Log", 0) != 0;
    settings.checkUpdates = ReadInt(path, L"CheckUpdates", 1) != 0;
    settings.lastUpdateCheck = _wtoi64(ReadString(path, kMainSection, L"LastUpdateCheck", L"0").c_str());
    settings.showPopup = ReadInt(path, L"ShowPopup", 1) != 0;
    settings.dialPopup = ReadInt(path, L"DialPopup", 1) != 0;
    settings.volumePopup = ReadInt(path, L"VolumePopup", 1) != 0;
    settings.knobPopup = ReadInt(path, L"KnobPopup", 1) != 0;
    settings.noisePopup = ReadInt(path, L"NoisePopup", 1) != 0;
    settings.noiseScreen = ReadInt(path, L"NoiseScreen", 1) != 0;
    settings.popups = ReadInt(path, L"Popups", 1) != 0;
    settings.popupCorner = static_cast<PopupCorner>(std::clamp(ReadInt(path, L"PopupCorner", 0), 0, 3));
    settings.gameVolume = std::clamp(ReadInt(path, L"GameVolume", 100), 0, 100);
    settings.chatVolume = std::clamp(ReadInt(path, L"ChatVolume", 100), 0, 100);
    settings.mediaVolume = std::clamp(ReadInt(path, L"MediaVolume", 100), 0, 100);
    settings.auxVolume = std::clamp(ReadInt(path, L"AuxVolume", 100), 0, 100);
    settings.warnGg = ReadInt(path, L"WarnGg", 1) != 0;
    // Media and the equalizer start turned off; settings files from earlier versions keep their choice.
    settings.mediaGroup = ReadInt(path, L"MediaGroup", 0) != 0;
    settings.auxGroup = ReadInt(path, L"AuxGroup", 0) != 0;
    settings.equalizer = ReadInt(path, L"Equalizer", 0) != 0;

    AppearanceSettings& appearance = settings.appearance;
    appearance.theme = ParseName(ReadString(path, kAppearanceSection, L"Theme", L"system"), kThemeNames,
                                 ThemeMode::System);
    const std::wstring accent = ReadString(path, kAppearanceSection, L"Accent", L"");
    // The Windows accent is the default; a saved color replaces it.
    appearance.windowsAccent = accent.empty() || _wcsicmp(accent.c_str(), L"windows") == 0;
    if (accent.size() == 7 && accent[0] == L'#') {
        appearance.accent = static_cast<uint32_t>(wcstoul(accent.c_str() + 1, nullptr, 16)) & 0xFFFFFF;
    }
    const std::wstring chatAccent = ReadString(path, kAppearanceSection, L"ChatColor", L"auto");
    if (chatAccent.size() == 7 && chatAccent[0] == L'#') {
        appearance.chatAccent = static_cast<uint32_t>(wcstoul(chatAccent.c_str() + 1, nullptr, 16)) & 0xFFFFFF;
    }
    // Version 1 had a single translucency switch.
    const bool translucent = ReadInt(path, L"Translucent", 1) != 0;
    appearance.backdrop = ParseName(ReadString(path, kAppearanceSection, L"Backdrop", translucent ? L"acrylic" : L"solid"),
                                    kBackdropNames, Backdrop::Acrylic);
    appearance.contrast = ParseName(ReadString(path, kAppearanceSection, L"Contrast", L"standard"), kContrastNames,
                                    ContrastLevel::Standard);
    appearance.reduceMotion = ReadSectionInt(path, kAppearanceSection, L"ReduceMotion", 0) != 0;
    appearance.tray = ParseName(ReadString(path, kAppearanceSection, L"TrayIcon", L"color"), kTrayNames, TrayStyle::Color);
    appearance.balancedIcon = ParseName(ReadString(path, kAppearanceSection, L"TrayBalanced", L"bars"), kBalancedIconNames,
                                        BalancedIcon::Bars);
    appearance.battery = ParseName(ReadString(path, kAppearanceSection, L"Battery", L"theme"), kBatteryTintNames,
                                   BatteryTint::Theme);
    appearance.trayBattery = ParseName(ReadString(path, kAppearanceSection, L"TrayBattery", L"theme"), kBatteryTintNames,
                                       BatteryTint::Theme);

    settings.ducking.enabled = ReadSectionInt(path, kDuckingSection, L"Enabled", 0) != 0;
    settings.ducking.amount = std::clamp(ReadSectionInt(path, kDuckingSection, L"Amount", 50), 10, 90);
    settings.battery.enabled = ReadSectionInt(path, kBatterySection, L"Alert", 1) != 0;
    settings.battery.threshold = std::clamp(ReadSectionInt(path, kBatterySection, L"Threshold", 15), 5, 50);
    LoadEqChannel(path, kGameEqSection, settings.gameEq);
    LoadEqChannel(path, kChatEqSection, settings.chatEq);
    LoadEqChannel(path, kMicEqSection, settings.micEq);
    LoadEqChannel(path, kMediaEqSection, settings.mediaEq);
    LoadEqChannel(path, kAuxEqSection, settings.auxEq);
    LoadPlugins(path, kGamePluginsSection, settings.gamePlugins);
    LoadPlugins(path, kChatPluginsSection, settings.chatPlugins);
    LoadPlugins(path, kMediaPluginsSection, settings.mediaPlugins);
    LoadPlugins(path, kAuxPluginsSection, settings.auxPlugins);
    LoadMic(path, settings.mic);
    LoadProfiles(path, settings);
    settings.routing.enabled = GetPrivateProfileIntW(kRoutingSection, L"Enabled", 0, path.c_str()) != 0;
    settings.routing.game = ReadString(path, kRoutingSection, L"Game", L"");
    settings.routing.chat = ReadString(path, kRoutingSection, L"Chat", L"");
    settings.routing.output = ReadString(path, kRoutingSection, L"Output", L"");
    settings.routing.media = ReadString(path, kRoutingSection, L"Media", L"");
    settings.routing.aux = ReadString(path, kRoutingSection, L"Aux", L"");
    settings.routing.gameOutput = ReadString(path, kRoutingSection, L"GameOutput", L"");
    settings.routing.chatOutput = ReadString(path, kRoutingSection, L"ChatOutput", L"");
    settings.routing.mediaOutput = ReadString(path, kRoutingSection, L"MediaOutput", L"");
    settings.routing.auxOutput = ReadString(path, kRoutingSection, L"AuxOutput", L"");
    settings.routing.manageDevices = GetPrivateProfileIntW(kRoutingSection, L"ManageDevices", 1, path.c_str()) != 0;
    settings.routing.savedDefault = ReadString(path, kRoutingSection, L"SavedDefault", L"");
    settings.routing.savedCommunications = ReadString(path, kRoutingSection, L"SavedCommunications", L"");
    settings.routing.autoRoute = GetPrivateProfileIntW(kRoutingSection, L"AutoRoute", 1, path.c_str()) != 0;
    settings.routing.routedApps = SplitList(ReadString(path, kRoutingSection, L"RoutedApps", L""));
    for (const std::wstring& exe : SplitList(ReadString(path, kMainSection, L"IgnoredApps", L""))) {
        settings.ignoredApps.insert(exe);
    }
    settings.outputs.order = SplitList(ReadString(path, kOutputsSection, L"Order", L""));
    settings.outputs.excluded = SplitList(ReadString(path, kOutputsSection, L"Excluded", L""));
    settings.outputs.switchOnLoss = ReadSectionInt(path, kOutputsSection, L"SwitchOnLoss", 0) != 0;
    settings.outputs.returnToPreferred = ReadSectionInt(path, kOutputsSection, L"ReturnToPreferred", 0) != 0;
    settings.idleScreen.animation = ParseIdleAnimation(ReadString(path, kIdleSection, L"Animation", L"off"));
    settings.idleScreen.delaySeconds =
        std::clamp(static_cast<int>(GetPrivateProfileIntW(kIdleSection, L"Delay", 60, path.c_str())), 5, 3600);
    settings.idleScreen.image = ReadString(path, kIdleSection, L"Image", L"");
    settings.visualizer.enabled = GetPrivateProfileIntW(kVisualizerSection, L"Enabled", 0, path.c_str()) != 0;
    settings.visualizer.mediaOnly = _wcsicmp(ReadString(path, kVisualizerSection, L"For", L"media").c_str(), L"sound") != 0;
    const std::wstring corner = ReadString(path, kIdleSection, L"BluetoothIcon", L"top-right");
    for (size_t i = 0; i < std::size(kIconCornerNames); ++i) {
        if (_wcsicmp(corner.c_str(), kIconCornerNames[i]) == 0) {
            settings.idleScreen.bluetoothIcon = static_cast<IconCorner>(i);
        }
    }
    settings.language =
        _wcsicmp(ReadString(path, kMainSection, L"Language", L"en").c_str(), L"uk") == 0 ? Language::Ukrainian
                                                                                          : Language::English;
    const UINT wheel = static_cast<UINT>(wcstoul(ReadString(path, kMainSection, L"WheelModifiers", L"0").c_str(), nullptr, 0));
    if (std::find(std::begin(kWheelModifierChoices), std::end(kWheelModifierChoices), wheel) != std::end(kWheelModifierChoices)) {
        settings.wheelModifiers = wheel;
    }
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
    // Log is set by hand or by the switch in Settings, which writes it at once; an edit made in this file while
    // OpenChatMix runs stays.
    if (ReadString(path, kMainSection, L"Log", L"").empty()) {
        WriteString(path, L"Log", settings.log ? L"1" : L"0");
    }
    WriteString(path, L"CheckUpdates", settings.checkUpdates ? L"1" : L"0");
    WriteString(path, L"LastUpdateCheck", std::to_wstring(settings.lastUpdateCheck).c_str());
    WriteString(path, L"ShowPopup", settings.showPopup ? L"1" : L"0");
    WriteString(path, L"ChatDefaults", std::to_wstring(kChatAppsKnown).c_str());
    WriteString(path, L"DialPopup", settings.dialPopup ? L"1" : L"0");
    WriteString(path, L"VolumePopup", settings.volumePopup ? L"1" : L"0");
    WriteString(path, L"KnobPopup", settings.knobPopup ? L"1" : L"0");
    WriteString(path, L"NoisePopup", settings.noisePopup ? L"1" : L"0");
    WriteString(path, L"NoiseScreen", settings.noiseScreen ? L"1" : L"0");
    WriteString(path, L"Popups", settings.popups ? L"1" : L"0");
    WriteString(path, L"PopupCorner", std::to_wstring(static_cast<int>(settings.popupCorner)).c_str());
    WriteString(path, L"GameVolume", std::to_wstring(settings.gameVolume).c_str());
    WriteString(path, L"ChatVolume", std::to_wstring(settings.chatVolume).c_str());
    WriteString(path, L"MediaVolume", std::to_wstring(settings.mediaVolume).c_str());
    WriteString(path, L"AuxVolume", std::to_wstring(settings.auxVolume).c_str());
    WriteString(path, L"WarnGg", settings.warnGg ? L"1" : L"0");
    WriteString(path, L"MediaGroup", settings.mediaGroup ? L"1" : L"0");
    WriteString(path, L"AuxGroup", settings.auxGroup ? L"1" : L"0");
    WriteString(path, L"Equalizer", settings.equalizer ? L"1" : L"0");
    WriteString(path, L"Translucent", nullptr);

    const AppearanceSettings& appearance = settings.appearance;
    WriteSectionString(path, kAppearanceSection, L"Theme", kThemeNames[static_cast<int>(appearance.theme)]);
    wchar_t accent[16];
    swprintf_s(accent, L"#%06X", appearance.accent & 0xFFFFFF);
    WriteSectionString(path, kAppearanceSection, L"Accent", appearance.windowsAccent ? L"windows" : accent);
    wchar_t chatAccent[16] = L"auto";
    if (appearance.chatAccent) {
        swprintf_s(chatAccent, L"#%06X", *appearance.chatAccent & 0xFFFFFF);
    }
    WriteSectionString(path, kAppearanceSection, L"ChatColor", chatAccent);
    WriteSectionString(path, kAppearanceSection, L"Backdrop", kBackdropNames[static_cast<int>(appearance.backdrop)]);
    WriteSectionString(path, kAppearanceSection, L"Contrast", kContrastNames[static_cast<int>(appearance.contrast)]);
    WriteSectionString(path, kAppearanceSection, L"ReduceMotion", appearance.reduceMotion ? L"1" : L"0");
    WriteSectionString(path, kAppearanceSection, L"TrayIcon", kTrayNames[static_cast<int>(appearance.tray)]);
    WriteSectionString(path, kAppearanceSection, L"TrayBalanced",
                       kBalancedIconNames[static_cast<int>(appearance.balancedIcon)]);
    WriteSectionString(path, kAppearanceSection, L"Battery", kBatteryTintNames[static_cast<int>(appearance.battery)]);
    WriteSectionString(path, kAppearanceSection, L"TrayBattery",
                       kBatteryTintNames[static_cast<int>(appearance.trayBattery)]);

    WriteSectionString(path, kDuckingSection, L"Enabled", settings.ducking.enabled ? L"1" : L"0");
    WriteSectionString(path, kDuckingSection, L"Amount", std::to_wstring(settings.ducking.amount));
    WriteSectionString(path, kBatterySection, L"Alert", settings.battery.enabled ? L"1" : L"0");
    WriteSectionString(path, kBatterySection, L"Threshold", std::to_wstring(settings.battery.threshold));
    SaveEqChannel(path, kGameEqSection, settings.gameEq);
    SaveEqChannel(path, kChatEqSection, settings.chatEq);
    SaveEqChannel(path, kMicEqSection, settings.micEq);
    SaveEqChannel(path, kMediaEqSection, settings.mediaEq);
    SaveEqChannel(path, kAuxEqSection, settings.auxEq);
    SavePlugins(path, kGamePluginsSection, settings.gamePlugins);
    SavePlugins(path, kChatPluginsSection, settings.chatPlugins);
    SavePlugins(path, kMediaPluginsSection, settings.mediaPlugins);
    SavePlugins(path, kAuxPluginsSection, settings.auxPlugins);
    SaveMic(path, settings.mic);
    SaveProfiles(path, settings);
    WritePrivateProfileStringW(kRoutingSection, L"Enabled", settings.routing.enabled ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(kRoutingSection, L"Game", settings.routing.game.c_str(), path.c_str());
    WritePrivateProfileStringW(kRoutingSection, L"Chat", settings.routing.chat.c_str(), path.c_str());
    WritePrivateProfileStringW(kRoutingSection, L"Output", settings.routing.output.c_str(), path.c_str());
    WritePrivateProfileStringW(kRoutingSection, L"Media", settings.routing.media.c_str(), path.c_str());
    WritePrivateProfileStringW(kRoutingSection, L"Aux", settings.routing.aux.c_str(), path.c_str());
    WritePrivateProfileStringW(kRoutingSection, L"GameOutput", settings.routing.gameOutput.c_str(), path.c_str());
    WritePrivateProfileStringW(kRoutingSection, L"ChatOutput", settings.routing.chatOutput.c_str(), path.c_str());
    WritePrivateProfileStringW(kRoutingSection, L"MediaOutput", settings.routing.mediaOutput.c_str(), path.c_str());
    WritePrivateProfileStringW(kRoutingSection, L"AuxOutput", settings.routing.auxOutput.c_str(), path.c_str());
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
    std::wstring ignored;
    for (const auto& exe : settings.ignoredApps) {
        ignored += (ignored.empty() ? L"" : std::wstring(1, kPathSeparator)) + exe;
    }
    WriteString(path, L"IgnoredApps", ignored.c_str());
    std::wstring order;
    for (const auto& id : settings.outputs.order) {
        order += (order.empty() ? L"" : std::wstring(1, kPathSeparator)) + id;
    }
    WriteSectionString(path, kOutputsSection, L"Order", order);
    std::wstring excluded;
    for (const auto& id : settings.outputs.excluded) {
        excluded += (excluded.empty() ? L"" : std::wstring(1, kPathSeparator)) + id;
    }
    WriteSectionString(path, kOutputsSection, L"Excluded", excluded);
    WriteSectionString(path, kOutputsSection, L"SwitchOnLoss", settings.outputs.switchOnLoss ? L"1" : L"0");
    WriteSectionString(path, kOutputsSection, L"ReturnToPreferred", settings.outputs.returnToPreferred ? L"1" : L"0");
    WritePrivateProfileStringW(kIdleSection, L"Animation",
                               kIdleAnimationNames[static_cast<size_t>(settings.idleScreen.animation)], path.c_str());
    WritePrivateProfileStringW(kIdleSection, L"Delay", std::to_wstring(settings.idleScreen.delaySeconds).c_str(),
                               path.c_str());
    WritePrivateProfileStringW(kIdleSection, L"Image", settings.idleScreen.image.c_str(), path.c_str());
    WritePrivateProfileStringW(kVisualizerSection, L"Enabled", settings.visualizer.enabled ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(kVisualizerSection, L"For", settings.visualizer.mediaOnly ? L"media" : L"sound",
                               path.c_str());
    WritePrivateProfileStringW(kIdleSection, L"BluetoothIcon",
                               kIconCornerNames[static_cast<size_t>(settings.idleScreen.bluetoothIcon)], path.c_str());
    WriteString(path, L"Language", settings.language == Language::Ukrainian ? L"uk" : L"en");
    for (int action = 0; action < kHotkeyCount; ++action) {
        wchar_t value[16];
        swprintf_s(value, L"0x%06X", PackHotkey(settings.hotkeys[action]));
        WriteString(path, kHotkeyKeys[action], value);
    }
    WriteString(path, L"WheelModifiers", std::to_wstring(settings.wheelModifiers).c_str());
    // Keys from version 1, and the removed equalizer preset hotkey.
    WriteString(path, L"HotkeyNextPreset", nullptr);
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
    SaveGgHeartbeats(path, settings);
    SaveAddOns(path, settings);
}

namespace {

std::wstring HeadsetSection(uint16_t productId) {
    wchar_t section[32];
    swprintf_s(section, L"Headset.%04X", productId);
    return section;
}

}  // namespace

// Ini keys of the extra equalizers, by ExtraEqualizer. The microphone's preset is kept as its onboard slot, which
// stays put while GG's list grows.
struct ExtraEqKeys {
    const wchar_t* preset;
    const wchar_t* curve;
    bool slot;  // preset holds the onboard slot instead of the list index.
};
constexpr ExtraEqKeys kExtraEqKeys[kExtraEqualizers] = {{L"BtEqPreset", L"BtEqCurve", false},
                                                        {L"MicEqSlot", L"MicEqCurve", true}};
// Up to 2.6.1 MicEqPreset held the index in a list of five presets, by slot here; index 5 was the custom setting.
constexpr wchar_t kLegacyMicPresetKey[] = L"MicEqPreset";
constexpr int kLegacyMicSlots[] = {0, 1, 2, 5, 6};

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

    // A parametric equalizer keeps its filters; one saved before it had them starts from its curve.
    if (features.equalizer.Parametric()) {
        const std::wstring filters = ReadString(path, section.c_str(), L"EqFilters", L"");
        ParametricEq read;
        int count = 0;
        for (size_t start = 0; count < kEqBands && start < filters.size(); ++count) {
            size_t end = filters.find(L';', start);
            if (end == std::wstring::npos) {
                end = filters.size();
            }
            const std::optional<EqBand> parsed = ParseBand(filters.substr(start, end - start));
            if (!parsed) {
                break;
            }
            read.bands[count] = *parsed;
            start = end + 1;
        }
        if (count == kEqBands) {
            state.eqFilters = PresetFilters(read, features.equalizer.notch);
            state.eqFiltersKnown = true;
        } else if (state.eqCurveKnown) {
            state.eqFilters = CurveFilters(state.eqCurve, features.equalizer.range);
            state.eqFiltersKnown = true;
        }
    }

    // The extra equalizers, as the main one.
    for (int index = 0; index < kExtraEqualizers; ++index) {
        const EqualizerSpec& extra = features.Extra(static_cast<ExtraEqualizer>(index));
        if (!extra.bandsCommand) {
            continue;
        }
        HeadsetState::ExtraEq& target = state.extraEq[index];
        const ExtraEqKeys& keys = kExtraEqKeys[index];
        const int stored = static_cast<int>(GetPrivateProfileIntW(section.c_str(), keys.preset, -1, path.c_str()));
        int extraPreset = keys.slot && stored >= 0 ? extra.IndexOfSlot(stored) : stored;
        if (keys.slot && stored < 0) {
            const int legacy = static_cast<int>(
                GetPrivateProfileIntW(section.c_str(), kLegacyMicPresetKey, -1, path.c_str()));
            const int legacyCount = static_cast<int>(std::size(kLegacyMicSlots));
            extraPreset = legacy >= 0 && legacy < legacyCount ? extra.IndexOfSlot(kLegacyMicSlots[legacy])
                          : legacy == legacyCount              ? extra.CustomIndex()
                                                               : -1;
        }
        if (extraPreset >= 0 && extraPreset <= extra.CustomIndex()) {
            target.preset = extraPreset;
        }
        const std::wstring extraCurve = ReadString(path, section.c_str(), kExtraEqKeys[index].curve, L"");
        int extraBand = 0;
        for (size_t start = 0; extraBand < kEqBands && start < extraCurve.size(); ++extraBand) {
            size_t end = extraCurve.find(L',', start);
            if (end == std::wstring::npos) {
                end = extraCurve.size();
            }
            target.curve[extraBand] = static_cast<int8_t>(std::clamp(
                _wtoi(extraCurve.substr(start, end - start).c_str()), -static_cast<int>(extra.range),
                static_cast<int>(extra.range)));
            start = end + 1;
        }
        target.curveKnown = extraBand == kEqBands;
    }
}

void SaveHeadsetState(const std::wstring& path, uint16_t productId, const HeadsetFeatures& features,
                      const HeadsetState& state) {
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
    if (state.eqFiltersKnown) {
        std::wstring filters;
        for (const EqBand& band : state.eqFilters) {
            if (!filters.empty()) {
                filters += L';';
            }
            filters += FormatBand(band);
        }
        WritePrivateProfileStringW(section.c_str(), L"EqFilters", filters.c_str(), path.c_str());
    }
    for (int index = 0; index < kExtraEqualizers; ++index) {
        const HeadsetState::ExtraEq& extra = state.extraEq[index];
        const ExtraEqKeys& keys = kExtraEqKeys[index];
        if (extra.preset >= 0) {
            const int value = keys.slot ? features.Extra(static_cast<ExtraEqualizer>(index)).SlotOf(extra.preset)
                                        : extra.preset;
            WritePrivateProfileStringW(section.c_str(), keys.preset, std::to_wstring(value).c_str(), path.c_str());
            if (keys.slot) {
                WritePrivateProfileStringW(section.c_str(), kLegacyMicPresetKey, nullptr, path.c_str());
            }
        }
        if (extra.curveKnown) {
            std::wstring curve;
            for (const int8_t band : extra.curve) {
                if (!curve.empty()) {
                    curve += L',';
                }
                curve += std::to_wstring(band);
            }
            WritePrivateProfileStringW(section.c_str(), kExtraEqKeys[index].curve, curve.c_str(), path.c_str());
        }
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

AddOnKind AddOnKindOf(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    std::wstring name = slash == std::wstring::npos ? path : path.substr(slash + 1);
    // SteelSeries GG's gamesense-discord-x64.exe, and any other program for Discord by the word in its name.
    std::transform(name.begin(), name.end(), name.begin(), [](wchar_t c) { return std::towlower(c); });
    return name.find(L"discord") != std::wstring::npos ? AddOnKind::Discord : AddOnKind::Generic;
}
