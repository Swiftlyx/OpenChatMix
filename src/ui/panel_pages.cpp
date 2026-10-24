// Headset settings, device picker, routing and snackbar parts of the panel.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "../app_info.h"
#include "panel.h"
#include "panel_internal.h"
#include "widgets.h"

namespace ui {

using namespace detail;

namespace {

constexpr const wchar_t* kBandLabels[kEqBands] = {L"32", L"64", L"125", L"250", L"500",
                                                  L"1k", L"2k", L"4k",  L"8k",  L"16k"};
constexpr float kEqTrackHeight = 120.0f;
constexpr float kSliderRowHeight = design::kChoiceRowHeight;
constexpr size_t kMaxSegments = 5;  // Longer option lists use a dropdown.
constexpr float kPresetRowHeight = 40.0f;  // A saved preset in the headset equalizer card.
constexpr SliderStyle kRowSlider{12.0f, 5.0f, 32.0f};

std::wstring OptionLabel(const SettingOption& option) {
    if (option.literal) {
        return option.literal;
    }
    if (option.minutes > 0) {
        return std::to_wstring(option.minutes) + L" " + T(Text::MinutesShort);
    }
    return T(option.text);
}

std::wstring FormatDb(int halfSteps) {
    if (halfSteps == 0) {
        return L"0";
    }
    std::wstring text = halfSteps > 0 ? L"+" : L"−";
    const int magnitude = std::abs(halfSteps);
    text += std::to_wstring(magnitude / 2);
    if (magnitude % 2) {
        text += L".5";
    }
    return text;
}

float RowHeight(const SettingSpec& spec) {
    switch (spec.kind) {
    case SettingKind::Slider: return kSliderRowHeight;
    case SettingKind::Choice: return spec.options.size() <= kMaxSegments ? design::kChoiceRowHeight : kRowHeight;
    case SettingKind::Toggle: break;
    }
    return kRowHeight;
}

constexpr IdleAnimationKind kIdleAnimations[] = {IdleAnimationKind::Off,   IdleAnimationKind::Stars,
                                                 IdleAnimationKind::Clock, IdleAnimationKind::Life,
                                                 IdleAnimationKind::Blank, IdleAnimationKind::Image};
constexpr Text kIdleAnimationLabels[] = {Text::OptOff,   Text::IdleStars, Text::IdleClock,
                                         Text::IdleLife, Text::IdleBlank, Text::IdleImage};
constexpr int kIdleDelays[] = {15, 30, 60, 120, 300, 600};
constexpr IconCorner kIconCorners[] = {IconCorner::Off, IconCorner::TopLeft, IconCorner::TopRight,
                                       IconCorner::BottomLeft, IconCorner::BottomRight};
constexpr Text kIconCornerLabels[] = {Text::OptOff, Text::CornerTopLeft, Text::CornerTopRight, Text::CornerBottomLeft,
                                      Text::CornerBottomRight};

std::wstring DelayLabel(int seconds) {
    return seconds < 60 ? std::to_wstring(seconds) + L" " + T(Text::SecondsShort)
                        : std::to_wstring(seconds / 60) + L" " + T(Text::MinutesShort);
}

int OptionIndex(const SettingSpec& spec, int value) {
    for (size_t i = 0; i < spec.options.size(); ++i) {
        if (spec.options[i].value == value) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

}  // namespace

void Panel::SetHeadset(const std::wstring& name, const HeadsetFeatures* features, const HeadsetState& state) {
    headsetName_ = name;
    features_ = features;
    headset_ = state;
    if (!features_ && page_ == Page::Headset) {
        GoTo(Page::Mixer);
    }
    Invalidate();
}

void Panel::SetHeadsetSaving(ULONGLONG deadline, unsigned delayMs) {
    saveDeadline_ = deadline;
    saveDelayMs_ = delayMs;
    savedAt_ = 0;
    Invalidate();
}

void Panel::SetHeadsetSaved() {
    saveDeadline_ = 0;
    savedAt_ = GetTickCount64();
    Invalidate();
}

void Panel::SetRouting(const RoutingView& routing) {
    routing_ = routing;
    Invalidate();
}

void Panel::SetIdleScreen(const IdleScreenView& idle) {
    idle_ = idle;
    Invalidate();
}

void Panel::OpenPicker(RoutingSlot slot) {
    pickSlot_ = slot;
    GoTo(Page::PickDevice);
}

void Panel::DrawHeadset() {
    const Palette& p = P();
    DrawTitle(headsetName_.empty() ? T(Text::HeadsetPage) : headsetName_.c_str());
    if (!features_) {
        return;
    }

    float y = BeginScroll(Page::Headset, kHeaderHeight);
    if (headset_.battery >= 0) {
        std::wstring text = T(Text::Battery);
        text += L"  ";
        if (headset_.headsetOff) {
            text += T(Text::HeadsetOff);
        } else {
            text += Percent(headset_.battery);
            if (headset_.charging) {
                text += std::wstring(L" · ") + T(Text::Charging);
            }
        }
        if (headset_.spareBattery >= 0) {
            text += std::wstring(L" · ") + T(Text::SpareBattery) + L" " + Percent(headset_.spareBattery);
        }
        const wchar_t glyph = static_cast<wchar_t>((headset_.charging ? kGlyphBatteryCharging0 : kGlyphBattery0) +
                                                   std::clamp(headset_.battery / 10, 0, 10));
        canvas_.Glyph(glyph, {kInset + 6.0f, y + 12.0f}, 16.0f, p.onSurfaceVariant);
        canvas_.Text(text, {kInset + 20.0f, y, kWidth - kInset, y + 24.0f}, TextStyle::BodyMedium, p.onSurfaceVariant);
        y += 24.0f + design::kSpace2;
    }
    if (features_->experimental) {
        y = PageHint(T(Text::HeadsetExperimental), y);
    }
    if (features_->equalizer.bandsCommand) {
        y = DrawEqualizer(y);
    }
    for (const auto& section : features_->sections) {
        y = SectionTitle(T(section.title), y);
        std::vector<CardRow> rows;
        for (const auto& setting : section.settings) {
            if (setting.shownWith && headset_.values[setting.shownWith] != setting.shownWithValue) {
                continue;
            }
            const SettingSpec* spec = &setting;
            const bool toggle = setting.kind == SettingKind::Toggle;
            rows.push_back({RowHeight(setting), [this, spec](float top) { DrawSettingRow(*spec, top); },
                            toggle ? HashId(kKindSetting, std::to_wstring(setting.command)) : 0u});
        }
        y = DrawCard(y, rows) + design::kSectionGap;
    }
    if (features_->oled) {
        y = DrawVisualizer(y);
        y = DrawIdleScreen(y);
    }
    EndScroll(Page::Headset, kHeaderHeight, y - design::kSectionGap);
}

EqCurve Panel::ActiveCurve() const {
    if (!features_) {
        return {};
    }
    const EqualizerSpec& eq = features_->equalizer;
    // A preset plays its own curve, whether it is stored on the device or sent as band values.
    const bool preset = headset_.eqPreset >= 0 && headset_.eqPreset < eq.CustomIndex();
    return preset ? eq.presets[headset_.eqPreset].curve : headset_.eqCurve;
}

EqFilters Panel::ActiveHeadsetFilters() const {
    return features_ ? ActiveFilters(features_->equalizer, headset_) : EqFilters{};
}

EqCurve Panel::ActiveExtraCurve(ExtraEqualizer which) const {
    if (!features_) {
        return {};
    }
    const EqualizerSpec& eq = features_->Extra(which);
    const HeadsetState::ExtraEq& state = headset_.extraEq[static_cast<int>(which)];
    const bool preset = state.preset >= 0 && state.preset < eq.CustomIndex();
    return preset ? eq.presets[state.preset].curve : state.curve;
}

void Panel::ApplyHeadsetGamePreset(const NamedEq& preset) {
    if (!features_) {
        return;
    }
    const EqualizerSpec& spec = features_->equalizer;
    // The replaced custom setting is kept as a saved preset first, named with the time.
    auto backup = [this](const std::function<bool(const std::wstring& path, const std::wstring& name)>& write) {
        SYSTEMTIME now{};
        GetLocalTime(&now);
        wchar_t stamp[32];
        swprintf_s(stamp, L"%04u-%02u-%02u %02u.%02u", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute);
        const std::wstring name = std::wstring(T(Text::CurveBackup)) + L" " + stamp;
        if (write(PresetDirectory() + L"\\" + name + L".json", name)) {
            userPresets_ = LoadUserPresets();
        }
    };
    if (spec.Parametric()) {
        // A parametric headset plays the preset's own filters.
        const EqFilters filters = PresetFilters(preset.eq, spec.notch);
        const EqFilters& current = headset_.eqFilters;
        const bool flat = std::all_of(current.begin(), current.end(), [](const EqBand& band) {
            return !band.enabled || (UsesGain(band.type) && std::fabs(band.gain) < 0.05f);
        });
        const bool saved = std::any_of(userPresets_.begin(), userPresets_.end(), [&](const UserPreset& user) {
            return PresetFilters(user, spec.range) == current;
        });
        if (headset_.eqFiltersKnown && !flat && !saved && current != filters) {
            backup([&current](const std::wstring& path, const std::wstring& name) {
                return WritePresetFilters(path, name, current);
            });
        }
        SetHeadsetFilters(filters);
        return;
    }
    const auto gains = GraphicEqGains(preset.eq, spec.range / 2.0f);
    EqCurve curve{};
    for (int band = 0; band < kEqBands; ++band) {
        curve[band] = static_cast<int8_t>(std::clamp(std::lround(gains[band] * 2.0f), -static_cast<long>(spec.range),
                                                     static_cast<long>(spec.range)));
    }
    // The custom curve is replaced; keep it unless it is flat or already among the saved presets.
    const EqCurve& current = headset_.eqCurve;
    const bool flat = std::all_of(current.begin(), current.end(), [](int8_t value) { return value == 0; });
    const bool saved = std::any_of(userPresets_.begin(), userPresets_.end(), [&](const UserPreset& user) {
        return PresetCurve(user, spec.range) == current;
    });
    if (headset_.eqCurveKnown && !flat && !saved && current != curve) {
        backup([&current](const std::wstring& path, const std::wstring& name) {
            return WritePresetFile(path, name, current);
        });
    }
    SetHeadsetCurve(curve);
}

// Before the headset saves a run of changes: a ring that empties and the seconds left; after it: Done, fading.
void Panel::DrawSaveState(float right, float centerY) {
    const Palette& p = P();
    const ULONGLONG now = GetTickCount64();
    // Both stages move with time, so they are drawn on every frame, like springs, until Done has faded.
    if (saveDeadline_ || (savedAt_ && now - savedAt_ < kSaveDoneMs)) {
        animating_ = true;
    }
    if (saveDeadline_) {
        const float left = saveDeadline_ > now ? static_cast<float>(saveDeadline_ - now) : 0.0f;
        const int seconds = std::max(1, static_cast<int>(std::ceil(left / 1000.0f)));
        const std::wstring text =
            std::wstring(T(Text::EqSavingIn)) + L" " + std::to_wstring(seconds) + L" " + T(Text::SecondsShort);
        const float textLeft = right - canvas_.Measure(text, TextStyle::LabelMedium);
        canvas_.Text(text, {textLeft, centerY - 10.0f, right + 2.0f, centerY + 10.0f}, TextStyle::LabelMedium,
                     p.onSurfaceVariant);
        const D2D1_POINT_2F center{textLeft - 12.0f, centerY};
        constexpr float kRadius = 6.0f;
        canvas_.StrokeCircle(center, kRadius, Alpha(p.onSurfaceVariant, 0.25f), 2.0f);
        const float fraction = std::clamp(left / static_cast<float>(std::max(1u, saveDelayMs_)), 0.0f, 1.0f);
        const int steps = std::max(2, static_cast<int>(std::ceil(32.0f * fraction)) + 1);
        std::vector<D2D1_POINT_2F> arc;
        for (int i = 0; i < steps; ++i) {
            // Clockwise from the top, the part still to wait.
            const float angle = -1.5707963f + 6.2831853f * fraction * static_cast<float>(i) / (steps - 1);
            arc.push_back({center.x + kRadius * std::cos(angle), center.y + kRadius * std::sin(angle)});
        }
        canvas_.Polyline(arc, p.primary, 2.0f);
        return;
    }
    if (!savedAt_ || now - savedAt_ >= kSaveDoneMs) {
        return;
    }
    // Done fades in quickly and out slowly, both eased.
    const float age = static_cast<float>(now - savedAt_);
    auto ease = [](float t) {
        t = std::clamp(t, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    };
    const float alpha = std::min(ease(age / 250.0f), ease((static_cast<float>(kSaveDoneMs) - age) / 700.0f));
    const std::wstring text = T(Text::EqSaved);
    const float textLeft = right - canvas_.Measure(text, TextStyle::LabelMedium);
    canvas_.Text(text, {textLeft, centerY - 10.0f, right + 2.0f, centerY + 10.0f}, TextStyle::LabelMedium,
                 Alpha(p.primary, alpha));
    canvas_.Glyph(kGlyphCheck, {textLeft - 12.0f, centerY}, 12.0f, Alpha(p.primary, alpha));
}

float Panel::DrawEqualizer(float y) {
    if (headsetEqView_ > 0 && headsetEqView_ <= kExtraEqualizers) {
        const auto which = static_cast<ExtraEqualizer>(headsetEqView_ - 1);
        if (features_->Extra(which).bandsCommand) {
            return DrawExtraEqualizer(y, which);
        }
    }
    if (features_->equalizer.Parametric()) {
        return DrawHeadsetFilters(y);
    }
    const Palette& p = P();
    const EqualizerSpec& eq = features_->equalizer;
    const int custom = eq.CustomIndex();
    const bool devicePreset = eq.presetCommand != 0 && headset_.eqPreset >= 0 && headset_.eqPreset < custom;
    const EqCurve curve = ActiveCurve();
    y = DrawEqualizerTitle(y, [this] { SetHeadsetCurve(EqCurve{}); });

    // One hint under the presets: a preset stored on the headset cannot be edited, an untested format says so.
    const wchar_t* hint = devicePreset ? T(Text::EqPresetActive) : eq.experimental ? T(Text::EqExperimental) : nullptr;
    const float hintHeight = hint ? HintHeight(hint, kWidth - 64.0f) + 4.0f : 0.0f;
    const float bandsHeight = 20.0f + kEqTrackHeight + 30.0f;
    const D2D1_RECT_F card = Card(y, 16.0f + 44.0f + hintHeight + bandsHeight + HeadsetPresetToolsHeight());

    std::vector<const wchar_t*> labels;
    for (const auto& preset : eq.presets) {
        labels.push_back(preset.name);
    }
    labels.push_back(T(Text::EqCustom));
    DrawSegmented({32.0f, y + 16.0f, kWidth - 32.0f, y + 48.0f}, kIdEqPreset, labels, headset_.eqPreset,
                  p.primary, p.onPrimary, [this](int index) { SetHeadsetPreset(index); });
    float inner = y + 60.0f;
    if (hint) {
        canvas_.Text(hint, {32.0f, inner, kWidth - 32.0f, inner + hintHeight}, TextStyle::BodySmall, p.onSurfaceVariant,
                     TextAlign::Leading, true);
        inner += hintHeight;
    }

    // A preset stored on the headset is shown as it plays, but only the custom curve can be edited.
    const float labelsBottom = DrawBandSliders(inner, curve, eq.range, !devicePreset, 0,
                                               [this](const EqCurve& next) { SetHeadsetCurve(next); });
    DrawHeadsetPresetTools(labelsBottom + 6.0f);
    return card.bottom + design::kSectionGap;
}

float Panel::DrawEqualizerTitle(float y, std::function<void()> reset) {
    const Palette& p = P();
    canvas_.Text(T(Text::Equalizer), {kInset, y, 240.0f, y + 20.0f}, TextStyle::TitleSmall, p.primary);
    TextButton(kIdEqReset, {kWidth - kGutter - 104.0f, y - 6.0f, kWidth - kGutter, y + 26.0f}, kGlyphReset,
               T(Text::EqReset), false, std::move(reset));
    if (features_->saveDelayMs) {
        DrawSaveState(kWidth - kGutter - 112.0f, y + 10.0f);
    }
    y += design::kSectionTitleHeight + 2.0f;
    // The headset's equalizers: the sound from the base station (2.4 GHz), and those it has besides.
    std::vector<const wchar_t*> labels{T(Text::Link24Ghz)};
    std::vector<int> views{0};
    constexpr Text kExtraLabels[kExtraEqualizers] = {Text::Bluetooth, Text::Microphone};
    for (int index = 0; index < kExtraEqualizers; ++index) {
        if (features_->Extra(static_cast<ExtraEqualizer>(index)).bandsCommand) {
            labels.push_back(T(kExtraLabels[index]));
            views.push_back(index + 1);
        }
    }
    if (views.size() == 1) {
        return y;
    }
    const auto shown = std::find(views.begin(), views.end(), headsetEqView_);
    DrawSegmented({kGutter, y, kWidth - kGutter, y + 36.0f}, kIdBluetoothEqBase + kIdEqChannel, labels,
                  shown == views.end() ? 0 : static_cast<int>(shown - views.begin()), p.secondaryContainer,
                  p.onSecondaryContainer, [this, views](int index) {
                      headsetEqView_ = views[index];
                      Invalidate();
                  });
    return y + 36.0f + design::kSpace2;
}

float Panel::DrawBandSliders(float inner, const EqCurve& curve, int8_t range, bool editable, uint32_t idBase,
                             std::function<void(const EqCurve&)> change) {
    const Palette& p = P();
    const float valueTop = inner;
    const float trackTop = inner + 20.0f;
    const float trackBottom = trackTop + kEqTrackHeight;
    const float zero = (trackTop + trackBottom) / 2.0f;
    const float half = kEqTrackHeight / 2.0f;
    const float left = 32.0f;
    const float column = (kWidth - 64.0f) / kEqBands;
    const float alpha = editable ? 1.0f : 0.8f;
    for (int band = 0; band < kEqBands; ++band) {
        const float cx = left + column * (band + 0.5f);
        const uint32_t id = idBase + kIdEqBand + band;
        const float value = Animate(AnimKey(id, 0), static_cast<float>(curve[band]), design::kSpringFastEffects);
        const float handleY = zero - value / range * half;
        canvas_.FillRound({cx - 4.0f, trackTop, cx + 4.0f, trackBottom}, 4.0f, Alpha(p.secondaryContainer, alpha));
        if (std::fabs(value) > 0.05f) {
            canvas_.FillRound({cx - 4.0f, std::min(zero, handleY), cx + 4.0f, std::max(zero, handleY)}, 4.0f,
                              Alpha(p.primary, alpha));
        }
        canvas_.FillRect({cx - 7.0f, zero - 0.5f, cx + 7.0f, zero + 0.5f}, Alpha(p.outline, 0.6f * alpha));
        const float handleWidth = Animate(AnimKey(id, 1), pressedId_ == id ? 28.0f : 22.0f, design::kSpringFastEffects);
        canvas_.FillRound({cx - handleWidth / 2.0f, handleY - 3.0f, cx + handleWidth / 2.0f, handleY + 3.0f}, 3.0f,
                          Alpha(curve[band] == 0 ? p.onSurface : p.primary, alpha));
        canvas_.Text(FormatDb(curve[band]), {cx - column / 2.0f, valueTop, cx + column / 2.0f, valueTop + 16.0f},
                     TextStyle::LabelMedium, Alpha(p.onSurfaceVariant, alpha), TextAlign::Center);
        canvas_.Text(kBandLabels[band],
                     {cx - column / 2.0f, trackBottom + 8.0f, cx + column / 2.0f, trackBottom + 24.0f},
                     TextStyle::BodySmall, p.onSurfaceVariant, TextAlign::Center);

        if (!editable) {
            continue;
        }
        Hit hit;
        hit.rect = {cx - column / 2.0f, trackTop - 10.0f, cx + column / 2.0f, trackBottom + 10.0f};
        hit.id = id;
        hit.drag = [band, zero, half, curve, range, change](float, float pointerY) {
            const int target = std::clamp(static_cast<int>(std::lround((zero - pointerY) / half * range)),
                                          -static_cast<int>(range), static_cast<int>(range));
            if (target != curve[band]) {
                EqCurve next = curve;
                next[band] = static_cast<int8_t>(target);
                change(next);
            }
        };
        hit.doubleClick = [band, curve, change] {
            EqCurve next = curve;
            next[band] = 0;
            change(next);
        };
        AddHit(std::move(hit));
    }
    return trackBottom + 24.0f;
}

float Panel::DrawExtraEqualizer(float y, ExtraEqualizer which) {
    const Palette& p = P();
    const EqualizerSpec& eq = features_->Extra(which);
    const HeadsetState::ExtraEq& state = headset_.extraEq[static_cast<int>(which)];
    const bool bluetooth = which == ExtraEqualizer::Bluetooth;
    const uint32_t ids = bluetooth ? kIdBluetoothEqBase : kIdMicrophoneEqBase;
    y = DrawEqualizerTitle(y, [this, which] { SetExtraCurve(which, EqCurve{}); });

    const wchar_t* hint = T(bluetooth ? Text::BluetoothEqHint : Text::MicEqHint);
    const float hintHeight = HintHeight(hint, kWidth - 64.0f) + 4.0f;
    const float bandsHeight = 20.0f + kEqTrackHeight + 24.0f;
    const D2D1_RECT_F card = Card(y, 16.0f + 44.0f + hintHeight + bandsHeight + 16.0f);
    const int custom = eq.CustomIndex();
    if (custom <= 4) {
        // A few short names: all in view as a button group.
        std::vector<const wchar_t*> labels;
        for (const auto& preset : eq.presets) {
            labels.push_back(preset.name);
        }
        labels.push_back(T(Text::EqCustom));
        DrawSegmented({32.0f, y + 16.0f, kWidth - 32.0f, y + 48.0f}, ids + kIdEqPreset, labels, state.preset,
                      p.primary, p.onPrimary, [this, which](int index) { SetExtraPreset(which, index); });
    } else {
        // Longer names (the microphone's): the chosen one as a dropdown.
        const D2D1_RECT_F button{24.0f, y + 12.0f, kWidth - 24.0f, y + 52.0f};
        const uint32_t buttonId = ids + kIdEqPresetButton;
        const float middle = (button.top + button.bottom) / 2.0f;
        StateLayer(buttonId, button, design::kRadiusMedium, p.onSurface);
        canvas_.Glyph(kGlyphEqualizer, {button.left + 20.0f, middle}, 16.0f, p.primary);
        const std::wstring name = state.preset >= 0 && state.preset < custom ? std::wstring(eq.presets[state.preset].name)
                                  : state.preset == custom                     ? std::wstring(T(Text::EqCustom))
                                                                               : std::wstring(L"—");
        const float nameWidth = std::min(canvas_.Measure(name, TextStyle::TitleSmall), button.right - button.left - 72.0f);
        canvas_.Text(name, {button.left + 40.0f, button.top, button.left + 41.0f + nameWidth, button.bottom},
                     TextStyle::TitleSmall, p.onSurface);
        canvas_.Glyph(kGlyphChevronDown, {button.left + 52.0f + nameWidth, middle}, 10.0f, p.onSurfaceVariant);
        AddHit({button, buttonId, [this, which, button] {
                    const EqualizerSpec& spec = features_->Extra(which);
                    std::vector<MenuItem> items;
                    for (const auto& preset : spec.presets) {
                        items.push_back({preset.name});
                    }
                    items.push_back({T(Text::EqCustom)});
                    OpenMenu(button, std::move(items), headset_.extraEq[static_cast<int>(which)].preset,
                             [this, which](int index) { SetExtraPreset(which, index); });
                }});
    }
    const float inner = y + 60.0f;
    canvas_.Text(hint, {32.0f, inner, kWidth - 32.0f, inner + hintHeight}, TextStyle::BodySmall, p.onSurfaceVariant,
                 TextAlign::Leading, true);
    DrawBandSliders(inner + hintHeight, ActiveExtraCurve(which), eq.range, true, ids,
                    [this, which](const EqCurve& next) { SetExtraCurve(which, next); });
    return card.bottom + design::kSectionGap;
}

float Panel::HeadsetPresetToolsHeight() {
    const float gameHint = HintHeight(T(Text::GamePresetsHint), kWidth - 64.0f);
    const float gameHeight = 44.0f + gameHint;
    // Saved presets: one row each, applied by a click, renamed, copied and deleted in place.
    const size_t saved = userPresets_.size();
    const float presetsHeight =
        saved == 0 ? 0.0f : 24.0f + saved * kPresetRowHeight + (saved - 1) * design::kSegmentGap + 8.0f;
    return 44.0f + gameHeight + presetsHeight + 12.0f;
}

void Panel::DrawHeadsetPresetTools(float actions) {
    const Palette& p = P();
    const EqualizerSpec& eq = features_->equalizer;
    const int custom = eq.CustomIndex();
    const float gameHint = HintHeight(T(Text::GamePresetsHint), kWidth - 64.0f);
    const float gameHeight = 44.0f + gameHint;
    const size_t saved = userPresets_.size();

    // Preset file actions.
    const float actionWidth = (kWidth - 64.0f) / 3.0f;
    TextButton(kIdEqSave, {32.0f, actions, 32.0f + actionWidth, actions + 32.0f}, kGlyphSave, T(Text::EqSave), false,
               [this] { RunFileDialog(FileDialog::SavePreset); });
    TextButton(kIdEqImport, {32.0f + actionWidth, actions, 32.0f + 2.0f * actionWidth, actions + 32.0f}, kGlyphImport,
               T(Text::EqImport), false, [this] { RunFileDialog(FileDialog::ImportPreset); });
    TextButton(kIdEqFolder, {32.0f + 2.0f * actionWidth, actions, kWidth - 32.0f, actions + 32.0f}, kGlyphFolder,
               T(Text::EqFolder), false, [] { OpenWithExplorer(PresetDirectory()); });

    // Game presets of every source, converted to the headset's bands; the headset's own presets sit above.
    const float gameTop = actions + 44.0f;
    const D2D1_RECT_F gameButton{32.0f, gameTop, kWidth - 32.0f, gameTop + 36.0f};
    TextButton(kIdGamePresets, gameButton, kGlyphGame, T(Text::GamePresets), true, [this, gameButton] {
        if (eqLibraryChannel_ != EqChannel::Game) {
            LoadEqLibrary(EqChannel::Game);
        }
        std::vector<const NamedEq*> presets;
        std::vector<MenuItem> items = EqLibraryMenu(presets, features_->equalizer.presets);
        std::vector<NamedEq> chosen;
        for (const NamedEq* preset : presets) {
            chosen.push_back(preset ? *preset : NamedEq{});
        }
        OpenMenu(
            gameButton, std::move(items), -1,
            [this, chosen = std::move(chosen)](int index) { ApplyHeadsetGamePreset(chosen[index]); }, true);
    });
    canvas_.Text(T(Text::GamePresetsHint), {32.0f, gameTop + 40.0f, kWidth - 32.0f, gameTop + 40.0f + gameHint},
                 TextStyle::BodySmall, p.onSurfaceVariant, TextAlign::Leading, true);

    if (saved > 0) {
        const float labelTop = actions + 44.0f + gameHeight;
        canvas_.Text(T(Text::MyPresets), {32.0f, labelTop, kWidth - 32.0f, labelTop + 18.0f}, TextStyle::BodySmall,
                     p.onSurfaceVariant);
        float rowTop = labelTop + 24.0f;
        const std::vector<UserPreset> presets = userPresets_;  // Actions below reload the list.
        const bool parametric = eq.Parametric();
        const EqCurve curve = ActiveCurve();
        const EqFilters filters = ActiveHeadsetFilters();
        for (const UserPreset& preset : presets) {
            const D2D1_RECT_F row{24.0f, rowTop, kWidth - 24.0f, rowTop + kPresetRowHeight};
            if (parametric) {
                const EqFilters presetFilters = PresetFilters(preset, eq.range);
                DrawPresetRow(preset, row, presetFilters == filters,
                              [this, presetFilters] { SetHeadsetFilters(presetFilters); });
            } else {
                const EqCurve presetCurve = PresetCurve(preset, eq.range);
                const bool active = presetCurve == curve && (eq.presetCommand == 0 || headset_.eqPreset == custom);
                DrawPresetRow(preset, row, active, [this, presetCurve] { SetHeadsetCurve(presetCurve); });
            }
            rowTop += kPresetRowHeight + design::kSegmentGap;
        }
    }
}

void Panel::DrawPresetRow(const UserPreset& preset, const D2D1_RECT_F& row, bool active, std::function<void()> apply) {
    const Palette& p = P();
    const uint32_t id = HashId(kKindPreset, preset.path);
    const uint32_t fieldId = PartId(id, 4);
    const float middle = (row.top + row.bottom) / 2.0f;
    const float button = design::kIconButtonSize - design::kSpace1;
    const float deleteX = row.right - button / 2.0f;
    const float copyX = deleteX - button;
    const float renameX = copyX - button;
    const float nameRight = renameX - button / 2.0f - design::kSpace1;
    auto reload = [this] {
        userPresets_ = LoadUserPresets();
        Invalidate();
    };
    // A click on the row applies the preset; the chosen one is filled.
    if (active) {
        canvas_.FillRound(row, design::kRadiusMedium, p.secondaryContainer);
    }
    StateLayer(id, row, design::kRadiusMedium, p.onSurface);
    AddHit({row, id, std::move(apply)});

    // The name renames in place: Enter keeps it, Esc leaves it as it was.
    auto rename = [preset, reload](const std::wstring& text) {
        std::wstring name = text;
        while (!name.empty() && name.back() == L' ') {
            name.pop_back();
        }
        if (name.empty()) {
            return false;
        }
        if (name != preset.name) {
            RenamePreset(preset, name);
            reload();
        }
        return true;
    };
    if (field_.id == fieldId) {
        DrawEditableValue(fieldId, {row.left + design::kSpace1, middle - design::kFieldHeight / 2.0f, nameRight,
                                    middle + design::kFieldHeight / 2.0f},
                          preset.name, rename, false);
    } else {
        canvas_.Text(preset.name, {row.left + design::kSpace3, row.top, nameRight, row.bottom}, TextStyle::LabelLarge,
                     active ? p.onSecondaryContainer : p.onSurface);
    }
    const D2D1_COLOR_F icons = active ? p.onSecondaryContainer : p.onSurfaceVariant;
    IconButton(PartId(id, 1), {renameX, middle}, kGlyphEdit, icons,
               [this, fieldId, preset, rename] { BeginField(fieldId, preset.name, rename, false); });
    Tooltip(PartId(id, 1), T(Text::TipRename));
    IconButton(PartId(id, 2), {copyX, middle}, kGlyphCopy, icons, [this, preset, reload] {
        // "Name (copy)", then "Name (copy 2)" and so on, so copies stay apart.
        const std::wstring base = preset.name + L" (" + T(Text::CopySuffix);
        std::wstring name = base + L")";
        for (int number = 2; std::any_of(userPresets_.begin(), userPresets_.end(),
                                         [&](const UserPreset& other) { return other.name == name; });
             ++number) {
            name = base + L" " + std::to_wstring(number) + L")";
        }
        CopyPreset(preset, name);
        reload();
    });
    Tooltip(PartId(id, 2), T(Text::TipCopy));
    // Deleted presets go to the Recycle Bin, so there is nothing to confirm.
    IconButton(PartId(id, 3), {deleteX, middle}, kGlyphDelete, p.error, [preset, reload] {
        DeletePreset(preset);
        reload();
    });
    Tooltip(PartId(id, 3), T(Text::TipDelete));
}

float Panel::DrawSettingRow(const SettingSpec& spec, float top) {
    const Palette& p = P();
    const float height = RowHeight(spec);
    const uint32_t id = HashId(kKindSetting, std::to_wstring(spec.command));
    const int value = headset_.values[spec.command];
    const SettingSpec* setting = &spec;
    const float controlBottom = top + design::kChoiceRowHeight - design::kHintBottomGap;

    switch (spec.kind) {
    case SettingKind::Toggle:
        DrawSwitchRow(id, top, T(spec.label), value == spec.max, [this, setting, value] {
            host_.OnHeadsetSetting(setting->command, value == setting->max ? setting->min : setting->max);
        });
        break;

    case SettingKind::Slider: {
        // No room for a second line above the slider, so long labels use the smaller style.
        const D2D1_RECT_F labelRect{kInset, top + design::kHintBottomGap, kWidth - 110.0f, top + 34.0f};
        const bool fits = canvas_.Measure(T(spec.label), TextStyle::BodyLarge) <= labelRect.right - labelRect.left;
        canvas_.Text(T(spec.label), labelRect, fits ? TextStyle::BodyLarge : TextStyle::BodyMedium, p.onSurface);
        const bool off = value == 0 && spec.wire == SettingWire::SwitchedLevel;
        canvas_.Text(value < 0 ? L"—" : off ? std::wstring(T(Text::OptOff)) : std::to_wstring(value),
                     {kWidth - 110.0f, labelRect.top, kWidth - kInset, labelRect.bottom}, TextStyle::LabelLarge,
                     p.onSurfaceVariant, TextAlign::Trailing);
        const float left = kInset + design::kSpace1;
        const float right = kWidth - kInset - design::kSpace1;
        const float centerY = controlBottom - design::kControlHeight / 2.0f;
        const float t = value >= 0 ? static_cast<float>(value - spec.min) / (spec.max - spec.min) : 0.0f;
        const float shown = Animate(AnimKey(id, 0), t, design::kSpringDefaultSpatial);
        const float handleWidth = Animate(AnimKey(id, 1), pressedId_ == id ? 2.0f : 4.0f, design::kSpringFastSpatial);
        DrawValueSlider(canvas_, p, left, right, centerY, std::clamp(shown, 0.0f, 1.0f), handleWidth, kRowSlider,
                        value >= 0);
        Hit hit;
        hit.rect = {left - 12.0f, centerY - 18.0f, right + 12.0f, centerY + 18.0f};
        hit.id = id;
        hit.drag = [this, setting, left, right](float x, float) {
            const float ratio = std::clamp((x - left) / (right - left), 0.0f, 1.0f);
            const int target = setting->min + static_cast<int>(std::lround(ratio * (setting->max - setting->min)));
            if (target != headset_.values[setting->command]) {
                host_.OnHeadsetSetting(setting->command, target);
            }
        };
        AddHit(std::move(hit));
        break;
    }

    case SettingKind::Choice: {
        const int selected = OptionIndex(spec, value);
        if (spec.options.size() <= kMaxSegments) {
            // A few short options: all in view as a button group.
            canvas_.Text(T(spec.label), {kInset, top + design::kHintBottomGap, kWidth - kInset, top + 34.0f},
                         TextStyle::BodyLarge, p.onSurface);
            std::vector<std::wstring> texts;
            for (const auto& option : spec.options) {
                texts.push_back(OptionLabel(option));
            }
            std::vector<const wchar_t*> labels;
            for (const auto& text : texts) {
                labels.push_back(text.c_str());
            }
            DrawSegmented({kInset, controlBottom - design::kControlHeight, kWidth - kInset, controlBottom}, id, labels,
                          selected, p.primary, p.onPrimary, [this, setting](int index) {
                              host_.OnHeadsetSetting(setting->command, setting->options[index].value);
                          });
        } else {
            // More options: a dropdown with the whole list.
            const D2D1_RECT_F field = DrawDropdown(
                id, kWidth - kInset, top + kRowHeight / 2.0f,
                selected >= 0 ? OptionLabel(spec.options[selected]) : L"—", 200.0f,
                [this, setting, selected](const D2D1_RECT_F& anchor) {
                    std::vector<MenuItem> items;
                    for (const auto& option : setting->options) {
                        items.push_back({OptionLabel(option)});
                    }
                    OpenMenu(anchor, std::move(items), selected, [this, setting](int index) {
                        host_.OnHeadsetSetting(setting->command, setting->options[index].value);
                    });
                });
            DrawRowLabel(T(spec.label), {kInset, top, field.left - design::kSpace3, top + kRowHeight}, p.onSurface);
        }
        break;
    }
    }
    return top + height;
}

float Panel::DrawVisualizer(float y) {
    y = SectionTitle(T(Text::Visualizer), y);
    const VisualizerSettings& visualizer = settings_.visualizer;
    std::vector<CardRow> rows{SwitchItem(kIdVisualizer, T(Text::VisualizerShow), visualizer.enabled, [this] {
        Edit([](Settings& s) { s.visualizer.enabled = !s.visualizer.enabled; });
    }, T(Text::VisualizerHint))};
    if (visualizer.enabled) {
        rows.push_back(ChoiceItem(kIdVisualizerFor, T(Text::VisualizerFor),
                                  {T(Text::VisualizerMedia), T(Text::VisualizerSound)}, visualizer.mediaOnly ? 0 : 1,
                                  [this](int choice) { Edit([choice](Settings& s) { s.visualizer.mediaOnly = choice == 0; }); },
                                  T(visualizer.mediaOnly ? Text::VisualizerMediaHint : Text::VisualizerSoundHint)));
    }
    return DrawCard(y, rows) + design::kSectionGap;
}

float Panel::DrawIdleScreen(float y) {
    const Palette& p = P();
    if (idle_.showing) {
        canvas_.Text(T(Text::IdleShowing), {kInset, y, kWidth - kInset, y + design::kSpace5}, TextStyle::BodySmall,
                     p.primary, TextAlign::Trailing);
    }
    y = SectionTitle(T(Text::IdleScreen), y);
    const bool on = idle_.animation != IdleAnimationKind::Off;
    const bool image = idle_.animation == IdleAnimationKind::Image;
    std::vector<CardRow> rows;

    const int index = static_cast<int>(std::find(std::begin(kIdleAnimations), std::end(kIdleAnimations),
                                                 idle_.animation) -
                                       std::begin(kIdleAnimations));
    rows.push_back(DropdownItem(
        HashId(kKindIdle, L"animation"), T(Text::IdleAnimation), index, static_cast<int>(std::size(kIdleAnimations)),
        [](int i) { return std::wstring(T(kIdleAnimationLabels[i])); },
        [this](int i) { host_.OnIdleAnimation(kIdleAnimations[i]); }));

    if (on) {
        const int delays = static_cast<int>(std::size(kIdleDelays));
        // The nearest listed delay, so values edited in the ini file still show sensibly.
        int delay = 0;
        for (int i = 1; i < delays; ++i) {
            if (std::abs(kIdleDelays[i] - idle_.delaySeconds) < std::abs(kIdleDelays[delay] - idle_.delaySeconds)) {
                delay = i;
            }
        }
        rows.push_back(DropdownItem(
            HashId(kKindIdle, L"delay"), T(Text::IdleDelay), delay, delays,
            [](int i) { return DelayLabel(kIdleDelays[i]); }, [this](int i) { host_.OnIdleDelay(kIdleDelays[i]); }));
    }

    if (on && idle_.bluetooth) {
        const int corner = static_cast<int>(std::find(std::begin(kIconCorners), std::end(kIconCorners),
                                                      idle_.bluetoothIcon) -
                                            std::begin(kIconCorners));
        rows.push_back(DropdownItem(
            HashId(kKindIdle, L"bluetooth"), T(Text::IdleBluetoothIcon), corner,
            static_cast<int>(std::size(kIconCorners)), [](int i) { return std::wstring(T(kIconCornerLabels[i])); },
            [this](int i) { host_.OnIdleBluetoothIcon(kIconCorners[i]); }, T(Text::IdleBluetoothIconHint)));
    }
    if (image) {
        rows.push_back({design::kTwoLineRowHeight, [this, &p](float top) {
                            const float right = kWidth - kInset;
                            const float buttonTop = top + (design::kTwoLineRowHeight - design::kControlHeight) / 2.0f;
                            canvas_.Text(T(Text::IdleFile), {kInset, top + 9.0f, right - 120.0f, top + 31.0f},
                                         TextStyle::BodyLarge, p.onSurface);
                            canvas_.Text(idle_.imageName.empty() ? T(Text::NotSelected) : idle_.imageName.c_str(),
                                         {kInset, top + 31.0f, right - 120.0f, top + 50.0f}, TextStyle::BodySmall,
                                         p.onSurfaceVariant);
                            TextButton(kIdIdleChoose,
                                       {right - 108.0f, buttonTop, right, buttonTop + design::kControlHeight},
                                       kGlyphFolder, T(Text::IdleChoose), true,
                                       [this] { RunFileDialog(FileDialog::IdleImage); });
                        }});
    }
    if (on) {
        // Choosing an animation only saves it; this shows it on the headset to try.
        rows.push_back({design::kTwoLineRowHeight, [this, &p](float top) {
                            const float right = kWidth - kInset;
                            const float buttonTop = top + (design::kTwoLineRowHeight - design::kControlHeight) / 2.0f;
                            canvas_.Text(T(Text::IdleTest), {kInset, top + 9.0f, right - 120.0f, top + 31.0f},
                                         TextStyle::BodyLarge, p.onSurface);
                            canvas_.Text(T(Text::IdleTestHint), {kInset, top + 31.0f, right - 120.0f, top + 50.0f},
                                         TextStyle::BodySmall, p.onSurfaceVariant);
                            TextButton(kIdIdleTest, {right - 108.0f, buttonTop, right, buttonTop + design::kControlHeight},
                                       0, T(Text::IdleTestButton), true, [this] { host_.OnIdleTest(); });
                        }});
    }
    rows.push_back(HintItem(T(Text::IdleHint)));
    const std::wstring gg = std::wstring(T(Text::IdleGgApps)) + idle_.ggApps;
    if (on && !idle_.ggApps.empty()) {
        rows.push_back(HintItem(gg.c_str()));
    }
    return DrawCard(y, rows) + design::kSectionGap;
}

float Panel::DrawRoutingSettings(float y) {
    const Palette& p = P();
    y = SectionTitle(T(Text::Routing), y);
    const bool on = routing_.enabled;
    std::vector<CardRow> rows{
        SwitchItem(kIdRoutingToggle, T(Text::RoutingMode), on, [this] { host_.OnRoutingEnabled(!routing_.enabled); },
                   T(Text::RoutingModeHint)),
    };
    if (on) {
        if (routing_.cablesMissing) {
            rows.push_back(HintItem(T(Text::RoutingNoCables)));
        }
        // A row per channel with its device and where it plays; Media only while its group is on.
        constexpr Text kChannels[] = {Text::Game, Text::Chat, Text::Media};
        constexpr RoutingSlot kDevices[] = {RoutingSlot::Game, RoutingSlot::Chat, RoutingSlot::Media};
        constexpr RoutingSlot kOutputs[] = {RoutingSlot::GameOutput, RoutingSlot::ChatOutput, RoutingSlot::MediaOutput};
        const D2D1_COLOR_F colors[] = {p.primary, p.tertiary, p.onSurfaceVariant};
        const wchar_t glyphs[] = {kGlyphGame, kGlyphChat, kGlyphVolume};
        for (int channel = 0; channel < (settings_.mediaGroup ? 3 : 2); ++channel) {
            // The device, and where it plays when that is not the shared output.
            const std::wstring& device = routing_.names[static_cast<int>(kDevices[channel])];
            const int output = static_cast<int>(kOutputs[channel]);
            std::wstring subtitle =
                device.empty() ? std::wstring(T(channel == 2 ? Text::RoutingMediaNone : Text::NotSelected)) : device;
            if (!device.empty() && !routing_.ids[output].empty()) {
                subtitle += L" → " + routing_.names[output];
            }
            rows.push_back(NavItem(kIdRoutingRow + 10 + channel, glyphs[channel], colors[channel], T(kChannels[channel]),
                                   subtitle, [this, channel] {
                                       routingChannel_ = channel;
                                       GoTo(Page::RoutingChannel);
                                   }));
        }
        rows.push_back(ValueItem(kIdRoutingRow + 4 + static_cast<int>(RoutingSlot::Output), T(Text::RoutingOutput),
                                 routing_.names[static_cast<int>(RoutingSlot::Output)].empty()
                                     ? T(Text::NotSelected)
                                     : routing_.names[static_cast<int>(RoutingSlot::Output)],
                                 [this] { OpenPicker(RoutingSlot::Output); }));
        if (!routing_.status.empty()) {
            const float height = HintHeight(routing_.status.c_str()) + design::kHintBottomGap;
            rows.push_back({height, [this, &p, height](float top) {
                                canvas_.Text(routing_.status, {kInset, top + design::kHintBottomGap, kWidth - kInset,
                                                               top + height - design::kHintBottomGap},
                                             TextStyle::BodySmall, routing_.error ? p.error : p.onSurfaceVariant,
                                             TextAlign::Leading, true);
                            }});
        }
    }
    if (!on) {
        // Without virtual devices each group can still play on a device of its own.
        constexpr Text kGroupNames[] = {Text::Game, Text::Chat, Text::Media};
        constexpr RoutingSlot kOutputs[] = {RoutingSlot::GameOutput, RoutingSlot::ChatOutput, RoutingSlot::MediaOutput};
        for (int group = 0; group < (settings_.mediaGroup ? 3 : 2); ++group) {
            const RoutingSlot output = kOutputs[group];
            rows.push_back(ValueItem(kIdRoutingRow + 16 + group, T(kGroupNames[group]),
                                     routing_.names[static_cast<int>(output)], [this, output] { OpenPicker(output); }));
        }
        rows.push_back(HintItem(T(Text::GroupOutputsHint)));
    }
    y = DrawCard(y, rows) + design::kGroupGap;
    if (on) {
        // How the mode treats Windows: the devices it switches and the apps it sends to them.
        y = DrawCard(y, {
                            SwitchItem(kIdRoutingManage, T(Text::RoutingManage), routing_.manageDevices,
                                       [this] { host_.OnRoutingManage(!routing_.manageDevices); },
                                       T(Text::RoutingManageHint)),
                            SwitchItem(kIdAutoRoute, T(Text::AutoRoute), settings_.routing.autoRoute,
                                       [this] { Edit([](Settings& s) { s.routing.autoRoute = !s.routing.autoRoute; }); },
                                       T(Text::AutoRouteHint)),
                        }) +
            design::kGroupGap;
    }
    return y;
}

void Panel::DrawRoutingChannel() {
    const Palette& p = P();
    constexpr Text kTitles[] = {Text::Game, Text::Chat, Text::Media};
    constexpr Text kHints[] = {Text::RoutingGameHint, Text::RoutingChatHint, Text::RoutingMediaHint};
    constexpr RoutingSlot kDevices[] = {RoutingSlot::Game, RoutingSlot::Chat, RoutingSlot::Media};
    constexpr RoutingSlot kOutputs[] = {RoutingSlot::GameOutput, RoutingSlot::ChatOutput, RoutingSlot::MediaOutput};
    const int channel = std::clamp(routingChannel_, 0, 2);
    DrawHeader(T(kTitles[channel]));
    float y = BeginScroll(Page::RoutingChannel, kHeaderHeight);
    y = PageHint(T(kHints[channel]), y);
    const RoutingSlot device = kDevices[channel];
    const RoutingSlot output = kOutputs[channel];
    const std::wstring& deviceName = routing_.names[static_cast<int>(device)];
    std::vector<CardRow> rows{
        ValueItem(kIdRoutingRow + 14, T(Text::RoutingDevice),
                  deviceName.empty() ? std::wstring(T(channel == 2 ? Text::RoutingMediaNone : Text::NotSelected))
                                     : deviceName,
                  [this, device] { OpenPicker(device); }),
    };
    // Where a channel plays matters only once it has a device.
    if (!routing_.ids[static_cast<int>(device)].empty()) {
        rows.push_back(ValueItem(kIdRoutingRow + 15, T(Text::RoutingPlaysOn), routing_.names[static_cast<int>(output)],
                                 [this, output] { OpenPicker(output); }));
    }
    y = DrawCard(y, rows);
    if (!routing_.status.empty() && routing_.error) {
        y = PageHint(routing_.status.c_str(), y + design::kSpace2, p.error);
    }
    EndScroll(Page::RoutingChannel, kHeaderHeight, y);
}

void Panel::DrawPickDevice() {
    const Palette& p = P();
    constexpr Text kTitles[] = {Text::RoutingGame,    Text::RoutingChat,    Text::RoutingOutput,
                                Text::MicInput,       Text::MicOutput,      Text::RoutingMediaDevice,
                                Text::RoutingPlaysOn, Text::RoutingPlaysOn, Text::RoutingPlaysOn};
    const int slot = static_cast<int>(pickSlot_);
    DrawHeader(T(kTitles[slot]));

    // An empty ID stands for the automatic choice of the slots that have one.
    std::vector<AudioEndpoint> items;
    switch (pickSlot_) {
    case RoutingSlot::Output: items.push_back({L"", T(Text::AutomaticHeadset), std::nullopt}); break;
    case RoutingSlot::MicInput: items.push_back({L"", T(Text::MicDefaultInput), std::nullopt}); break;
    case RoutingSlot::MicOutput: items.push_back({L"", T(Text::MicAutomaticCable), std::nullopt}); break;
    case RoutingSlot::Media: items.push_back({L"", T(Text::RoutingMediaNone), std::nullopt}); break;
    case RoutingSlot::GameOutput:
    case RoutingSlot::ChatOutput:
    case RoutingSlot::MediaOutput:
        items.push_back({L"", T(routing_.enabled ? Text::SameAsOutput : Text::WindowsDefaultOutput), std::nullopt});
        break;
    case RoutingSlot::Game:
    case RoutingSlot::Chat: break;
    }
    items.insert(items.end(), endpoints_.begin(), endpoints_.end());
    const std::wstring& current = pickSlot_ == RoutingSlot::MicInput    ? settings_.mic.input
                                  : pickSlot_ == RoutingSlot::MicOutput ? settings_.mic.output
                                                                        : routing_.ids[slot];

    // Pickers list connected devices only; the hint says where the others went. Virtual cables are what
    // the Game, Chat and cable slots are for, so only the output and microphone pickers leave them out.
    const bool cables = pickSlot_ == RoutingSlot::Game || pickSlot_ == RoutingSlot::Chat ||
                        pickSlot_ == RoutingSlot::Media || pickSlot_ == RoutingSlot::MicOutput;
    if (!cables) {
        std::erase_if(items, [](const AudioEndpoint& item) { return !item.id.empty() && IsVirtualEndpoint(item); });
    }
    // The channels play from virtual cables only: a headset or speakers there would loop the sound
    // into itself and be switched off with the mode.
    if (pickSlot_ == RoutingSlot::Game || pickSlot_ == RoutingSlot::Chat || pickSlot_ == RoutingSlot::Media) {
        std::erase_if(items, [](const AudioEndpoint& item) { return !item.id.empty() && !IsVirtualEndpoint(item); });
    }
    // Media cannot share the Game or Chat device.
    if (pickSlot_ == RoutingSlot::Media) {
        std::erase_if(items, [this](const AudioEndpoint& item) {
            return !item.id.empty() && (item.id == routing_.ids[static_cast<int>(RoutingSlot::Game)] ||
                                        item.id == routing_.ids[static_cast<int>(RoutingSlot::Chat)]);
        });
    }
    const bool channel = pickSlot_ == RoutingSlot::Game || pickSlot_ == RoutingSlot::Chat || pickSlot_ == RoutingSlot::Media;
    const wchar_t* hintText = T(channel  ? Text::ChannelDevicesHint
                                : cables ? Text::DisabledDevicesHint
                                         : Text::HiddenDevicesHint);
    const float listTop = PageHint(hintText, kHeaderHeight);
    const D2D1_RECT_F list{kGutter, listTop, kWidth - kGutter, ContentBottom() - design::kGroupGap};
    if (items.empty()) {
        DrawCard(listTop, {HintItem(T(Text::NoPlaybackDevices))});
        return;
    }
    auto idOf = [&](size_t index) {
        return HashId(kKindEndpoint, items[index].id.empty() ? L"auto" : items[index].id);
    };
    DrawList(list, kIdPickList, scroll_[Page::PickDevice], items.size(), kRowHeight, idOf,
             [&](size_t index, float top) {
                 const AudioEndpoint& item = items[index];
                 const bool chosen = current == item.id;
                 const float middle = top + kRowHeight / 2.0f;
                 const wchar_t glyph = pickSlot_ == RoutingSlot::MicInput                             ? kGlyphMicrophone
                                       : item.steelSeriesProductId || (item.id.empty() && !MicSlot()) ? kGlyphHeadphones
                                                                                                      : kGlyphSpeakers;
                 canvas_.Glyph(glyph, {design::kLeadingIconCenter, middle}, design::kIconSize - 2.0f,
                               chosen ? p.primary : p.onSurfaceVariant);
                 canvas_.Text(
                     item.name,
                     {design::kLeadingTextInset, top, design::kTrailingIconCenter - design::kSpace4, top + kRowHeight},
                     TextStyle::BodyLarge, chosen ? p.primary : p.onSurface);
                 if (chosen) {
                     canvas_.Glyph(kGlyphCheck, {design::kTrailingIconCenter, middle}, 16.0f, p.primary);
                 }
                 AddHit({TileRect(top, kRowHeight), idOf(index), [this, id = item.id] {
                             if (pickSlot_ == RoutingSlot::MicInput) {
                                 Edit([id](Settings& s) { s.mic.input = id; });
                             } else if (pickSlot_ == RoutingSlot::MicOutput) {
                                 Edit([id](Settings& s) { s.mic.output = id; });
                             } else {
                                 host_.OnRoutingDevice(pickSlot_, id);
                             }
                             GoBack();
                         }});
             });
}

}  // namespace ui
