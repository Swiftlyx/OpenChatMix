// Headset settings, device picker, routing and snackbar parts of the panel.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "panel.h"
#include "panel_internal.h"
#include "widgets.h"

namespace ui {

using namespace detail;

namespace {

constexpr const wchar_t* kBandLabels[kEqBands] = {L"32", L"64", L"125", L"250", L"500",
                                                  L"1k", L"2k", L"4k",  L"8k",  L"16k"};
constexpr float kEqTrackHeight = 120.0f;
constexpr float kSliderRowHeight = 80.0f;
constexpr float kChoiceRowHeight = 84.0f;
constexpr size_t kMaxSegments = 4;  // Longer option lists use a stepper.
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
    case SettingKind::Choice: return spec.options.size() <= kMaxSegments ? kChoiceRowHeight : kRowHeight;
    case SettingKind::Toggle: break;
    }
    return kRowHeight;
}

constexpr IdleAnimationKind kIdleAnimations[] = {IdleAnimationKind::Off,    IdleAnimationKind::Stars,
                                                 IdleAnimationKind::Bounce, IdleAnimationKind::Clock,
                                                 IdleAnimationKind::Life,   IdleAnimationKind::Blank,
                                                 IdleAnimationKind::Image};
constexpr Text kIdleAnimationLabels[] = {Text::OptOff,   Text::IdleStars, Text::IdleBounce, Text::IdleClock,
                                         Text::IdleLife, Text::IdleBlank, Text::IdleImage};
constexpr int kIdleDelays[] = {15, 30, 60, 120, 300, 600};
constexpr float kIdleHintHeight = 62.0f;

std::wstring DelayLabel(int seconds) {
    return seconds < 60 ? std::to_wstring(seconds) + L" " + T(Text::SecondsShort)
                        : std::to_wstring(seconds / 60) + L" " + T(Text::MinutesShort);
}

// Opens a folder or URL. explorer.exe needs no COM apartment, unlike ShellExecute on this MTA thread.
void OpenWithExplorer(const std::wstring& target) {
    std::wstring command = L"explorer.exe \"" + target + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
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
        const wchar_t glyph = static_cast<wchar_t>((headset_.charging ? kGlyphBatteryCharging0 : kGlyphBattery0) +
                                                   std::clamp(headset_.battery / 10, 0, 10));
        canvas_.Glyph(glyph, {32.0f, y + 12.0f}, 16.0f, p.onSurfaceVariant);
        canvas_.Text(text, {48.0f, y, kWidth - 20.0f, y + 24.0f}, TextStyle::BodyMedium, p.onSurfaceVariant);
        y += 34.0f;
    }
    if (features_->equalizer.bandsCommand) {
        y = DrawEqualizer(y);
    }
    for (const auto& section : features_->sections) {
        canvas_.Text(T(section.title), {20.0f, y, kWidth - 20.0f, y + 20.0f}, TextStyle::TitleSmall, p.primary);
        y += 26.0f;
        float height = 2.0f * kListPadding;
        for (const auto& setting : section.settings) {
            height += RowHeight(setting);
        }
        const D2D1_RECT_F card{16.0f, y, kWidth - 16.0f, y + height};
        canvas_.FillRound(card, kCardRadius, Alpha(p.surfaceContainer, kCardAlpha));
        float top = card.top + kListPadding;
        for (const auto& setting : section.settings) {
            top = DrawSettingRow(setting, top);
        }
        y = card.bottom + 20.0f;
    }
    if (features_->oled) {
        y = DrawIdleScreen(y);
    }
    EndScroll(Page::Headset, kHeaderHeight, y - 20.0f);
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

void Panel::ApplyHeadsetGamePreset(const NamedEq& preset) {
    if (!features_) {
        return;
    }
    const EqualizerSpec& spec = features_->equalizer;
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
        SYSTEMTIME now{};
        GetLocalTime(&now);
        wchar_t stamp[32];
        swprintf_s(stamp, L"%04u-%02u-%02u %02u.%02u", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute);
        const std::wstring name = std::wstring(T(Text::CurveBackup)) + L" " + stamp;
        if (WritePresetFile(PresetDirectory() + L"\\" + name + L".json", name, current)) {
            userPresets_ = LoadUserPresets();
        }
    }
    host_.OnEqualizerCurve(curve);
}

float Panel::DrawEqualizer(float y) {
    const Palette& p = P();
    const EqualizerSpec& eq = features_->equalizer;
    const int custom = eq.CustomIndex();
    const bool devicePreset = eq.presetCommand != 0 && headset_.eqPreset >= 0 && headset_.eqPreset < custom;
    const EqCurve curve = ActiveCurve();

    canvas_.Text(T(Text::Equalizer), {20.0f, y, 240.0f, y + 20.0f}, TextStyle::TitleSmall, p.primary);
    TextButton(kIdEqReset, {kWidth - 16.0f - 104.0f, y - 6.0f, kWidth - 16.0f, y + 26.0f}, kGlyphReset,
               T(Text::EqReset), false, [this] { host_.OnEqualizerCurve(EqCurve{}); });
    y += 30.0f;

    // Saved presets flow as chips; measure them first so the card can be sized.
    struct Chip {
        D2D1_RECT_F rect;
        const UserPreset* preset;
    };
    std::vector<Chip> chips;
    const float chipsLeft = 32.0f;
    const float chipsRight = kWidth - 32.0f;
    float chipX = chipsLeft;
    float chipRow = 0.0f;
    for (const auto& preset : userPresets_) {
        const float width = std::min(chipsRight - chipsLeft, canvas_.Measure(preset.name, TextStyle::LabelLarge) + 28.0f);
        if (chipX + width > chipsRight && chipX > chipsLeft) {
            chipX = chipsLeft;
            chipRow += 40.0f;
        }
        chips.push_back({{chipX, chipRow, chipX + width, chipRow + 32.0f}, &preset});
        chipX += width + 8.0f;
    }
    const float hintHeight = devicePreset ? HintHeight(T(Text::EqPresetActive), kWidth - 64.0f) + 4.0f : 0.0f;
    const float bandsHeight = 20.0f + kEqTrackHeight + 30.0f;
    const float gameHint = HintHeight(T(Text::GamePresetsHint), kWidth - 64.0f);
    const float gameHeight = 44.0f + gameHint;
    const float presetsHeight = chips.empty() ? 0.0f : 24.0f + chipRow + 32.0f + 8.0f;
    const D2D1_RECT_F card{16.0f, y, kWidth - 16.0f,
                           y + 16.0f + 44.0f + hintHeight + bandsHeight + 44.0f + gameHeight + presetsHeight + 12.0f};
    canvas_.FillRound(card, kCardRadius, Alpha(p.surfaceContainer, kCardAlpha));

    std::vector<const wchar_t*> labels;
    for (const auto& preset : eq.presets) {
        labels.push_back(preset.name);
    }
    labels.push_back(T(Text::EqCustom));
    DrawSegmented({32.0f, y + 16.0f, kWidth - 32.0f, y + 48.0f}, kIdEqPreset, labels, headset_.eqPreset,
                  p.primaryContainer, p.onPrimaryContainer, [this](int index) { host_.OnEqualizerPreset(index); });
    float inner = y + 60.0f;
    if (devicePreset) {
        canvas_.Text(T(Text::EqPresetActive), {32.0f, inner, kWidth - 32.0f, inner + hintHeight}, TextStyle::BodySmall,
                     p.onSurfaceVariant, TextAlign::Leading, true);
        inner += hintHeight;
    }

    const float valueTop = inner;
    const float trackTop = inner + 20.0f;
    const float trackBottom = trackTop + kEqTrackHeight;
    const float zero = (trackTop + trackBottom) / 2.0f;
    const float half = kEqTrackHeight / 2.0f;
    const float left = 32.0f;
    const float column = (kWidth - 64.0f) / kEqBands;
    // A preset stored on the headset is shown as it plays, but only the custom curve can be edited.
    const float alpha = devicePreset ? 0.8f : 1.0f;
    for (int band = 0; band < kEqBands; ++band) {
        const float cx = left + column * (band + 0.5f);
        const uint32_t id = kIdEqBand + band;
        const float value = Animate(AnimKey(id, 0), static_cast<float>(curve[band]), 22.0f);
        const float handleY = zero - value / eq.range * half;
        canvas_.FillRound({cx - 4.0f, trackTop, cx + 4.0f, trackBottom}, 4.0f, Alpha(p.secondaryContainer, alpha));
        if (std::fabs(value) > 0.05f) {
            canvas_.FillRound({cx - 4.0f, std::min(zero, handleY), cx + 4.0f, std::max(zero, handleY)}, 4.0f,
                              Alpha(p.primary, alpha));
        }
        canvas_.FillRect({cx - 7.0f, zero - 0.5f, cx + 7.0f, zero + 0.5f}, Alpha(p.outline, 0.6f * alpha));
        const float handleWidth = Animate(AnimKey(id, 1), pressedId_ == id ? 28.0f : 22.0f, 24.0f);
        canvas_.FillRound({cx - handleWidth / 2.0f, handleY - 3.0f, cx + handleWidth / 2.0f, handleY + 3.0f}, 3.0f,
                          Alpha(curve[band] == 0 ? p.onSurface : p.primary, alpha));
        canvas_.Text(FormatDb(curve[band]), {cx - column / 2.0f, valueTop, cx + column / 2.0f, valueTop + 16.0f},
                     TextStyle::LabelMedium, Alpha(p.onSurfaceVariant, alpha), TextAlign::Center);
        canvas_.Text(kBandLabels[band], {cx - column / 2.0f, trackBottom + 8.0f, cx + column / 2.0f, trackBottom + 24.0f},
                     TextStyle::BodySmall, p.onSurfaceVariant, TextAlign::Center);

        if (devicePreset) {
            continue;
        }
        Hit hit;
        hit.rect = {cx - column / 2.0f, trackTop - 10.0f, cx + column / 2.0f, trackBottom + 10.0f};
        hit.id = id;
        hit.drag = [this, band, zero, half, curve, range = eq.range](float, float pointerY) {
            const int target = std::clamp(static_cast<int>(std::lround((zero - pointerY) / half * range)),
                                          -static_cast<int>(range), static_cast<int>(range));
            if (target != curve[band]) {
                EqCurve next = curve;
                next[band] = static_cast<int8_t>(target);
                host_.OnEqualizerCurve(next);
            }
        };
        hit.doubleClick = [this, band, curve] {
            EqCurve next = curve;
            next[band] = 0;
            host_.OnEqualizerCurve(next);
        };
        AddHit(std::move(hit));
    }

    // Preset file actions.
    float actions = trackBottom + 30.0f;
    const float actionWidth = (kWidth - 64.0f) / 3.0f;
    TextButton(kIdEqSave, {32.0f, actions, 32.0f + actionWidth, actions + 32.0f}, kGlyphSave, T(Text::EqSave), false,
               [this] { RunFileDialog(FileDialog::SavePreset); });
    TextButton(kIdEqImport, {32.0f + actionWidth, actions, 32.0f + 2.0f * actionWidth, actions + 32.0f}, kGlyphImport,
               T(Text::EqImport), false, [this] { RunFileDialog(FileDialog::ImportPreset); });
    TextButton(kIdEqFolder, {32.0f + 2.0f * actionWidth, actions, kWidth - 32.0f, actions + 32.0f}, kGlyphFolder,
               T(Text::EqFolder), false, [] { OpenWithExplorer(PresetDirectory()); });

    // Game presets of every source, converted to the headset's bands.
    const float gameTop = actions + 44.0f;
    const D2D1_RECT_F gameButton{32.0f, gameTop, kWidth - 32.0f, gameTop + 36.0f};
    TextButton(kIdGamePresets, gameButton, kGlyphGame, T(Text::GamePresets), true, [this, gameButton] {
        if (eqLibraryChannel_ != EqChannel::Game) {
            LoadEqLibrary(EqChannel::Game);
        }
        std::vector<const NamedEq*> presets;
        std::vector<MenuItem> items = EqLibraryMenu(presets);
        std::vector<NamedEq> chosen;
        for (const NamedEq* preset : presets) {
            chosen.push_back(preset ? *preset : NamedEq{});
        }
        OpenMenu(gameButton, std::move(items), -1,
                 [this, chosen = std::move(chosen)](int index) { ApplyHeadsetGamePreset(chosen[index]); });
    });
    canvas_.Text(T(Text::GamePresetsHint), {32.0f, gameTop + 40.0f, kWidth - 32.0f, gameTop + 40.0f + gameHint},
                 TextStyle::BodySmall, p.onSurfaceVariant, TextAlign::Leading, true);

    if (!chips.empty()) {
        const float labelTop = actions + 44.0f + gameHeight;
        canvas_.Text(T(Text::MyPresets), {32.0f, labelTop, kWidth - 32.0f, labelTop + 18.0f}, TextStyle::BodySmall,
                     p.onSurfaceVariant);
        const float chipsTop = labelTop + 24.0f;
        for (const auto& chip : chips) {
            const D2D1_RECT_F rect{chip.rect.left, chipsTop + chip.rect.top, chip.rect.right, chipsTop + chip.rect.bottom};
            const EqCurve presetCurve = PresetCurve(*chip.preset, eq.range);
            const bool active = presetCurve == curve && (eq.presetCommand == 0 || headset_.eqPreset == custom);
            const uint32_t id = HashId(kKindPreset, chip.preset->path);
            if (active) {
                canvas_.FillRound(rect, 8.0f, p.secondaryContainer);
            } else {
                canvas_.StrokeRound(Inset(rect, 0.5f, 0.5f), 8.0f, p.outline, 1.0f);
            }
            StateLayer(id, rect, 8.0f, p.onSurface);
            canvas_.Text(chip.preset->name, Inset(rect, 14.0f, 0.0f), TextStyle::LabelLarge,
                         active ? p.onSecondaryContainer : p.onSurface, TextAlign::Center);
            AddHit({rect, id, [this, presetCurve] { host_.OnEqualizerCurve(presetCurve); }});
        }
    }
    return card.bottom + 20.0f;
}

float Panel::DrawSettingRow(const SettingSpec& spec, float top) {
    const Palette& p = P();
    const float height = RowHeight(spec);
    const uint32_t id = HashId(kKindSetting, std::to_wstring(spec.command));
    const int value = headset_.values[spec.command];
    const SettingSpec* setting = &spec;

    switch (spec.kind) {
    case SettingKind::Toggle:
        DrawSwitchRow(id, top, T(spec.label), value == spec.max, [this, setting, value] {
            host_.OnHeadsetSetting(setting->command, value == setting->max ? setting->min : setting->max);
        });
        break;

    case SettingKind::Slider: {
        // No room for a second line above the slider, so long labels use the smaller style.
        const D2D1_RECT_F labelRect{36.0f, top + 8.0f, kWidth - 110.0f, top + 32.0f};
        const bool fits = canvas_.Measure(T(spec.label), TextStyle::BodyLarge) <= labelRect.right - labelRect.left;
        canvas_.Text(T(spec.label), labelRect, fits ? TextStyle::BodyLarge : TextStyle::BodyMedium, p.onSurface);
        canvas_.Text(value >= 0 ? std::to_wstring(value) : L"—", {kWidth - 110.0f, top + 8.0f, kWidth - 36.0f, top + 32.0f},
                     TextStyle::LabelLarge, p.onSurfaceVariant, TextAlign::Trailing);
        const float left = 40.0f;
        const float right = kWidth - 40.0f;
        const float centerY = top + 54.0f;
        const float t = value >= 0 ? static_cast<float>(value - spec.min) / (spec.max - spec.min) : 0.0f;
        const float shown = Animate(AnimKey(id, 0), t, 20.0f);
        const float handleWidth = Animate(AnimKey(id, 1), pressedId_ == id ? 2.0f : 4.0f, 24.0f);
        DrawValueSlider(canvas_, p, left, right, centerY, shown, handleWidth, kRowSlider, value >= 0);
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
            canvas_.Text(T(spec.label), {36.0f, top + 8.0f, kWidth - 36.0f, top + 32.0f}, TextStyle::BodyLarge,
                         p.onSurface);
            std::vector<std::wstring> texts;
            for (const auto& option : spec.options) {
                texts.push_back(OptionLabel(option));
            }
            std::vector<const wchar_t*> labels;
            for (const auto& text : texts) {
                labels.push_back(text.c_str());
            }
            DrawSegmented({36.0f, top + 42.0f, kWidth - 36.0f, top + 74.0f}, id, labels, selected,
                          p.secondaryContainer, p.onSecondaryContainer, [this, setting](int index) {
                              host_.OnHeadsetSetting(setting->command, setting->options[index].value);
                          });
        } else {
            const int count = static_cast<int>(spec.options.size());
            const int previous = selected < 0 ? 0 : std::max(0, selected - 1);
            const int next = selected < 0 ? 0 : std::min(count - 1, selected + 1);
            DrawStepperRow(
                id, top, T(spec.label), selected >= 0 ? OptionLabel(spec.options[selected]) : L"—", 72.0f,
                selected != 0, selected < count - 1,
                [this, setting, previous] { host_.OnHeadsetSetting(setting->command, setting->options[previous].value); },
                [this, setting, next] { host_.OnHeadsetSetting(setting->command, setting->options[next].value); });
        }
        break;
    }
    }
    return top + height;
}

float Panel::DrawIdleScreen(float y) {
    const Palette& p = P();
    canvas_.Text(T(Text::IdleScreen), {20.0f, y, kWidth - 20.0f, y + 20.0f}, TextStyle::TitleSmall, p.primary);
    if (idle_.showing) {
        canvas_.Text(T(Text::IdleShowing), {20.0f, y, kWidth - 24.0f, y + 20.0f}, TextStyle::BodySmall, p.primary,
                     TextAlign::Trailing);
    }
    y += 26.0f;
    const bool on = idle_.animation != IdleAnimationKind::Off;
    const bool image = idle_.animation == IdleAnimationKind::Image;
    const int rows = 1 + (on ? 1 : 0) + (image ? 1 : 0);
    const D2D1_RECT_F card{16.0f, y, kWidth - 16.0f, y + 2.0f * kListPadding + rows * kRowHeight + kIdleHintHeight};
    canvas_.FillRound(card, kCardRadius, Alpha(p.surfaceContainer, kCardAlpha));
    float top = card.top + kListPadding;

    const int count = static_cast<int>(std::size(kIdleAnimations));
    const int index = static_cast<int>(std::find(std::begin(kIdleAnimations), std::end(kIdleAnimations),
                                                 idle_.animation) - std::begin(kIdleAnimations));
    DrawStepperRow(HashId(kKindIdle, L"animation"), top, T(Text::IdleAnimation), T(kIdleAnimationLabels[index]),
                   150.0f, index > 0, index < count - 1,
                   [this, index] { host_.OnIdleAnimation(kIdleAnimations[index - 1]); },
                   [this, index] { host_.OnIdleAnimation(kIdleAnimations[index + 1]); });
    top += kRowHeight;

    if (on) {
        const int delays = static_cast<int>(std::size(kIdleDelays));
        // The nearest listed delay, so values edited in the ini file still step sensibly.
        int delay = 0;
        for (int i = 1; i < delays; ++i) {
            if (std::abs(kIdleDelays[i] - idle_.delaySeconds) < std::abs(kIdleDelays[delay] - idle_.delaySeconds)) {
                delay = i;
            }
        }
        DrawStepperRow(HashId(kKindIdle, L"delay"), top, T(Text::IdleDelay), DelayLabel(idle_.delaySeconds), 72.0f,
                       delay > 0, delay < delays - 1, [this, delay] { host_.OnIdleDelay(kIdleDelays[delay - 1]); },
                       [this, delay] { host_.OnIdleDelay(kIdleDelays[delay + 1]); });
        top += kRowHeight;
    }

    if (image) {
        const float right = kWidth - 36.0f;
        canvas_.Text(T(Text::IdleFile), {36.0f, top + 8.0f, right - 120.0f, top + 29.0f}, TextStyle::BodyLarge,
                     p.onSurface);
        canvas_.Text(idle_.imageName.empty() ? T(Text::NotSelected) : idle_.imageName.c_str(),
                     {36.0f, top + 29.0f, right - 120.0f, top + 47.0f}, TextStyle::BodySmall, p.onSurfaceVariant);
        TextButton(kIdIdleChoose, {right - 108.0f, top + 12.0f, right, top + 44.0f}, kGlyphFolder,
                   T(Text::IdleChoose), true, [this] { RunFileDialog(FileDialog::IdleImage); });
        top += kRowHeight;
    }
    canvas_.Text(T(Text::IdleHint), {36.0f, top + 2.0f, kWidth - 36.0f, top + kIdleHintHeight - 6.0f},
                 TextStyle::BodySmall, p.onSurfaceVariant, TextAlign::Leading, true);
    return card.bottom + 20.0f;
}

float Panel::DrawRoutingSettings(float y) {
    const Palette& p = P();
    canvas_.Text(T(Text::Routing), {20.0f, y, kWidth - 20.0f, y + 20.0f}, TextStyle::TitleSmall, p.primary);
    y += 26.0f;
    const bool on = routing_.enabled;
    const int rows = on ? 3 : 0;
    const float statusHeight = on ? 28.0f : 0.0f;
    const float modeHint = HintHeight(T(Text::RoutingModeHint));
    const float manageHint = on ? HintHeight(T(Text::RoutingManageHint)) : 0.0f;
    const float routeHint = on ? HintHeight(T(Text::AutoRouteHint)) : 0.0f;
    const float manageHeight = on ? 2.0f * kRowHeight + manageHint + routeHint : 0.0f;
    const float cablesHeight = on && routing_.cablesMissing ? HintHeight(T(Text::RoutingNoCables)) : 0.0f;
    const D2D1_RECT_F card{16.0f, y, kWidth - 16.0f,
                           y + 2.0f * kListPadding + kRowHeight + modeHint + manageHeight + cablesHeight +
                               rows * kRowHeight + statusHeight};
    canvas_.FillRound(card, kCardRadius, Alpha(p.surfaceContainer, kCardAlpha));

    float top = card.top + kListPadding;
    DrawSwitchRow(kIdRoutingToggle, top, T(Text::RoutingMode), on, [this] { host_.OnRoutingEnabled(!routing_.enabled); });
    DrawHint(T(Text::RoutingModeHint), top + kRowHeight - 8.0f, modeHint);
    top += kRowHeight + modeHint;

    if (on) {
        DrawSwitchRow(kIdRoutingManage, top, T(Text::RoutingManage), routing_.manageDevices,
                      [this] { host_.OnRoutingManage(!routing_.manageDevices); });
        DrawHint(T(Text::RoutingManageHint), top + kRowHeight - 8.0f, manageHint);
        top += kRowHeight + manageHint;
        DrawSwitchRow(kIdAutoRoute, top, T(Text::AutoRoute), settings_.routing.autoRoute,
                      [this] { Edit([](Settings& s) { s.routing.autoRoute = !s.routing.autoRoute; }); });
        DrawHint(T(Text::AutoRouteHint), top + kRowHeight - 8.0f, routeHint);
        top += kRowHeight + routeHint;
    }
    if (cablesHeight > 0.0f) {
        DrawHint(T(Text::RoutingNoCables), top, cablesHeight);
        top += cablesHeight;
    }

    constexpr RoutingSlot kSlots[] = {RoutingSlot::Game, RoutingSlot::Chat, RoutingSlot::Output};
    constexpr Text kLabels[] = {Text::RoutingGame, Text::RoutingChat, Text::RoutingOutput};
    for (int i = 0; i < rows; ++i) {
        const uint32_t id = kIdRoutingRow + 4 + i;
        const D2D1_RECT_F row{20.0f, top, kWidth - 20.0f, top + kRowHeight};
        StateLayer(id, row, 16.0f, p.onSurface);
        canvas_.Text(T(kLabels[i]), {36.0f, top, 170.0f, top + kRowHeight}, TextStyle::BodyLarge, p.onSurface);
        canvas_.Text(routing_.names[i].empty() ? T(Text::NotSelected) : routing_.names[i].c_str(),
                     {170.0f, top, kWidth - 56.0f, top + kRowHeight}, TextStyle::BodySmall, p.onSurfaceVariant,
                     TextAlign::Trailing);
        canvas_.Glyph(kGlyphChevronRight, {kWidth - 40.0f, top + 28.0f}, 14.0f, p.onSurfaceVariant);
        AddHit({row, id, [this, slot = kSlots[i]] { OpenPicker(slot); }});
        top += kRowHeight;
    }
    if (routing_.enabled) {
        canvas_.Text(routing_.status, {36.0f, top, kWidth - 36.0f, top + statusHeight}, TextStyle::BodySmall,
                     routing_.error ? p.error : p.onSurfaceVariant);
    }
    return card.bottom + 12.0f;
}

void Panel::DrawPickDevice() {
    const Palette& p = P();
    constexpr Text kTitles[] = {Text::RoutingGame, Text::RoutingChat, Text::RoutingOutput};
    const int slot = static_cast<int>(pickSlot_);
    DrawHeader(T(kTitles[slot]));

    std::vector<AudioEndpoint> items;
    if (pickSlot_ == RoutingSlot::Output) {
        items.push_back({L"", T(Text::AutomaticHeadset), std::nullopt});
    }
    items.insert(items.end(), endpoints_.begin(), endpoints_.end());

    const D2D1_RECT_F list{16.0f, 64.0f, kWidth - 16.0f, ContentBottom() - 12.0f};
    if (items.empty()) {
        canvas_.FillRound(list, kCardRadius, Alpha(p.surfaceContainer, kCardAlpha));
        canvas_.Text(T(Text::NoPlaybackDevices), Inset(list, 24.0f, 0.0f), TextStyle::BodyMedium, p.onSurfaceVariant,
                     TextAlign::Center, true);
        return;
    }
    DrawList(list, kIdPickList, scroll_[Page::PickDevice], items.size(), [&](size_t index, float top) {
        const AudioEndpoint& item = items[index];
        const uint32_t id = HashId(kKindEndpoint, item.id.empty() ? L"auto" : item.id);
        const D2D1_RECT_F row{20.0f, top, kWidth - 20.0f, top + kRowHeight};
        const bool chosen = routing_.ids[slot] == item.id;
        StateLayer(id, row, 16.0f, p.onSurface);
        canvas_.Glyph(item.steelSeriesProductId || item.id.empty() ? kGlyphHeadphones : kGlyphSpeakers,
                      {44.0f, top + 28.0f}, 18.0f, chosen ? p.primary : p.onSurfaceVariant);
        canvas_.Text(item.name, {68.0f, top, kWidth - 64.0f, top + kRowHeight}, TextStyle::BodyLarge, p.onSurface);
        if (chosen) {
            canvas_.Glyph(kGlyphCheck, {kWidth - 44.0f, top + 28.0f}, 16.0f, p.primary);
        }
        AddHit({row, id, [this, id = item.id] {
                    host_.OnRoutingDevice(pickSlot_, id);
                    GoBack();
                }});
    });
}

}  // namespace ui
