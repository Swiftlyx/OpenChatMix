// Equalizer of the virtual devices: a response graph with draggable bands, in the manner of
// SteelSeries Sonar, plus the editor of the selected band and the preset library.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "../app_info.h"
#include "panel.h"
#include "panel_internal.h"

namespace ui {

using namespace detail;

namespace {

constexpr float kGraphHeight = 176.0f;  // Plot with its axis labels, below the card's header row.
constexpr float kGraphHeaderHeight = 52.0f;
constexpr float kGraphRange = 15.0f;  // dB above and below 0 shown in the graph.
constexpr float kNodeRadius = 10.0f;
constexpr int kCurvePoints = 140;
constexpr float kFrequencyStep = 1.12246f;  // A sixth of an octave.
constexpr float kQStep = 1.15f;
const double kLogMin = std::log10(static_cast<double>(kEqMinFrequency));
const double kLogMax = std::log10(static_cast<double>(kEqMaxFrequency));

constexpr Text kFilterLabels[kFilterTypeCount] = {Text::FilterPeak,    Text::FilterLowShelf, Text::FilterHighShelf,
                                                  Text::FilterLowPass, Text::FilterHighPass, Text::FilterNotch};
constexpr Text kSmartVolumeLabels[kSmartVolumeCount] = {Text::EffectOff, Text::SmartQuiet, Text::SmartBalanced,
                                                        Text::SmartLoud};
constexpr float kEffectStep = 1.0f;

std::wstring FormatFrequency(float hz) {
    wchar_t text[32];
    if (hz >= 1000.0f) {
        swprintf_s(text, hz >= 10000.0f ? L"%.0f kHz" : L"%.1f kHz", hz / 1000.0f);
        std::wstring result = text;
        if (const size_t zero = result.find(L".0 "); zero != std::wstring::npos) {
            result.erase(zero, 2);
        }
        return result;
    }
    swprintf_s(text, L"%.0f Hz", hz);
    return text;
}

std::wstring FormatDecibels(float db) {
    wchar_t text[32];
    swprintf_s(text, L"%.1f dB", std::fabs(db));
    return (db > 0.04f ? L"+" : db < -0.04f ? L"−" : L"") + std::wstring(text);
}

std::wstring FormatQ(float q) {
    wchar_t text[16];
    swprintf_s(text, L"%.2f", q);
    return text;
}

// Whole hertz below 1 kHz, then three significant digits.
float RoundFrequency(float hz) {
    hz = std::clamp(hz, kEqMinFrequency, kEqMaxFrequency);
    if (hz < 1000.0f) {
        return std::round(hz);
    }
    const float step = hz < 10000.0f ? 10.0f : 100.0f;
    return std::round(hz / step) * step;
}

struct ChannelColors {
    D2D1_COLOR_F accent, onAccent, container, onContainer;
};

ChannelColors ColorsOf(const Palette& p, EqChannel channel) {
    switch (channel) {
    case EqChannel::Chat: return {p.tertiary, p.onTertiary, p.tertiaryContainer, p.onTertiaryContainer};
    // The palette has no plain secondary tone; the container pair stands in for it.
    case EqChannel::Mic: return {p.onSecondaryContainer, p.secondaryContainer, p.primary, p.onPrimary};
    case EqChannel::Media: return {p.onSurfaceVariant, p.surfaceContainerHighest, p.secondaryContainer, p.onSecondaryContainer};
    case EqChannel::Aux: return {p.secondary, p.surfaceContainerHighest, p.secondaryContainer, p.onSecondaryContainer};
    case EqChannel::Game: break;
    }
    return {p.primary, p.onPrimary, p.primaryContainer, p.onPrimaryContainer};
}

const wchar_t* ChannelName(EqChannel channel) {
    switch (channel) {
    case EqChannel::Chat: return T(Text::Chat);
    case EqChannel::Mic: return T(Text::Microphone);
    case EqChannel::Media: return T(Text::Media);
    case EqChannel::Aux: return T(Text::Aux);
    case EqChannel::Game: break;
    }
    return T(Text::Game);
}

// A typed value: digits with an optional sign and point, and k for thousands ("1.5k" is 1500).
std::optional<float> ParseTyped(std::wstring text) {
    float scale = 1.0f;
    while (!text.empty() && iswspace(text.back())) {
        text.pop_back();
    }
    if (!text.empty() && (text.back() == L'k' || text.back() == L'K')) {
        scale = 1000.0f;
        text.pop_back();
    }
    if (text.empty()) {
        return std::nullopt;
    }
    wchar_t* end = nullptr;
    const float value = std::wcstof(text.c_str(), &end);
    if (end == text.c_str() || *end != L'\0' || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value * scale;
}

}  // namespace

std::vector<EqChannel> Panel::EqChannels() const {
    std::vector<EqChannel> channels;
    if (routing_.enabled) {
        // Game and Chat playing straight on a headset's own devices pass nothing through OpenChatMix.
        if (!routing_.ownDevices || !routing_.ids[static_cast<int>(RoutingSlot::Game)].empty()) {
            channels.push_back(EqChannel::Game);
        }
        if (!routing_.ownDevices || !routing_.ids[static_cast<int>(RoutingSlot::Chat)].empty()) {
            channels.push_back(EqChannel::Chat);
        }
        if (routing_.mediaRouted) {
            channels.push_back(EqChannel::Media);
        }
        if (routing_.auxRouted) {
            channels.push_back(EqChannel::Aux);
        }
    }
    if (settings_.mic.enabled) {
        channels.push_back(EqChannel::Mic);
    }
    return channels;
}

EqChannelSettings& Panel::EqChannelState() {
    return EqStateOf(settings_, eqChannel_);
}

Panel::EqEditor Panel::ChannelEqEditor() {
    const ChannelColors colors = ColorsOf(P(), eqChannel_);
    EqEditor editor;
    editor.edit = [this](const std::function<void(ParametricEq&)>& edit) { EditEq(edit); };
    editor.band = &eqBand_;
    editor.accent = colors.accent;
    editor.onAccent = colors.onAccent;
    editor.enabled = EqChannelState().enabled;
    return editor;
}

Panel::EqEditor Panel::HeadsetEqEditor() {
    const Palette& p = P();
    EqEditor editor;
    // Each edit starts from what the headset plays now, so editing a preset turns it into the custom filters.
    editor.edit = [this](const std::function<void(ParametricEq&)>& edit) {
        ParametricEq eq;
        eq.bands = ActiveHeadsetFilters();
        edit(eq);
        SetHeadsetFilters(PresetFilters(eq, features_->equalizer.notch));
    };
    editor.band = &headsetBand_;
    editor.accent = p.primary;
    editor.onAccent = p.onPrimary;
    editor.hardware = true;
    // Notch is the last type; only headsets with a notch type of their own offer it.
    editor.typeCount = features_->equalizer.notch ? kFilterTypeCount : kFilterTypeCount - 1;
    editor.idBase = kIdHeadsetEqBase;
    return editor;
}

void Panel::EditEq(const std::function<void(ParametricEq&)>& edit) {
    const EqChannel channel = eqChannel_;
    RecordUndo(ChannelUndoStep(channel));
    Edit([channel, edit](Settings& s) {
        EqChannelSettings& state = EqStateOf(s, channel);
        edit(state.eq);
        state.preset.clear();
    });
}

void Panel::EditEffects(const std::function<void(EqEffects&)>& edit) {
    const EqChannel channel = eqChannel_;
    RecordUndo(ChannelUndoStep(channel));
    Edit([channel, edit](Settings& s) { edit(EqStateOf(s, channel).eq.effects); });
}

void Panel::ApplyEqPreset(const NamedEq& preset) {
    const EqChannel channel = eqChannel_;
    RecordUndo(ChannelUndoStep(channel));
    Edit([channel, preset](Settings& s) {
        EqChannelSettings& state = EqStateOf(s, channel);
        AssignBands(state.eq, preset.eq);
        state.preset = preset.name;
        state.enabled = true;
    });
}

void Panel::LoadEqLibrary(EqChannel channel) {
    eqLibraryChannel_ = channel;
    eqLibrary_[0] = BuiltinEqPresets(channel);
    eqLibrary_[1] = SonarEqPresets(channel);
    eqLibrary_[2] = LoadParametricPresets();
}

const NamedEq* Panel::FindInEqLibrary(const std::wstring& name) const {
    for (const auto& section : eqLibrary_) {
        for (const NamedEq& preset : section) {
            if (_wcsicmp(preset.name.c_str(), name.c_str()) == 0) {
                return &preset;
            }
        }
    }
    return nullptr;
}

std::vector<MenuItem> Panel::EqLibraryMenu(std::vector<const NamedEq*>& presets,
                                           std::span<const EqPreset> deviceOwn) const {
    constexpr Text kSections[] = {Text::PresetsBuiltin, Text::PresetsSonar, Text::MyPresets};
    constexpr int kOwnSection = 2;
    auto onDevice = [deviceOwn](const NamedEq& preset) {
        return std::any_of(deviceOwn.begin(), deviceOwn.end(), [&preset](const EqPreset& own) {
            return _wcsicmp(own.name, preset.name.c_str()) == 0;
        });
    };
    std::vector<MenuItem> items;
    for (int section = 0; section < 3; ++section) {
        bool titled = false;
        for (const NamedEq& preset : eqLibrary_[section]) {
            if (section != kOwnSection && onDevice(preset)) {
                continue;
            }
            if (!titled) {
                items.push_back({T(kSections[section]), true});
                presets.push_back(nullptr);
                titled = true;
            }
            items.push_back({preset.name});
            presets.push_back(&preset);
        }
    }
    return items;
}

void Panel::OpenEqPresetMenu(const D2D1_RECT_F& anchor) {
    if (eqLibraryChannel_ != eqChannel_) {
        LoadEqLibrary(eqChannel_);
    }
    std::vector<const NamedEq*> presets;
    std::vector<MenuItem> items = EqLibraryMenu(presets);
    // Manage presets sits after OpenChatMix's own, above GG's long list, so it is reached without scrolling.
    size_t manage = items.size();
    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].header && (items[i].text == T(Text::PresetsSonar) || items[i].text == T(Text::MyPresets))) {
            manage = i;
            break;
        }
    }
    items.insert(items.begin() + static_cast<std::ptrdiff_t>(manage),
                 MenuItem{T(Text::ManagePresets), false, kGlyphSettings, true});
    presets.insert(presets.begin() + static_cast<std::ptrdiff_t>(manage), nullptr);
    const std::wstring current = EqChannelState().preset;
    int selected = -1;
    std::vector<NamedEq> chosen;
    for (size_t i = 0; i < presets.size(); ++i) {
        chosen.push_back(presets[i] ? *presets[i] : NamedEq{});
        if (presets[i] && selected < 0 && _wcsicmp(presets[i]->name.c_str(), current.c_str()) == 0) {
            selected = static_cast<int>(i);
        }
    }
    OpenMenu(
        anchor, std::move(items), selected,
        [this, manage, chosen = std::move(chosen)](int index) {
            if (index == static_cast<int>(manage)) {
                GoTo(Page::EqPresets);
            } else {
                ApplyEqPreset(chosen[index]);
            }
        },
        true);
}

void Panel::DrawEqualizerPage() {
    const Palette& p = P();
    const std::vector<EqChannel> channels = EqChannels();
    if (channels.empty()) {
        DrawTitle(T(Text::Equalizers));
        const D2D1_RECT_F card = Card(kHeaderHeight, 172.0f);
        canvas_.Glyph(kGlyphEqualizer, {kWidth / 2.0f, card.top + 34.0f}, 24.0f, p.primary);
        canvas_.Text(T(routing_.enabled && routing_.ownDevices ? Text::EqNeedsDevice : Text::EqNeedsRouting),
                     {card.left + 24.0f, card.top + 56.0f, card.right - 24.0f, card.top + 120.0f},
                     TextStyle::BodyMedium, p.onSurfaceVariant, TextAlign::Center, true);
        TextButton(kIdEqOpenAudio,
                   {kWidth / 2.0f - 100.0f, card.top + 126.0f, kWidth / 2.0f + 100.0f, card.top + 158.0f}, kGlyphVolume,
                   T(Text::OpenAudioSettings), true, [this] { GoTo(Page::Audio); });
        return;
    }

    // The channel switch and the graph stay in place; the band editor and effects scroll below them,
    // so every change shows on the graph.
    if (std::find(channels.begin(), channels.end(), eqChannel_) == channels.end()) {
        eqChannel_ = channels.front();
        eqBand_ = 0;
        LoadEqLibrary(eqChannel_);
    }
    const ChannelColors colors = ColorsOf(p, eqChannel_);
    const D2D1_COLOR_F accent = colors.accent;
    std::vector<const wchar_t*> labels;
    for (const EqChannel channel : channels) {
        labels.push_back(channel == EqChannel::Game    ? T(Text::Game)
                         : channel == EqChannel::Chat  ? T(Text::Chat)
                         : channel == EqChannel::Media ? T(Text::Media)
                         : channel == EqChannel::Aux   ? T(Text::Aux)
                                                       : T(Text::MicShort));
    }
    // Narrower segments when three or four channels share the title row.
    const float segmentsWidth = (labels.size() > 3 ? 54.0f : labels.size() > 2 ? 60.0f : 72.0f) * labels.size();
    const int selected = static_cast<int>(std::find(channels.begin(), channels.end(), eqChannel_) - channels.begin());
    // The channel switch sits left of the pin. The title stays while it fits beside the switch; otherwise the
    // switch takes the whole row, as the Equalizer tab below already names the page.
    const float segmentsRight = kWidth - kGutter - design::kIconButtonSize - design::kSpace1;
    const wchar_t* title = T(Text::Equalizers);
    const bool titled = kInset + canvas_.Measure(title, TextStyle::TitleLarge) + design::kSpace3 <=
                        segmentsRight - segmentsWidth;
    if (titled) {
        DrawTitle(title);
    } else {
        DrawPinButton();
    }
    const float segmentsLeft = titled ? segmentsRight - segmentsWidth : kGutter;
    DrawSegmented({segmentsLeft, (kHeaderHeight - design::kControlHeight) / 2.0f, segmentsRight,
                   (kHeaderHeight + design::kControlHeight) / 2.0f},
                  kIdEqChannel, labels, selected,
                  colors.container, colors.onContainer, [this, channels](int index) {
                      eqChannel_ = channels[index];
                      eqBand_ = 0;
                      LoadEqLibrary(eqChannel_);
                      Invalidate();
                  });

    const EqChannelSettings& state = EqChannelState();
    const D2D1_RECT_F card = Card(kHeaderHeight, kGraphHeaderHeight + kGraphHeight);

    // Header row: the preset as a dropdown, and the channel's on/off switch.
    const D2D1_RECT_F presetButton{card.left + 8.0f, card.top + 8.0f, card.right - 84.0f, card.top + 48.0f};
    StateLayer(kIdEqPresetButton, presetButton, design::kRadiusMedium, p.onSurface);
    canvas_.Glyph(kGlyphEqualizer, {presetButton.left + 20.0f, (presetButton.top + presetButton.bottom) / 2.0f}, 16.0f,
                  accent);
    const std::wstring presetName = state.preset.empty() ? T(Text::EqCustom) : state.preset;
    const float nameWidth = std::min(canvas_.Measure(presetName, TextStyle::TitleSmall),
                                     presetButton.right - presetButton.left - 72.0f);
    canvas_.Text(presetName, {presetButton.left + 40.0f, presetButton.top, presetButton.left + 41.0f + nameWidth,
                              presetButton.bottom},
                 TextStyle::TitleSmall, p.onSurface);
    canvas_.Glyph(kGlyphChevronDown,
                  {presetButton.left + 52.0f + nameWidth, (presetButton.top + presetButton.bottom) / 2.0f}, 10.0f,
                  p.onSurfaceVariant);
    AddHit({presetButton, kIdEqPresetButton, [this, presetButton] { OpenEqPresetMenu(presetButton); }});
    const D2D1_RECT_F track{card.right - design::kItemPadding - design::kSwitchWidth, card.top + 12.0f,
                            card.right - design::kItemPadding, card.top + 12.0f + design::kSwitchHeight};
    DrawSwitch(track, state.enabled, kIdEqToggle);
    AddHit({Inset(track, -8.0f, -8.0f), kIdEqToggle, [this] {
                const EqChannel channel = eqChannel_;
                RecordUndo(ChannelUndoStep(channel));
                Edit([channel](Settings& s) {
                    EqChannelSettings& target = EqStateOf(s, channel);
                    target.enabled = !target.enabled;
                });
            }});

    const EqEditor editor = ChannelEqEditor();
    DrawEqGraph({card.left, card.top + kGraphHeaderHeight, card.right, card.bottom}, state.eq, editor);

    const float scrollTop = card.bottom + design::kSpace1;
    float y = BeginScroll(Page::Equalizer, scrollTop);
    y = DrawBandEditor(y + design::kSpace2, state.eq, editor);
    y = DrawEqEffects(y, state.eq);
    // The microphone's plugins have their place in its chain, on the Microphone page.
    if (eqChannel_ != EqChannel::Mic) {
        y = DrawPlugins(y + design::kSectionGap - design::kGroupGap, eqChannel_) + design::kGroupGap;
    }
    EndScroll(Page::Equalizer, scrollTop, y - design::kGroupGap);
}

// A parametric headset's equalizer: its presets, the response graph of its filters and the editor of the selected
// one, as on the virtual devices' equalizer; then the preset tools.
float Panel::DrawHeadsetFilters(float y) {
    const Palette& p = P();
    const EqualizerSpec& eq = features_->equalizer;
    y = DrawEqualizerTitle(y, [this] { SetHeadsetCurve(EqCurve{}); });

    const wchar_t* hint = eq.experimental ? T(Text::EqExperimental) : nullptr;
    const float hintHeight = hint ? HintHeight(hint, kWidth - 64.0f) + 4.0f : 0.0f;
    const D2D1_RECT_F card = Card(y, 16.0f + 44.0f + hintHeight + kGraphHeight);
    std::vector<const wchar_t*> labels;
    for (const auto& preset : eq.presets) {
        labels.push_back(preset.name);
    }
    labels.push_back(T(Text::EqCustom));
    DrawSegmented({32.0f, y + 16.0f, kWidth - 32.0f, y + 48.0f}, kIdEqPreset, labels, headset_.eqPreset, p.primary,
                  p.onPrimary, [this](int index) { SetHeadsetPreset(index); });
    float inner = y + 60.0f;
    if (hint) {
        canvas_.Text(hint, {32.0f, inner, kWidth - 32.0f, inner + hintHeight}, TextStyle::BodySmall, p.onSurfaceVariant,
                     TextAlign::Leading, true);
        inner += hintHeight;
    }

    ParametricEq shown;
    shown.bands = ActiveHeadsetFilters();
    const EqEditor editor = HeadsetEqEditor();
    DrawEqGraph({card.left, inner - 8.0f, card.right, card.bottom}, shown, editor);
    // The band editor belongs to the graph, so it follows at the gap of a group.
    y = DrawBandEditor(card.bottom + design::kGroupGap, shown, editor) - design::kSectionGap + design::kGroupGap;

    const D2D1_RECT_F tools = Card(y, 16.0f + HeadsetPresetToolsHeight());
    DrawHeadsetPresetTools(y + 16.0f);
    return tools.bottom + design::kSectionGap;
}

float Panel::DrawEqEffects(float y, const ParametricEq& eq) {
    y = SectionTitle(T(Text::EqEffects), y);
    const EqEffects& effects = eq.effects;
    std::vector<CardRow> rows;
    auto boost = [this](uint32_t id, Text label, float value, float EqEffects::*field) {
        return NumberItem(
            id, T(label), value < 0.05f ? std::wstring(T(Text::EffectOff)) : FormatDecibels(value), value > 0.0f,
            value < kEffectMaxGain,
            [this, field] {
                EditEffects(
                    [field](EqEffects& target) { target.*field = std::max(0.0f, target.*field - kEffectStep); });
            },
            [this, field] {
                EditEffects([field](EqEffects& target) {
                    target.*field = std::min(kEffectMaxGain, target.*field + kEffectStep);
                });
            },
            [this, field](const std::wstring& text) {
                const std::optional<float> typed = text.empty() ? std::optional<float>(0.0f) : ParseTyped(text);
                if (!typed) {
                    return false;
                }
                EditEffects([field, value = std::clamp(*typed, 0.0f, kEffectMaxGain)](EqEffects& target) {
                    target.*field = value;
                });
                return true;
            });
    };
    rows.push_back(boost(kIdEqBass, Text::BassBoost, effects.bassBoost, &EqEffects::bassBoost));
    rows.push_back(boost(kIdEqVoice, Text::VoiceClarity, effects.voiceClarity, &EqEffects::voiceClarity));
    rows.push_back(boost(kIdEqTreble, Text::TrebleBoost, effects.trebleBoost, &EqEffects::trebleBoost));
    // Four short options: all in view as a button group.
    std::vector<std::wstring> smartLabels;
    for (const Text label : kSmartVolumeLabels) {
        smartLabels.push_back(T(label));
    }
    rows.push_back(ChoiceItem(
        kIdEqSmart, T(Text::SmartVolume), std::move(smartLabels), static_cast<int>(effects.smartVolume),
        [this](int index) {
            EditEffects([index](EqEffects& target) { target.smartVolume = static_cast<SmartVolume>(index); });
        },
        T(Text::SmartVolumeHint)));
    return DrawCard(y, rows) + design::kGroupGap;
}

void Panel::DrawEqGraph(const D2D1_RECT_F& rect, const ParametricEq& eq, const EqEditor& editor) {
    const Palette& p = P();
    const D2D1_COLOR_F accent = editor.accent;
    const D2D1_COLOR_F onAccent = editor.onAccent;
    const bool enabled = editor.enabled;
    const int selectedBand = *editor.band;

    const D2D1_RECT_F plot{rect.left + 40.0f, rect.top + 16.0f, rect.right - 16.0f, rect.bottom - 28.0f};
    const float width = plot.right - plot.left;
    const float mid = (plot.top + plot.bottom) / 2.0f;
    const float half = (plot.bottom - plot.top) / 2.0f;
    auto xOf = [&](double hz) {
        return plot.left + static_cast<float>((std::log10(hz) - kLogMin) / (kLogMax - kLogMin)) * width;
    };
    auto hzOf = [=](float x) {
        const double t = std::clamp(static_cast<double>((x - plot.left) / width), 0.0, 1.0);
        return static_cast<float>(std::pow(10.0, kLogMin + t * (kLogMax - kLogMin)));
    };
    auto yOf = [&](double db) {
        return mid -
               static_cast<float>(std::clamp(db, -static_cast<double>(kGraphRange), static_cast<double>(kGraphRange)) /
                                  kGraphRange) *
                   half;
    };
    auto dbOf = [=](float y) { return (mid - y) / half * kGraphRange; };

    // Grid with octave-ish frequency marks and 6 dB steps.
    constexpr std::pair<double, const wchar_t*> kMarks[] = {{50, L"50"},   {100, L"100"},  {200, L"200"},
                                                            {500, L"500"}, {1000, L"1k"},  {2000, L"2k"},
                                                            {5000, L"5k"}, {10000, L"10k"}};
    for (const auto& [hz, label] : kMarks) {
        const float x = xOf(hz);
        canvas_.Line({x, plot.top}, {x, plot.bottom}, Alpha(p.outlineVariant, 0.7f), 1.0f);
        canvas_.Text(label, {x - 20.0f, plot.bottom + 6.0f, x + 20.0f, plot.bottom + 22.0f}, TextStyle::LabelMedium,
                     p.onSurfaceVariant, TextAlign::Center);
    }
    for (const int db : {-12, -6, 0, 6, 12}) {
        const float y = yOf(db);
        canvas_.Line({plot.left, y}, {plot.right, y}, db == 0 ? p.outline : Alpha(p.outlineVariant, 0.7f), 1.0f);
        const std::wstring label = db > 0 ? L"+" + std::to_wstring(db) : db < 0 ? L"−" + std::to_wstring(-db) : L"0";
        canvas_.Text(label, {rect.left + 4.0f, y - 9.0f, plot.left - 6.0f, y + 9.0f}, TextStyle::LabelMedium,
                     p.onSurfaceVariant, TextAlign::Trailing);
    }

    // Response curve with the area to 0 dB filled.
    std::array<D2D1_POINT_2F, kCurvePoints> curve{};
    for (int i = 0; i < kCurvePoints; ++i) {
        const double hz = std::pow(10.0, kLogMin + (kLogMax - kLogMin) * i / (kCurvePoints - 1));
        curve[i] = {xOf(hz), yOf(ResponseDb(eq, hz))};
    }
    std::vector<D2D1_POINT_2F> area(curve.begin(), curve.end());
    area.push_back({curve.back().x, yOf(0)});
    area.push_back({curve.front().x, yOf(0)});
    canvas_.PushClip(plot);
    canvas_.FillPolygon(area, Alpha(accent, enabled ? 0.20f : 0.08f));
    canvas_.Polyline(curve, Alpha(accent, enabled ? 1.0f : 0.45f), 2.5f);
    canvas_.PopClip();

    // Band handles; the selected one is drawn last so it stays on top.
    std::array<int, kParametricBands> order{};
    for (int i = 0; i < kParametricBands; ++i) {
        order[i] = i;
    }
    std::stable_partition(order.begin(), order.end(), [selectedBand](int band) { return band != selectedBand; });
    for (const int band : order) {
        const EqBand& settings = eq.bands[band];
        const float x = xOf(settings.frequency);
        const float y = UsesGain(settings.type) ? yOf(settings.gain) : yOf(0);
        const bool selected = band == selectedBand;
        const uint32_t id = PartId(kIdEqGraph + editor.idBase, static_cast<uint32_t>(band) + 1);
        const float grow =
            Animate(AnimKey(id, 0), selected ? 1.0f : (hotId_ == id ? 0.5f : 0.0f), design::kSpringFastEffects);
        const float radius = kNodeRadius + 2.0f * grow;
        if (selected) {
            canvas_.FillCircle({x, y}, radius + 4.0f, Alpha(accent, 0.25f));
            canvas_.FillCircle({x, y}, radius, accent);
        } else {
            canvas_.FillCircle({x, y}, radius, p.surfaceContainerHighest);
            canvas_.StrokeCircle({x, y}, radius - 0.75f, settings.enabled ? accent : Alpha(p.outline, 0.6f), 1.5f);
        }
        canvas_.Text(std::to_wstring(band + 1), {x - radius - 4.0f, y - radius, x + radius + 4.0f, y + radius},
                     band + 1 >= 10 ? TextStyle::LabelSmall : TextStyle::LabelMedium,
                     selected ? onAccent : (settings.enabled ? p.onSurface : Alpha(p.onSurfaceVariant, 0.6f)),
                     TextAlign::Center);

        Hit hit;
        hit.rect = {x - 14.0f, y - 14.0f, x + 14.0f, y + 14.0f};
        hit.id = id;
        hit.drag = [this, editor, band, id, x, y, hzOf, dbOf](float pointerX, float pointerY) {
            // The handle keeps its offset from the pointer, so grabbing it does not make it jump.
            if (dragId_ != id) {
                dragId_ = id;
                dragOffsetX_ = x - pointerX;
                dragOffsetY_ = y - pointerY;
                *editor.band = band;
                Invalidate();
                return;
            }
            const float frequency = RoundFrequency(hzOf(pointerX + dragOffsetX_));
            const float gain =
                std::clamp(std::round(dbOf(pointerY + dragOffsetY_) * 2.0f) / 2.0f, -kEqMaxGain, kEqMaxGain);
            editor.edit([band, frequency, gain](ParametricEq& target) {
                EqBand& edited = target.bands[band];
                edited.frequency = frequency;
                if (UsesGain(edited.type)) {
                    edited.gain = gain;
                }
            });
        };
        hit.wheel = [editor, band](int notches) {
            *editor.band = band;
            editor.edit([band, notches](ParametricEq& target) {
                EqBand& edited = target.bands[band];
                edited.q =
                    std::clamp(std::round(edited.q * std::pow(kQStep, static_cast<float>(notches)) * 100.0f) / 100.0f,
                               kEqMinQ, kEqMaxQ);
            });
        };
        hit.doubleClick = [editor, band] {
            *editor.band = band;
            editor.edit([band](ParametricEq& target) { target.bands[band].gain = 0.0f; });
        };
        AddHit(std::move(hit));
    }
}

float Panel::DrawBandEditor(float y, const ParametricEq& eq, const EqEditor& editor) {
    const Palette& p = P();
    const int band = *editor.band;
    const EqBand& settings = eq.bands[band];
    const uint32_t ids = editor.idBase;
    // A typed value, clamped to its range, applied by apply.
    auto typedValue = [](float low, float high, std::function<void(float)> apply) {
        return [low, high, apply = std::move(apply)](const std::wstring& text) {
            const std::optional<float> typed = ParseTyped(text);
            if (!typed) {
                return false;
            }
            apply(std::clamp(*typed, low, high));
            return true;
        };
    };
    auto editBand = [edit = editor.edit, band](std::function<void(EqBand&)> change) {
        edit([band, change](ParametricEq& target) { change(target.bands[band]); });
    };
    std::vector<CardRow> rows;

    // Band selector with its on/off switch.
    rows.push_back({kRowHeight,
                    [this, &p, editor, ids, band, enabled = settings.enabled](float top) {
                        const std::wstring title = std::wstring(T(Text::Band)) + L" " + std::to_wstring(band + 1);
                        canvas_.Text(title, {kInset, top, 180.0f, top + kRowHeight}, TextStyle::BodyLarge, p.onSurface);
                        const float switchLeft = kWidth - kInset - design::kSwitchWidth;
                        const float middle = top + kRowHeight / 2.0f;
                        IconButton(PartId(kIdEqBandSelect + ids, 1), {switchLeft - 72.0f, middle}, kGlyphChevronLeft,
                                   p.onSurface, [this, selected = editor.band] {
                                       *selected = (*selected + kParametricBands - 1) % kParametricBands;
                                       Invalidate();
                                   });
                        Tooltip(PartId(kIdEqBandSelect + ids, 1), T(Text::TipPreviousBand));
                        IconButton(PartId(kIdEqBandSelect + ids, 2), {switchLeft - 32.0f, middle}, kGlyphChevronRight,
                                   p.onSurface, [this, selected = editor.band] {
                                       *selected = (*selected + 1) % kParametricBands;
                                       Invalidate();
                                   });
                        Tooltip(PartId(kIdEqBandSelect + ids, 2), T(Text::TipNextBand));
                        const D2D1_RECT_F track{switchLeft, middle - design::kSwitchHeight / 2.0f, kWidth - kInset,
                                                middle + design::kSwitchHeight / 2.0f};
                        DrawSwitch(track, enabled, kIdEqBandToggle + ids);
                        AddHit({Inset(track, -6.0f, -6.0f), kIdEqBandToggle + ids, [edit = editor.edit, band] {
                                    edit([band](ParametricEq& target) {
                                        target.bands[band].enabled = !target.bands[band].enabled;
                                    });
                                }});
                    }});

    const int typeCount = editor.typeCount;
    rows.push_back(DropdownItem(
        kIdEqType + ids, T(Text::FilterType), std::min(static_cast<int>(settings.type), typeCount - 1), typeCount,
        [](int i) { return std::wstring(T(kFilterLabels[i])); },
        [editBand](int index) {
            editBand([index](EqBand& target) { target.type = static_cast<FilterType>(index); });
        }));

    rows.push_back(NumberItem(
        kIdEqFrequency + ids, T(Text::Frequency), FormatFrequency(settings.frequency), settings.frequency > kEqMinFrequency,
        settings.frequency < kEqMaxFrequency,
        [editBand] {
            editBand([](EqBand& target) { target.frequency = RoundFrequency(target.frequency / kFrequencyStep); });
        },
        [editBand] {
            editBand([](EqBand& target) { target.frequency = RoundFrequency(target.frequency * kFrequencyStep); });
        },
        typedValue(
            kEqMinFrequency, kEqMaxFrequency,
            [editBand](float value) { editBand([value](EqBand& target) { target.frequency = value; }); })));

    const bool gainUsed = UsesGain(settings.type);
    rows.push_back(NumberItem(
        kIdEqGain + ids, T(Text::Level), gainUsed ? FormatDecibels(settings.gain) : L"—",
        gainUsed && settings.gain > -kEqMaxGain, gainUsed && settings.gain < kEqMaxGain,
        [editBand] { editBand([](EqBand& target) { target.gain = std::max(-kEqMaxGain, target.gain - 0.5f); }); },
        [editBand] { editBand([](EqBand& target) { target.gain = std::min(kEqMaxGain, target.gain + 0.5f); }); },
        gainUsed ? typedValue(-kEqMaxGain, kEqMaxGain,
                              [editBand](float value) { editBand([value](EqBand& target) { target.gain = value; }); })
                 : std::function<bool(const std::wstring&)>()));

    rows.push_back(NumberItem(
        kIdEqQ + ids, L"Q", FormatQ(settings.q), settings.q > kEqMinQ, settings.q < kEqMaxQ,
        [editBand] {
            editBand(
                [](EqBand& target) { target.q = std::max(kEqMinQ, std::round(target.q / kQStep * 100.0f) / 100.0f); });
        },
        [editBand] {
            editBand(
                [](EqBand& target) { target.q = std::min(kEqMaxQ, std::round(target.q * kQStep * 100.0f) / 100.0f); });
        },
        typedValue(
            kEqMinQ, kEqMaxQ, [editBand](float value) { editBand([value](EqBand& target) { target.q = value; }); })));

    // The headset's filters have no preamp, and their reset and preset files sit on the Headset tab.
    if (editor.hardware) {
        return DrawCard(y, rows) + design::kSectionGap;
    }
    rows.push_back(NumberItem(
        kIdEqPreamp, T(Text::Preamp), FormatDecibels(eq.preamp), eq.preamp > -kEqMaxGain, eq.preamp < kEqMaxGain,
        [this] { EditEq([](ParametricEq& target) { target.preamp = std::max(-kEqMaxGain, target.preamp - 0.5f); }); },
        [this] { EditEq([](ParametricEq& target) { target.preamp = std::min(kEqMaxGain, target.preamp + 0.5f); }); },
        typedValue(-kEqMaxGain, kEqMaxGain, [this](float value) {
            EditEq([value](ParametricEq& target) { target.preamp = value; });
        })));

    rows.push_back({kRowHeight, [this](float top) {
                        const float width = (kWidth - 2.0f * kInset) / 3.0f;
                        const float buttonTop = top + (kRowHeight - design::kControlHeight) / 2.0f;
                        const float bottom = buttonTop + design::kControlHeight;
                        TextButton(kIdEqReset2, {kInset, buttonTop, kInset + width, bottom}, kGlyphReset,
                                   T(Text::EqReset), false,
                                   [this] { EditEq([](ParametricEq& target) { AssignBands(target, FlatEq()); }); });
                        TextButton(kIdEqSaveFile, {kInset + width, buttonTop, kInset + 2.0f * width, bottom},
                                   kGlyphSave, T(Text::EqSave), false,
                                   [this] { RunFileDialog(FileDialog::SaveEqPreset); });
                        TextButton(kIdEqImportFile, {kInset + 2.0f * width, buttonTop, kWidth - kInset, bottom},
                                   kGlyphImport, T(Text::EqImport), false,
                                   [this] { RunFileDialog(FileDialog::ImportEqPreset); });
                    }});
    return DrawCard(y, rows) + design::kSectionGap;
}

void Panel::DrawEqPresets() {
    const Palette& p = P();
    DrawHeader(T(Text::Presets), ChannelName(eqChannel_));
    float y = BeginScroll(Page::EqPresets, kHeaderHeight);

    // Three tonal buttons as a standard button group: 8 DIP apart.
    const float buttonWidth = (kWidth - 2.0f * kGutter - 2.0f * design::kSpace2) / 3.0f;
    const float second = kGutter + buttonWidth + design::kSpace2;
    const float third = second + buttonWidth + design::kSpace2;
    const float bottom = y + design::kButtonHeight;
    TextButton(kIdEqSaveFile, {kGutter, y, kGutter + buttonWidth, bottom}, kGlyphSave, T(Text::EqSave), true,
               [this] { RunFileDialog(FileDialog::SaveEqPreset); });
    TextButton(kIdEqImportFile, {second, y, second + buttonWidth, bottom}, kGlyphImport, T(Text::EqImport), true,
               [this] { RunFileDialog(FileDialog::ImportEqPreset); });
    TextButton(kIdEqFolder2, {third, y, kWidth - kGutter, bottom}, kGlyphFolder, T(Text::EqFolder),
               true, [] { OpenWithExplorer(ParametricPresetDirectory()); });
    y = bottom + design::kSectionGap;

    const std::wstring current = EqChannelState().preset;
    const D2D1_COLOR_F accent = ColorsOf(p, eqChannel_).accent;
    constexpr Text kSections[] = {Text::PresetsBuiltin, Text::PresetsSonar, Text::MyPresets};
    for (int section = 0; section < 3; ++section) {
        const std::vector<NamedEq>& presets = eqLibrary_[section];
        if (presets.empty()) {
            continue;
        }
        y = SectionTitle(T(kSections[section]), y);
        std::vector<CardRow> rows;
        for (const auto& preset : presets) {
            const uint32_t id = HashId(kKindEqPreset, std::to_wstring(section) + preset.name);
            const bool chosen = _wcsicmp(preset.name.c_str(), current.c_str()) == 0;
            rows.push_back({kRowHeight,
                            [this, &p, &preset, id, chosen, accent](float top) {
                                canvas_.Text(preset.name, {kInset, top, design::kTrailingIconCenter - design::kSpace4,
                                                           top + kRowHeight},
                                             TextStyle::BodyLarge, chosen ? accent : p.onSurface);
                                if (chosen) {
                                    canvas_.Glyph(kGlyphCheck, {design::kTrailingIconCenter, top + kRowHeight / 2.0f},
                                                  design::kSmallIconSize + 2.0f, accent);
                                }
                                AddHit({TileRect(top, kRowHeight), id, [this, preset] {
                                            ApplyEqPreset(preset);
                                            GoBack();
                                        }});
                            },
                            id});
        }
        y = DrawCard(y, rows) + design::kSectionGap;
    }
    EndScroll(Page::EqPresets, kHeaderHeight, y);
}

}  // namespace ui
