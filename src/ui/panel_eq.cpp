// Equalizer of the virtual devices: a response graph with draggable bands, in the manner of
// SteelSeries Sonar, plus the editor of the selected band and the preset library.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

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

}  // namespace

EqChannelSettings& Panel::EqChannelState() {
    return eqChannel_ == EqChannel::Game ? settings_.gameEq : settings_.chatEq;
}

void Panel::EditEq(const std::function<void(ParametricEq&)>& edit) {
    const EqChannel channel = eqChannel_;
    Edit([channel, edit](Settings& s) {
        EqChannelSettings& state = channel == EqChannel::Game ? s.gameEq : s.chatEq;
        edit(state.eq);
        state.preset.clear();
    });
}

void Panel::EditEffects(const std::function<void(EqEffects&)>& edit) {
    const EqChannel channel = eqChannel_;
    Edit([channel, edit](Settings& s) { edit((channel == EqChannel::Game ? s.gameEq : s.chatEq).eq.effects); });
}

void Panel::ApplyEqPreset(const NamedEq& preset) {
    const EqChannel channel = eqChannel_;
    Edit([channel, preset](Settings& s) {
        EqChannelSettings& state = channel == EqChannel::Game ? s.gameEq : s.chatEq;
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

std::vector<MenuItem> Panel::EqLibraryMenu(std::vector<const NamedEq*>& presets) const {
    constexpr Text kSections[] = {Text::PresetsBuiltin, Text::PresetsSonar, Text::MyPresets};
    std::vector<MenuItem> items;
    for (int section = 0; section < 3; ++section) {
        if (eqLibrary_[section].empty()) {
            continue;
        }
        items.push_back({T(kSections[section]), true});
        presets.push_back(nullptr);
        for (const NamedEq& preset : eqLibrary_[section]) {
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
    const std::wstring current = EqChannelState().preset;
    int selected = -1;
    std::vector<NamedEq> chosen;
    for (size_t i = 0; i < presets.size(); ++i) {
        chosen.push_back(presets[i] ? *presets[i] : NamedEq{});
        if (presets[i] && selected < 0 && _wcsicmp(presets[i]->name.c_str(), current.c_str()) == 0) {
            selected = static_cast<int>(i);
        }
    }
    items.push_back({T(Text::ManagePresets)});
    OpenMenu(anchor, std::move(items), selected, [this, chosen = std::move(chosen)](int index) {
        if (index >= static_cast<int>(chosen.size())) {
            GoTo(Page::EqPresets);
        } else {
            ApplyEqPreset(chosen[index]);
        }
    });
}

void Panel::DrawEqualizerPage() {
    const Palette& p = P();
    DrawTitle(T(Text::Equalizers));
    if (!routing_.enabled) {
        const D2D1_RECT_F card = Card(kHeaderHeight + 4.0f, 172.0f);
        canvas_.Glyph(kGlyphEqualizer, {kWidth / 2.0f, card.top + 34.0f}, 24.0f, p.primary);
        canvas_.Text(T(Text::EqNeedsRouting), {card.left + 24.0f, card.top + 56.0f, card.right - 24.0f, card.top + 120.0f},
                     TextStyle::BodyMedium, p.onSurfaceVariant, TextAlign::Center, true);
        TextButton(kIdEqOpenAudio, {kWidth / 2.0f - 100.0f, card.top + 126.0f, kWidth / 2.0f + 100.0f, card.top + 158.0f},
                   kGlyphVolume, T(Text::OpenAudioSettings), true, [this] { GoTo(Page::Audio); });
        return;
    }

    // The channel switch and the graph stay in place; the band editor and effects scroll below them,
    // so every change shows on the graph.
    const bool chat = eqChannel_ == EqChannel::Chat;
    const D2D1_COLOR_F accent = chat ? p.tertiary : p.primary;
    const wchar_t* channels[] = {T(Text::Game), T(Text::Chat)};
    DrawSegmented({kWidth - 16.0f - 176.0f, 14.0f, kWidth - 16.0f, 46.0f}, kIdEqChannel, channels, chat ? 1 : 0,
                  chat ? p.tertiaryContainer : p.primaryContainer, chat ? p.onTertiaryContainer : p.onPrimaryContainer,
                  [this](int index) {
                      eqChannel_ = index ? EqChannel::Chat : EqChannel::Game;
                      eqBand_ = 0;
                      LoadEqLibrary(eqChannel_);
                      Invalidate();
                  });

    const EqChannelSettings& state = EqChannelState();
    const D2D1_RECT_F card{16.0f, kHeaderHeight + 4.0f, kWidth - 16.0f,
                           kHeaderHeight + 4.0f + kGraphHeaderHeight + kGraphHeight};
    canvas_.FillRound(card, kCardRadius, Alpha(p.surfaceContainer, kCardAlpha));

    // Header row: the preset as a dropdown, and the channel's on/off switch.
    const D2D1_RECT_F presetButton{card.left + 8.0f, card.top + 8.0f, card.right - 84.0f, card.top + 48.0f};
    StateLayer(kIdEqPresetButton, presetButton, 20.0f, p.onSurface);
    canvas_.Glyph(kGlyphEqualizer, {presetButton.left + 20.0f, (presetButton.top + presetButton.bottom) / 2.0f}, 16.0f,
                  accent);
    const std::wstring presetName = state.preset.empty() ? T(Text::EqCustom) : state.preset;
    const float nameWidth = std::min(canvas_.Measure(presetName, TextStyle::TitleSmall),
                                     presetButton.right - presetButton.left - 72.0f);
    canvas_.Text(presetName, {presetButton.left + 40.0f, presetButton.top, presetButton.left + 41.0f + nameWidth,
                              presetButton.bottom},
                 TextStyle::TitleSmall, p.onSurface);
    canvas_.Glyph(kGlyphChevronDown, {presetButton.left + 52.0f + nameWidth, (presetButton.top + presetButton.bottom) / 2.0f},
                  10.0f, p.onSurfaceVariant);
    AddHit({presetButton, kIdEqPresetButton, [this, presetButton] { OpenEqPresetMenu(presetButton); }});
    const D2D1_RECT_F track{card.right - 16.0f - 52.0f, card.top + 12.0f, card.right - 16.0f, card.top + 44.0f};
    DrawSwitch(track, state.enabled, kIdEqToggle);
    AddHit({Inset(track, -8.0f, -8.0f), kIdEqToggle, [this] {
                const EqChannel channel = eqChannel_;
                Edit([channel](Settings& s) {
                    EqChannelSettings& target = channel == EqChannel::Game ? s.gameEq : s.chatEq;
                    target.enabled = !target.enabled;
                });
            }});

    DrawEqGraph({card.left, card.top + kGraphHeaderHeight, card.right, card.bottom}, state.eq);

    const float scrollTop = card.bottom + 8.0f;
    float y = BeginScroll(Page::Equalizer, scrollTop);
    y = DrawBandEditor(y, state.eq);
    y = DrawEqEffects(y, state.eq);
    EndScroll(Page::Equalizer, scrollTop, y - 12.0f);
}

float Panel::DrawEqEffects(float y, const ParametricEq& eq) {
    const Palette& p = P();
    y = SectionTitle(T(Text::EqEffects), y);
    const float smartHint = HintHeight(T(Text::SmartVolumeHint));
    const D2D1_RECT_F card = Card(y, 2.0f * kListPadding + 4.0f * kRowHeight + smartHint);
    float top = card.top + kListPadding;
    const EqEffects& effects = eq.effects;

    auto boostRow = [&](uint32_t id, Text label, float value, float EqEffects::*field) {
        const std::wstring text = value < 0.05f ? std::wstring(T(Text::EffectOff)) : FormatDecibels(value);
        DrawStepperRow(id, top, T(label), text, 88.0f, value > 0.0f, value < kEffectMaxGain,
                       [this, field] {
                           EditEffects([field](EqEffects& target) {
                               target.*field = std::max(0.0f, target.*field - kEffectStep);
                           });
                       },
                       [this, field] {
                           EditEffects([field](EqEffects& target) {
                               target.*field = std::min(kEffectMaxGain, target.*field + kEffectStep);
                           });
                       });
        top += kRowHeight;
    };
    boostRow(kIdEqBass, Text::BassBoost, effects.bassBoost, &EqEffects::bassBoost);
    boostRow(kIdEqVoice, Text::VoiceClarity, effects.voiceClarity, &EqEffects::voiceClarity);
    boostRow(kIdEqTreble, Text::TrebleBoost, effects.trebleBoost, &EqEffects::trebleBoost);

    const int smart = static_cast<int>(effects.smartVolume);
    auto setSmart = [this](int index) {
        EditEffects([index](EqEffects& target) { target.smartVolume = static_cast<SmartVolume>(index); });
    };
    DrawStepperRow(kIdEqSmart, top, T(Text::SmartVolume), T(kSmartVolumeLabels[smart]), 112.0f, smart > 0,
                   smart < kSmartVolumeCount - 1, [setSmart, smart] { setSmart(smart - 1); },
                   [setSmart, smart] { setSmart(smart + 1); },
                   [this, smart, setSmart](const D2D1_RECT_F& anchor) {
                       std::vector<MenuItem> items;
                       for (const Text label : kSmartVolumeLabels) {
                           items.push_back({T(label)});
                       }
                       OpenMenu(anchor, std::move(items), smart, setSmart);
                   });
    DrawHint(T(Text::SmartVolumeHint), top + kRowHeight - 8.0f, smartHint);
    (void)p;
    return card.bottom + 12.0f;
}

void Panel::DrawEqGraph(const D2D1_RECT_F& rect, const ParametricEq& eq) {
    const Palette& p = P();
    const bool chat = eqChannel_ == EqChannel::Chat;
    const D2D1_COLOR_F accent = chat ? p.tertiary : p.primary;
    const D2D1_COLOR_F onAccent = chat ? p.onTertiary : p.onPrimary;
    const bool enabled = EqChannelState().enabled;

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
        return mid - static_cast<float>(std::clamp(db, -static_cast<double>(kGraphRange), static_cast<double>(kGraphRange)) /
                                        kGraphRange) *
                         half;
    };
    auto dbOf = [=](float y) { return (mid - y) / half * kGraphRange; };

    // Grid with octave-ish frequency marks and 6 dB steps.
    constexpr std::pair<double, const wchar_t*> kMarks[] = {{50, L"50"},   {100, L"100"}, {200, L"200"}, {500, L"500"},
                                                            {1000, L"1k"}, {2000, L"2k"}, {5000, L"5k"}, {10000, L"10k"}};
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
    std::stable_partition(order.begin(), order.end(), [this](int band) { return band != eqBand_; });
    for (const int band : order) {
        const EqBand& settings = eq.bands[band];
        const float x = xOf(settings.frequency);
        const float y = UsesGain(settings.type) ? yOf(settings.gain) : yOf(0);
        const bool selected = band == eqBand_;
        const uint32_t id = PartId(kIdEqGraph, static_cast<uint32_t>(band) + 1);
        const float grow = Animate(AnimKey(id, 0), selected ? 1.0f : (hotId_ == id ? 0.5f : 0.0f), 22.0f);
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
        hit.drag = [this, band, id, x, y, hzOf, dbOf](float pointerX, float pointerY) {
            // The handle keeps its offset from the pointer, so grabbing it does not make it jump.
            if (dragId_ != id) {
                dragId_ = id;
                dragOffsetX_ = x - pointerX;
                dragOffsetY_ = y - pointerY;
                eqBand_ = band;
                Invalidate();
                return;
            }
            const float frequency = RoundFrequency(hzOf(pointerX + dragOffsetX_));
            const float gain = std::clamp(std::round(dbOf(pointerY + dragOffsetY_) * 2.0f) / 2.0f, -kEqMaxGain, kEqMaxGain);
            EditEq([band, frequency, gain](ParametricEq& target) {
                EqBand& edited = target.bands[band];
                edited.frequency = frequency;
                if (UsesGain(edited.type)) {
                    edited.gain = gain;
                }
            });
        };
        hit.wheel = [this, band](int notches) {
            eqBand_ = band;
            EditEq([band, notches](ParametricEq& target) {
                EqBand& edited = target.bands[band];
                edited.q = std::clamp(std::round(edited.q * std::pow(kQStep, static_cast<float>(notches)) * 100.0f) / 100.0f,
                                      kEqMinQ, kEqMaxQ);
            });
        };
        hit.doubleClick = [this, band] {
            eqBand_ = band;
            EditEq([band](ParametricEq& target) { target.bands[band].gain = 0.0f; });
        };
        AddHit(std::move(hit));
    }
}

float Panel::DrawBandEditor(float y, const ParametricEq& eq) {
    const Palette& p = P();
    const int band = eqBand_;
    const EqBand& settings = eq.bands[band];
    const D2D1_RECT_F card = Card(y, 2.0f * kListPadding + 6.0f * kRowHeight + 52.0f);
    float top = card.top + kListPadding;

    // Band selector with its on/off switch.
    const std::wstring title = std::wstring(T(Text::Band)) + L" " + std::to_wstring(band + 1);
    canvas_.Text(title, {36.0f, top, 180.0f, top + kRowHeight}, TextStyle::BodyLarge, p.onSurface);
    const float switchLeft = kWidth - 36.0f - 52.0f;
    IconButton(PartId(kIdEqBandSelect, 1), {switchLeft - 76.0f, top + kRowHeight / 2.0f}, kGlyphChevronLeft, p.onSurface,
               [this] {
                   eqBand_ = (eqBand_ + kParametricBands - 1) % kParametricBands;
                   Invalidate();
               });
    IconButton(PartId(kIdEqBandSelect, 2), {switchLeft - 32.0f, top + kRowHeight / 2.0f}, kGlyphChevronRight, p.onSurface,
               [this] {
                   eqBand_ = (eqBand_ + 1) % kParametricBands;
                   Invalidate();
               });
    const D2D1_RECT_F track{switchLeft, top + 12.0f, kWidth - 36.0f, top + 44.0f};
    DrawSwitch(track, settings.enabled, kIdEqBandToggle);
    AddHit({Inset(track, -6.0f, -6.0f), kIdEqBandToggle, [this, band] {
                EditEq([band](ParametricEq& target) { target.bands[band].enabled = !target.bands[band].enabled; });
            }});
    top += kRowHeight;

    const int type = static_cast<int>(settings.type);
    auto setType = [this, band](int index) {
        EditEq([band, index](ParametricEq& target) { target.bands[band].type = static_cast<FilterType>(index); });
    };
    DrawStepperRow(kIdEqType, top, T(Text::FilterType), T(kFilterLabels[type]), 112.0f, type > 0,
                   type < kFilterTypeCount - 1, [setType, type] { setType(type - 1); },
                   [setType, type] { setType(type + 1); },
                   [this, type, setType](const D2D1_RECT_F& anchor) {
                       std::vector<MenuItem> items;
                       for (const Text label : kFilterLabels) {
                           items.push_back({T(label)});
                       }
                       OpenMenu(anchor, std::move(items), type, setType);
                   });
    top += kRowHeight;

    DrawStepperRow(kIdEqFrequency, top, T(Text::Frequency), FormatFrequency(settings.frequency), 88.0f,
                   settings.frequency > kEqMinFrequency, settings.frequency < kEqMaxFrequency,
                   [this, band] {
                       EditEq([band](ParametricEq& target) {
                           target.bands[band].frequency = RoundFrequency(target.bands[band].frequency / kFrequencyStep);
                       });
                   },
                   [this, band] {
                       EditEq([band](ParametricEq& target) {
                           target.bands[band].frequency = RoundFrequency(target.bands[band].frequency * kFrequencyStep);
                       });
                   });
    top += kRowHeight;

    const bool gainUsed = UsesGain(settings.type);
    DrawStepperRow(kIdEqGain, top, T(Text::Level), gainUsed ? FormatDecibels(settings.gain) : L"—", 88.0f,
                   gainUsed && settings.gain > -kEqMaxGain, gainUsed && settings.gain < kEqMaxGain,
                   [this, band] {
                       EditEq([band](ParametricEq& target) {
                           target.bands[band].gain = std::max(-kEqMaxGain, target.bands[band].gain - 0.5f);
                       });
                   },
                   [this, band] {
                       EditEq([band](ParametricEq& target) {
                           target.bands[band].gain = std::min(kEqMaxGain, target.bands[band].gain + 0.5f);
                       });
                   });
    top += kRowHeight;

    DrawStepperRow(kIdEqQ, top, L"Q", FormatQ(settings.q), 88.0f, settings.q > kEqMinQ, settings.q < kEqMaxQ,
                   [this, band] {
                       EditEq([band](ParametricEq& target) {
                           target.bands[band].q =
                               std::max(kEqMinQ, std::round(target.bands[band].q / kQStep * 100.0f) / 100.0f);
                       });
                   },
                   [this, band] {
                       EditEq([band](ParametricEq& target) {
                           target.bands[band].q =
                               std::min(kEqMaxQ, std::round(target.bands[band].q * kQStep * 100.0f) / 100.0f);
                       });
                   });
    top += kRowHeight;

    DrawStepperRow(kIdEqPreamp, top, T(Text::Preamp), FormatDecibels(eq.preamp), 88.0f, eq.preamp > -kEqMaxGain,
                   eq.preamp < kEqMaxGain,
                   [this] { EditEq([](ParametricEq& target) { target.preamp = std::max(-kEqMaxGain, target.preamp - 0.5f); }); },
                   [this] { EditEq([](ParametricEq& target) { target.preamp = std::min(kEqMaxGain, target.preamp + 0.5f); }); });
    top += kRowHeight;

    const float buttonWidth = (kWidth - 64.0f) / 3.0f;
    const float buttonsTop = top + 8.0f;
    TextButton(kIdEqReset2, {32.0f, buttonsTop, 32.0f + buttonWidth, buttonsTop + 32.0f}, kGlyphReset, T(Text::EqReset),
               false, [this] { EditEq([](ParametricEq& target) { AssignBands(target, FlatEq()); }); });
    TextButton(kIdEqSaveFile, {32.0f + buttonWidth, buttonsTop, 32.0f + 2.0f * buttonWidth, buttonsTop + 32.0f},
               kGlyphSave, T(Text::EqSave), false, [this] { RunFileDialog(FileDialog::SaveEqPreset); });
    TextButton(kIdEqImportFile, {32.0f + 2.0f * buttonWidth, buttonsTop, kWidth - 32.0f, buttonsTop + 32.0f},
               kGlyphImport, T(Text::EqImport), false, [this] { RunFileDialog(FileDialog::ImportEqPreset); });
    return card.bottom + 12.0f;
}

void Panel::DrawEqPresets() {
    const Palette& p = P();
    DrawHeader(T(Text::Presets), eqChannel_ == EqChannel::Game ? T(Text::Game) : T(Text::Chat));
    float y = BeginScroll(Page::EqPresets, kHeaderHeight);

    constexpr float kGap = 8.0f;
    const float buttonWidth = (kWidth - 32.0f - 2.0f * kGap) / 3.0f;
    const float second = 16.0f + buttonWidth + kGap;
    const float third = second + buttonWidth + kGap;
    TextButton(kIdEqSaveFile, {16.0f, y, 16.0f + buttonWidth, y + 32.0f}, kGlyphSave, T(Text::EqSave), true,
               [this] { RunFileDialog(FileDialog::SaveEqPreset); });
    TextButton(kIdEqImportFile, {second, y, second + buttonWidth, y + 32.0f}, kGlyphImport, T(Text::EqImport), true,
               [this] { RunFileDialog(FileDialog::ImportEqPreset); });
    TextButton(kIdEqFolder2, {third, y, kWidth - 16.0f, y + 32.0f}, kGlyphFolder, T(Text::EqFolder),
               true, [] {
                   // explorer.exe needs no COM apartment, unlike ShellExecute on this MTA thread.
                   std::wstring command = L"explorer.exe \"" + ParametricPresetDirectory() + L"\"";
                   STARTUPINFOW startup{};
                   startup.cb = sizeof(startup);
                   PROCESS_INFORMATION process{};
                   if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup,
                                      &process)) {
                       CloseHandle(process.hThread);
                       CloseHandle(process.hProcess);
                   }
               });
    y += 48.0f;

    const std::wstring current = EqChannelState().preset;
    const D2D1_COLOR_F accent = eqChannel_ == EqChannel::Chat ? p.tertiary : p.primary;
    constexpr Text kSections[] = {Text::PresetsBuiltin, Text::PresetsSonar, Text::MyPresets};
    for (int section = 0; section < 3; ++section) {
        const std::vector<NamedEq>& presets = eqLibrary_[section];
        if (presets.empty()) {
            continue;
        }
        y = SectionTitle(T(kSections[section]), y);
        const D2D1_RECT_F card = Card(y, 2.0f * kListPadding + presets.size() * kRowHeight);
        float top = card.top + kListPadding;
        for (const auto& preset : presets) {
            const uint32_t id = HashId(kKindEqPreset, std::to_wstring(section) + preset.name);
            const D2D1_RECT_F row{20.0f, top, kWidth - 20.0f, top + kRowHeight};
            const bool chosen = _wcsicmp(preset.name.c_str(), current.c_str()) == 0;
            StateLayer(id, row, 16.0f, p.onSurface);
            canvas_.Text(preset.name, {36.0f, top, kWidth - 64.0f, top + kRowHeight}, TextStyle::BodyLarge,
                         chosen ? accent : p.onSurface);
            if (chosen) {
                canvas_.Glyph(kGlyphCheck, {kWidth - 44.0f, top + kRowHeight / 2.0f}, 16.0f, accent);
            }
            AddHit({row, id, [this, preset] {
                        ApplyEqPreset(preset);
                        GoBack();
                    }});
            top += kRowHeight;
        }
        y = card.bottom + 16.0f;
    }
    EndScroll(Page::EqPresets, kHeaderHeight, y);
}

}  // namespace ui
