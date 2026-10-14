// Settings and their sub-pages.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "../app.h"
#include "../version.h"
#include "panel.h"
#include "panel_internal.h"
#include "widgets.h"

namespace ui {

using namespace detail;

namespace {

constexpr uint32_t kAccentPresets[] = {kDefaultAccent, 0xE5484D, 0xE0559A, 0x8E5CF7,
                                       0x3B82F6,       0x14B8A6, 0x22C55E, 0xEAB308};
constexpr int kDuckingSteps[] = {10, 20, 30, 40, 50, 60, 70, 80, 90};
constexpr int kBatteryThresholds[] = {5, 10, 15, 20, 25, 30, 40, 50};
constexpr float kSwatchSize = 40.0f;
constexpr SliderStyle kPickerSlider{12.0f, 5.0f, 32.0f};

template <size_t Count>
int NearestIndex(const int (&values)[Count], int value) {
    int best = 0;
    for (int i = 1; i < static_cast<int>(Count); ++i) {
        if (std::abs(values[i] - value) < std::abs(values[best] - value)) {
            best = i;
        }
    }
    return best;
}

std::wstring BalanceText(int balance) {
    const Levels levels = LevelsFromBalance(balance);
    return std::wstring(T(Text::Game)) + L" " + Percent(levels.game) + L" · " + T(Text::Chat) + L" " +
           Percent(levels.chat);
}

}  // namespace

GameProfile* Panel::EditedProfile() {
    const auto found = std::find_if(settings_.profiles.begin(), settings_.profiles.end(), [&](const GameProfile& profile) {
        return _wcsicmp(profile.exe.c_str(), profileExe_.c_str()) == 0;
    });
    return found == settings_.profiles.end() ? nullptr : &*found;
}

void Panel::DrawSettings() {
    const Palette& p = P();
    DrawTitle(T(Text::Settings));
    float y = BeginScroll(Page::Settings, kHeaderHeight);

    const D2D1_RECT_F sections = Card(y, 2.0f * kListPadding + 5.0f * 64.0f);
    float top = sections.top + kListPadding;
    top = DrawNavRow(HashId(kKindNav, L"appearance"), top, kGlyphPalette, p.primary, T(Text::Appearance),
                     T(Text::AppearanceSummary), [this] { GoTo(Page::Appearance); });
    top = DrawNavRow(HashId(kKindNav, L"audio"), top, kGlyphVolume, p.primary, T(Text::AudioSection),
                     T(Text::AudioSummary), [this] { GoTo(Page::Audio); });
    top = DrawNavRow(HashId(kKindNav, L"hotkeys"), top, kGlyphKeyboard, p.primary, T(Text::Hotkeys), HotkeyHint(),
                     [this] { GoTo(Page::Hotkeys); });
    top = DrawNavRow(HashId(kKindNav, L"notifications"), top, kGlyphBell, p.primary, T(Text::Notifications),
                     T(Text::NotificationsSummary), [this] { GoTo(Page::Notifications); });
    std::wstring profiles;
    for (const auto& profile : settings_.profiles) {
        profiles += (profiles.empty() ? L"" : L", ") + profile.exe;
    }
    DrawNavRow(HashId(kKindNav, L"profiles"), top, kGlyphGame, p.primary, T(Text::Profiles),
               profiles.empty() ? T(Text::ProfilesSummary) : profiles, [this] { GoTo(Page::Profiles); });
    y = sections.bottom + 12.0f;

    y = SectionTitle(T(Text::General), y);
    const D2D1_RECT_F general = Card(y, 2.0f * kListPadding + 2.0f * kRowHeight);
    top = general.top + kListPadding;
    canvas_.Text(T(Text::LanguageLabel), {36.0f, top, 160.0f, top + kRowHeight}, TextStyle::BodyLarge, p.onSurface);
    const int language = CurrentLanguage() == Language::Ukrainian ? 1 : 0;
    DrawSegmented({kWidth - 36.0f - 196.0f, top + 12.0f, kWidth - 36.0f, top + 44.0f}, kIdLanguage, kLanguageLabels,
                  language, p.secondaryContainer, p.onSecondaryContainer, [this](int index) {
                      if (kLanguages[index] != CurrentLanguage()) {
                          host_.OnLanguageChanged(kLanguages[index]);
                      }
                  });
    top += kRowHeight;
    DrawSwitchRow(kIdAutostart, top, T(Text::StartWithWindows), autostart_, [this] {
        autostart_ = !autostart_;
        host_.OnAutostartChanged(autostart_);
        Invalidate();
    });
    y = general.bottom + 12.0f;

    // Updates: the automatic check and the current state with its action.
    y = SectionTitle(T(Text::Updates), y);
    const float updateHint = HintHeight(T(Text::CheckUpdatesHint));
    const D2D1_RECT_F updates = Card(y, 2.0f * kListPadding + 2.0f * kRowHeight + updateHint);
    top = updates.top + kListPadding;
    DrawSwitchRow(kIdCheckUpdates, top, T(Text::CheckUpdates), settings_.checkUpdates,
                  [this] { Edit([](Settings& s) { s.checkUpdates = !s.checkUpdates; }); });
    DrawHint(T(Text::CheckUpdatesHint), top + kRowHeight - 8.0f, updateHint);
    top += kRowHeight + updateHint;
    {
        using State = UpdateView::State;
        const std::wstring current = std::wstring(T(Text::VersionLabel)) + L" " + APP_VERSION_WSTRING;
        std::wstring status = current;
        const wchar_t* action = T(Text::UpdateCheckNow);
        std::function<void()> click = [this] { host_.OnCheckForUpdates(); };
        D2D1_COLOR_F statusColor = p.onSurface;
        switch (update_.state) {
        case State::Idle:
            break;
        case State::Checking:
            status = T(Text::UpdateChecking);
            action = nullptr;
            break;
        case State::UpToDate:
            status = std::wstring(T(Text::UpdateCurrent)) + L" · " + APP_VERSION_WSTRING;
            break;
        case State::Available:
            status = std::wstring(T(Text::UpdateAvailable)) + L" · " + update_.version;
            statusColor = p.primary;
            action = T(Text::UpdateInstall);
            click = [this] { host_.OnInstallUpdate(); };
            break;
        case State::Installing:
            status = T(Text::UpdateDownloading);
            action = nullptr;
            break;
        case State::Failed:
            status = T(Text::UpdateFailed);
            statusColor = p.error;
            action = T(Text::UpdateOpenPage);
            click = [this] { host_.OnOpenReleasePage(); };
            break;
        case State::CheckFailed:
            status = T(Text::UpdateCheckFailed);
            statusColor = p.error;
            break;
        }
        const float buttonWidth = action ? canvas_.Measure(action, TextStyle::LabelLarge) + 40.0f : 0.0f;
        canvas_.Text(status, {36.0f, top, kWidth - 44.0f - buttonWidth, top + kRowHeight}, TextStyle::BodyLarge,
                     statusColor);
        if (action) {
            TextButton(kIdUpdateAction, {kWidth - 36.0f - buttonWidth, top + 12.0f, kWidth - 36.0f, top + 44.0f}, 0,
                       action, true, std::move(click));
        }
    }
    y = updates.bottom + 12.0f;

    const D2D1_RECT_F quitCard = Card(y, 2.0f * kListPadding + kRowHeight);
    top = quitCard.top + kListPadding;
    const D2D1_RECT_F quitRow{20.0f, top, kWidth - 20.0f, top + kRowHeight};
    StateLayer(kIdQuit, quitRow, 16.0f, p.error);
    canvas_.Glyph(kGlyphPower, {46.0f, top + 28.0f}, 16.0f, p.error);
    canvas_.Text(T(Text::Quit), {72.0f, top, kWidth - 36.0f, top + kRowHeight}, TextStyle::BodyLarge, p.error);
    AddHit({quitRow, kIdQuit, [this] { host_.OnQuit(); }});
    y = quitCard.bottom + 12.0f;

    canvas_.Text(T(Text::SupportFooter), {20.0f, y, kWidth - 20.0f, y + 64.0f}, TextStyle::BodySmall, p.onSurfaceVariant,
                 TextAlign::Leading, true);
    y += 64.0f;
    canvas_.Text(std::wstring(kAppName) + L" " + APP_VERSION_WSTRING, {20.0f, y, kWidth - 20.0f, y + 20.0f},
                 TextStyle::BodySmall, Alpha(p.onSurfaceVariant, 0.7f));
    y += 20.0f;
    EndScroll(Page::Settings, kHeaderHeight, y);
}

void Panel::DrawAppearance() {
    const Palette& p = P();
    const AppearanceSettings& appearance = settings_.appearance;
    DrawHeader(T(Text::Appearance));
    float y = BeginScroll(Page::Appearance, kHeaderHeight);

    y = SectionTitle(T(Text::Theme), y);
    D2D1_RECT_F card = Card(y, 2.0f * kListPadding + kRowHeight);
    const wchar_t* themes[] = {T(Text::ThemeAuto), T(Text::ThemeLight), T(Text::ThemeDark)};
    DrawSegmented({36.0f, card.top + 16.0f, kWidth - 36.0f, card.top + 48.0f}, kIdTheme, themes,
                  static_cast<int>(appearance.theme), p.secondaryContainer, p.onSecondaryContainer, [this](int index) {
                      Edit([index](Settings& s) { s.appearance.theme = static_cast<ThemeMode>(index); });
                  });
    y = card.bottom + 16.0f;

    // Swatches: Windows accent, presets and the custom color.
    y = SectionTitle(T(Text::Color), y);
    card = Card(y, 2.0f * kListPadding + 2.0f * 60.0f + 36.0f);
    const bool dark = palette_->dark;
    const bool presetChosen = !appearance.windowsAccent &&
                              std::find(std::begin(kAccentPresets), std::end(kAccentPresets), appearance.accent) !=
                                  std::end(kAccentPresets);
    struct Swatch {
        D2D1_COLOR_F color;
        bool selected;
        wchar_t glyph;
        std::function<void()> click;
    };
    std::vector<Swatch> swatches;
    const uint32_t windowsAccent = WindowsAccentColor();
    swatches.push_back({SeedSwatch(windowsAccent, dark), appearance.windowsAccent, 0, [this] {
                            Edit([](Settings& s) { s.appearance.windowsAccent = true; });
                        }});
    for (const uint32_t seed : kAccentPresets) {
        swatches.push_back({SeedSwatch(seed, dark), !appearance.windowsAccent && appearance.accent == seed, 0, [this, seed] {
                                Edit([seed](Settings& s) {
                                    s.appearance.windowsAccent = false;
                                    s.appearance.accent = seed;
                                });
                            }});
    }
    const bool customChosen = !appearance.windowsAccent && !presetChosen;
    swatches.push_back({customChosen ? SeedSwatch(appearance.accent, dark) : p.surfaceContainerHighest, customChosen,
                        kGlyphEdit, [this] { GoTo(Page::ColorPicker); }});
    const float spacing = (kWidth - 64.0f) / 5.0f;
    for (size_t i = 0; i < swatches.size(); ++i) {
        const float cx = 32.0f + spacing * (i % 5 + 0.5f);
        const float cy = card.top + kListPadding + 30.0f + 60.0f * (i / 5);
        const uint32_t id = kIdSwatch + static_cast<uint32_t>(i);
        const Swatch& swatch = swatches[i];
        const float ring = Animate(AnimKey(id, 0), swatch.selected ? 1.0f : 0.0f, 20.0f);
        if (ring > 0.01f) {
            canvas_.StrokeCircle({cx, cy}, kSwatchSize / 2.0f + 4.0f, Alpha(p.onSurface, ring), 2.0f);
        }
        const D2D1_RECT_F hover{cx - 26.0f, cy - 26.0f, cx + 26.0f, cy + 26.0f};
        StateLayer(id, hover, 26.0f, p.onSurface);
        canvas_.FillCircle({cx, cy}, kSwatchSize / 2.0f, swatch.color);
        if (i == 0) {
            // A four-pane window marks the system accent.
            const D2D1_COLOR_F pane{1.0f, 1.0f, 1.0f, 0.92f};
            for (int index = 0; index < 4; ++index) {
                const float x = cx - 7.0f + (index % 2) * 7.5f;
                const float paneTop = cy - 7.0f + (index / 2) * 7.5f;
                canvas_.FillRound({x, paneTop, x + 6.5f, paneTop + 6.5f}, 1.0f, pane);
            }
        } else if (swatch.glyph) {
            canvas_.Glyph(swatch.glyph, {cx, cy}, 16.0f, customChosen ? D2D1_COLOR_F{1.0f, 1.0f, 1.0f, 0.95f} : p.onSurface);
        } else if (swatch.selected) {
            canvas_.Glyph(kGlyphCheck, {cx, cy}, 16.0f, {1.0f, 1.0f, 1.0f, 0.95f});
        }
        AddHit({hover, id, swatch.click});
    }
    std::wstring caption = appearance.windowsAccent ? std::wstring(T(Text::WindowsColor)) + L" · " + HexColor(windowsAccent)
                           : customChosen ? std::wstring(T(Text::CustomColor)) + L" · " + HexColor(appearance.accent)
                                          : HexColor(appearance.accent);
    canvas_.Text(caption, {32.0f, card.bottom - 36.0f, kWidth - 32.0f, card.bottom - 12.0f}, TextStyle::BodySmall,
                 p.onSurfaceVariant, TextAlign::Center);
    y = card.bottom + 16.0f;

    y = SectionTitle(T(Text::Background), y);
    card = Card(y, 2.0f * kListPadding + kRowHeight);
    const wchar_t* backdrops[] = {T(Text::Acrylic), T(Text::Mica), T(Text::Solid)};
    DrawSegmented({36.0f, card.top + 16.0f, kWidth - 36.0f, card.top + 48.0f}, kIdBackdrop, backdrops,
                  static_cast<int>(appearance.backdrop), p.secondaryContainer, p.onSecondaryContainer, [this](int index) {
                      Edit([index](Settings& s) { s.appearance.backdrop = static_cast<Backdrop>(index); });
                  });
    y = card.bottom + 16.0f;

    card = Card(y, 2.0f * kListPadding + kRowHeight + 84.0f);
    float top = card.top + kListPadding;
    DrawSwitchRow(kIdReduceMotion, top, T(Text::ReduceMotion), appearance.reduceMotion,
                  [this] { Edit([](Settings& s) { s.appearance.reduceMotion = !s.appearance.reduceMotion; }); });
    top += kRowHeight;
    canvas_.Text(T(Text::TrayIcon), {36.0f, top + 8.0f, kWidth - 36.0f, top + 32.0f}, TextStyle::BodyLarge, p.onSurface);
    const wchar_t* trays[] = {T(Text::TrayColor), T(Text::TrayMono)};
    DrawSegmented({36.0f, top + 42.0f, kWidth - 36.0f, top + 74.0f}, kIdTray, trays, static_cast<int>(appearance.tray),
                  p.secondaryContainer, p.onSecondaryContainer, [this](int index) {
                      Edit([index](Settings& s) { s.appearance.tray = static_cast<TrayStyle>(index); });
                  });
    y = card.bottom;
    EndScroll(Page::Appearance, kHeaderHeight, y);
}

void Panel::DrawColorPicker() {
    const Palette& p = P();
    DrawHeader(T(Text::CustomColor));
    float y = BeginScroll(Page::ColorPicker, kHeaderHeight);
    const bool dark = palette_->dark;
    const uint32_t seed = settings_.appearance.windowsAccent ? WindowsAccentColor() : settings_.appearance.accent;

    // Preview of the scheme the color produces.
    D2D1_RECT_F card = Card(y, 96.0f);
    const Palette preview = MakePalette(seed, dark);
    canvas_.FillCircle({60.0f, card.top + 48.0f}, 28.0f, SeedSwatch(seed, dark));
    const D2D1_COLOR_F chips[] = {preview.primary, preview.primaryContainer, preview.secondaryContainer,
                                  preview.tertiary, preview.tertiaryContainer};
    for (int i = 0; i < 5; ++i) {
        const float left = 108.0f + i * 50.0f;
        canvas_.FillRound({left, card.top + 28.0f, left + 42.0f, card.top + 68.0f}, 12.0f, chips[i]);
    }
    y = card.bottom + 16.0f;

    // Hue and saturation sliders over OKLCH; the palette uses only these two.
    card = Card(y, 2.0f * 80.0f + 16.0f);
    const float left = 40.0f;
    const float right = kWidth - 40.0f;
    auto slider = [&](float top, const wchar_t* label, const std::wstring& value, uint32_t id, float t,
                      const std::function<D2D1_COLOR_F(float)>& colorAt, const std::function<void(float)>& set) {
        canvas_.Text(label, {36.0f, top + 8.0f, kWidth - 110.0f, top + 32.0f}, TextStyle::BodyLarge, p.onSurface);
        canvas_.Text(value, {kWidth - 110.0f, top + 8.0f, kWidth - 36.0f, top + 32.0f}, TextStyle::LabelLarge,
                     p.onSurfaceVariant, TextAlign::Trailing);
        const float centerY = top + 54.0f;
        constexpr int kStops = 13;
        std::array<D2D1_COLOR_F, kStops> stops{};
        for (int i = 0; i < kStops; ++i) {
            stops[i] = colorAt(static_cast<float>(i) / (kStops - 1));
        }
        constexpr float kTrack = 16.0f;
        canvas_.FillRoundGradient({left, centerY - kTrack / 2.0f, right, centerY + kTrack / 2.0f}, kTrack / 2.0f, stops);
        // The handle stays within the rounded ends; a ring of the card color separates it from the track.
        const float travelLeft = left + kTrack / 2.0f;
        const float travelRight = right - kTrack / 2.0f;
        const float x = travelLeft + (travelRight - travelLeft) * t;
        const float grow = Animate(AnimKey(id, 0), pressedId_ == id ? 1.0f : (hotId_ == id ? 0.5f : 0.0f), 22.0f);
        canvas_.FillCircle({x, centerY}, 13.0f + 2.0f * grow, p.surfaceContainer);
        canvas_.FillCircle({x, centerY}, 10.0f + 2.0f * grow, colorAt(t));
        canvas_.StrokeCircle({x, centerY}, 10.0f + 2.0f * grow, Alpha(p.onSurface, 0.35f), 1.0f);
        Hit hit;
        hit.rect = {left - 12.0f, centerY - 20.0f, right + 12.0f, centerY + 20.0f};
        hit.id = id;
        hit.drag = [travelLeft, travelRight, set](float pointerX, float) {
            set(std::clamp((pointerX - travelLeft) / (travelRight - travelLeft), 0.0f, 1.0f));
        };
        AddHit(std::move(hit));
    };
    auto apply = [this]() {
        const uint32_t rgb = SeedFromHueChroma(pickerHue_, pickerChroma_);
        hexInput_ = HexColor(rgb);
        Edit([rgb](Settings& s) {
            s.appearance.windowsAccent = false;
            s.appearance.accent = rgb;
        });
    };
    wchar_t hueText[16];
    swprintf_s(hueText, L"%.0f°", pickerHue_);
    slider(card.top + 8.0f, T(Text::Hue), hueText, kIdHueSlider, pickerHue_ / 360.0f,
           [dark](float t) { return HueChromaColor(t * 360.0f, 0.12f, dark); },
           [this, apply](float t) {
               pickerHue_ = t * 360.0f;
               apply();
           });
    slider(card.top + 88.0f, T(Text::Saturation), Percent(static_cast<int>(std::lround(pickerChroma_ / 0.20f * 100.0f))),
           kIdChromaSlider, pickerChroma_ / 0.20f,
           [this, dark](float t) { return HueChromaColor(pickerHue_, t * 0.20f, dark); },
           [this, apply](float t) {
               pickerChroma_ = t * 0.20f;
               apply();
           });
    y = card.bottom + 16.0f;

    // HEX field: typing a complete code applies it.
    const D2D1_RECT_F field{16.0f, y, kWidth - 16.0f, y + 48.0f};
    canvas_.FillRound(field, 24.0f, Alpha(p.surfaceContainerHigh, kCardAlpha));
    canvas_.Text(L"HEX", {36.0f, field.top, 90.0f, field.bottom}, TextStyle::LabelLarge, p.onSurfaceVariant);
    canvas_.Text(hexInput_, {92.0f, field.top, kWidth - 36.0f, field.bottom}, TextStyle::BodyLarge, p.onSurface);
    const float caret = 92.0f + canvas_.Measure(hexInput_, TextStyle::BodyLarge) + 1.0f;
    canvas_.FillRect({caret, field.top + 14.0f, caret + 1.5f, field.bottom - 14.0f}, p.primary);
    y = field.bottom + 8.0f;
    DrawHint(T(Text::HexHint), y, 20.0f);
    y += 24.0f;
    EndScroll(Page::ColorPicker, kHeaderHeight, y);
}

void Panel::DrawAudio() {
    const Palette& p = P();
    DrawHeader(T(Text::AudioSection));
    float y = BeginScroll(Page::Audio, kHeaderHeight);

    y = SectionTitle(T(Text::Groups), y);
    const bool ducking = settings_.ducking.enabled;
    const float mediaHint = HintHeight(T(Text::MediaGroupHint));
    const float duckingHint = HintHeight(T(Text::DuckingHint));
    const D2D1_RECT_F card =
        Card(y, 2.0f * kListPadding + 2.0f * kRowHeight + mediaHint + duckingHint + (ducking ? kRowHeight : 0.0f));
    float top = card.top + kListPadding;
    DrawSwitchRow(kIdMediaGroup, top, T(Text::MediaGroupToggle), settings_.mediaGroup,
                  [this] { Edit([](Settings& s) { s.mediaGroup = !s.mediaGroup; }); });
    DrawHint(T(Text::MediaGroupHint), top + kRowHeight - 8.0f, mediaHint);
    top += kRowHeight + mediaHint;
    DrawSwitchRow(kIdDucking, top, T(Text::Ducking), ducking,
                  [this] { Edit([](Settings& s) { s.ducking.enabled = !s.ducking.enabled; }); });
    DrawHint(T(Text::DuckingHint), top + kRowHeight - 8.0f, duckingHint);
    top += kRowHeight + duckingHint;
    if (ducking) {
        const int index = NearestIndex(kDuckingSteps, settings_.ducking.amount);
        const int count = static_cast<int>(std::size(kDuckingSteps));
        DrawStepperRow(kIdDuckingAmount, top, T(Text::DuckingAmount), Percent(kDuckingSteps[index]), 72.0f, index > 0,
                       index < count - 1,
                       [this, index] { Edit([index](Settings& s) { s.ducking.amount = kDuckingSteps[index - 1]; }); },
                       [this, index] { Edit([index](Settings& s) { s.ducking.amount = kDuckingSteps[index + 1]; }); });
    }
    y = DrawRoutingSettings(card.bottom + 16.0f);

    const float eqHint = HintHeight(T(Text::EqFeatureHint));
    const D2D1_RECT_F eqCard = Card(y, 2.0f * kListPadding + kRowHeight + eqHint);
    DrawSwitchRow(kIdEqFeature, eqCard.top + kListPadding, T(Text::Equalizers), settings_.equalizer,
                  [this] { Edit([](Settings& s) { s.equalizer = !s.equalizer; }); });
    DrawHint(T(Text::EqFeatureHint), eqCard.top + kListPadding + kRowHeight - 8.0f, eqHint);
    y = eqCard.bottom;
    (void)p;
    EndScroll(Page::Audio, kHeaderHeight, y);
}

void Panel::DrawHotkeys() {
    const Palette& p = P();
    DrawHeader(T(Text::Hotkeys));
    float y = BeginScroll(Page::Hotkeys, kHeaderHeight);
    const D2D1_RECT_F keys = Card(y, 2.0f * kListPadding + static_cast<float>(kHotkeyCount) * kRowHeight);
    const Text labels[kHotkeyCount] = {Text::MoreGame, Text::MoreChat, Text::Balanced, Text::MuteChat, Text::NextPreset};
    bool anyFailed = false;
    for (int action = 0; action < kHotkeyCount; ++action) {
        const float top = keys.top + kListPadding + action * kRowHeight;
        const uint32_t id = kIdHotkeyRow + action;
        const D2D1_RECT_F row{20.0f, top, kWidth - 20.0f, top + kRowHeight};
        StateLayer(id, row, 16.0f, p.onSurface);

        const bool recording = recording_ == action;
        const bool failed = !recording && hotkeyFailed_[action];
        anyFailed = anyFailed || failed;
        std::wstring text;
        if (recording) {
            text = heldModifiers_ ? ModifiersText(heldModifiers_) + L"…" : T(Text::PressKeys);
        } else {
            text = hotkeys_[action].IsEmpty() ? T(Text::NotAssigned) : HotkeyText(hotkeys_[action]);
        }
        const float chipWidth = canvas_.Measure(text, TextStyle::LabelLarge) + 28.0f;
        const D2D1_RECT_F chip{kWidth - 36.0f - chipWidth, top + 12.0f, kWidth - 36.0f, top + 44.0f};
        DrawRowLabel(T(labels[action]), {36.0f, top, chip.left - 12.0f, top + kRowHeight}, p.onSurface);
        if (recording) {
            canvas_.FillRound(chip, 8.0f, p.primaryContainer);
            canvas_.Text(text, chip, TextStyle::LabelLarge, p.onPrimaryContainer, TextAlign::Center);
        } else {
            canvas_.StrokeRound(Inset(chip, 0.5f, 0.5f), 8.0f, failed ? p.error : p.outline, 1.0f);
            const D2D1_COLOR_F color = failed ? p.error : hotkeys_[action].IsEmpty() ? p.onSurfaceVariant : p.onSurface;
            canvas_.Text(text, chip, TextStyle::LabelLarge, color, TextAlign::Center);
        }
        AddHit({row, id, [this, action] {
                    if (recording_ == action) {
                        StopRecording();
                    } else {
                        StartRecording(action);
                    }
                    Invalidate();
                }});
    }
    y = keys.bottom + 8.0f;
    const Text hint = recording_ >= 0 ? Text::HotkeyHintRecording
                      : anyFailed     ? Text::HotkeyTaken
                                      : Text::HotkeyHintIdle;
    const float hintHeight = HintHeight(T(hint), kWidth - 48.0f);
    canvas_.Text(T(hint), {24.0f, y, kWidth - 24.0f, y + hintHeight}, TextStyle::BodySmall,
                 recording_ < 0 && anyFailed ? p.error : p.onSurfaceVariant, TextAlign::Leading, true);
    y += hintHeight;
    EndScroll(Page::Hotkeys, kHeaderHeight, y);
}

void Panel::DrawNotifications() {
    DrawHeader(T(Text::Notifications));
    float y = BeginScroll(Page::Notifications, kHeaderHeight);

    const float dialHint = HintHeight(T(Text::DialPopupHint));
    D2D1_RECT_F card = Card(y, 2.0f * kListPadding + 2.0f * kRowHeight + dialHint);
    float top = card.top + kListPadding;
    DrawSwitchRow(kIdShowPopup, top, T(Text::ShowPopup), settings_.showPopup,
                  [this] { Edit([](Settings& s) { s.showPopup = !s.showPopup; }); });
    top += kRowHeight;
    DrawSwitchRow(kIdDialPopup, top, T(Text::DialPopup), settings_.dialPopup,
                  [this] { Edit([](Settings& s) { s.dialPopup = !s.dialPopup; }); });
    DrawHint(T(Text::DialPopupHint), top + kRowHeight - 8.0f, dialHint);
    y = card.bottom + 16.0f;

    // Wired headsets report no battery, so the warning is not offered for them.
    if (features_ && (features_->batteryQuery || headset_.battery >= 0)) {
        const bool battery = settings_.battery.enabled;
        card = Card(y, 2.0f * kListPadding + kRowHeight + (battery ? kRowHeight : 0.0f));
        top = card.top + kListPadding;
        DrawSwitchRow(kIdBatteryAlert, top, T(Text::BatteryAlert), battery,
                      [this] { Edit([](Settings& s) { s.battery.enabled = !s.battery.enabled; }); });
        top += kRowHeight;
        if (battery) {
            const int index = NearestIndex(kBatteryThresholds, settings_.battery.threshold);
            const int count = static_cast<int>(std::size(kBatteryThresholds));
            DrawStepperRow(kIdBatteryThreshold, top, T(Text::BatteryThreshold), Percent(kBatteryThresholds[index]), 72.0f,
                           index > 0, index < count - 1,
                           [this, index] { Edit([index](Settings& s) { s.battery.threshold = kBatteryThresholds[index - 1]; }); },
                           [this, index] { Edit([index](Settings& s) { s.battery.threshold = kBatteryThresholds[index + 1]; }); });
        }
        y = card.bottom + 16.0f;
    }

    card = Card(y, 2.0f * kListPadding + kRowHeight);
    DrawSwitchRow(kIdWarnGg, card.top + kListPadding, T(Text::WarnGg), settings_.warnGg,
                  [this] { Edit([](Settings& s) { s.warnGg = !s.warnGg; }); });
    y = card.bottom;
    EndScroll(Page::Notifications, kHeaderHeight, y);
}

void Panel::DrawProfiles() {
    const Palette& p = P();
    DrawHeader(T(Text::Profiles));
    TextButton(kIdAddProfile, {kWidth - 16.0f - 104.0f, 16.0f, kWidth - 16.0f, 48.0f}, kGlyphAdd, T(Text::Add), true,
               [this] { OpenAddApp(AddTarget::Profile); });
    float y = BeginScroll(Page::Profiles, kHeaderHeight);
    const float profilesHint = HintHeight(T(Text::NoProfiles));
    DrawHint(T(Text::NoProfiles), y, profilesHint);
    y += profilesHint;
    if (!settings_.profiles.empty()) {
        const D2D1_RECT_F card = Card(y, 2.0f * kListPadding + settings_.profiles.size() * 64.0f);
        float top = card.top + kListPadding;
        for (const auto& profile : settings_.profiles) {
            const uint32_t id = HashId(kKindProfile, profile.exe);
            const D2D1_RECT_F row{20.0f, top, kWidth - 20.0f, top + 64.0f};
            StateLayer(id, row, 16.0f, p.onSurface);
            AppItem app{profile.exe, profile.path, profile.path.empty() ? profile.exe : host_.AppName(profile.path)};
            DrawAppIcon(app, {32.0f, top + 16.0f, 64.0f, top + 48.0f});
            std::wstring summary;
            if (profile.balance) {
                summary = BalanceText(*profile.balance);
            }
            if (!profile.gameEq.empty()) {
                summary += (summary.empty() ? L"" : L" · ") + profile.gameEq;
            }
            if (profile.headsetEq >= 0 && features_ && profile.headsetEq <= features_->equalizer.CustomIndex()) {
                const int custom = features_->equalizer.CustomIndex();
                summary += (summary.empty() ? L"" : L" · ") +
                           std::wstring(profile.headsetEq == custom ? T(Text::EqCustom)
                                                                    : features_->equalizer.presets[profile.headsetEq].name);
            }
            canvas_.Text(app.name, {76.0f, top + 10.0f, kWidth - 56.0f, top + 32.0f}, TextStyle::BodyLarge, p.onSurface);
            canvas_.Text(summary.empty() ? T(Text::Unchanged) : summary, {76.0f, top + 32.0f, kWidth - 56.0f, top + 52.0f},
                         TextStyle::BodySmall, p.onSurfaceVariant);
            canvas_.Glyph(kGlyphChevronRight, {kWidth - 40.0f, top + 32.0f}, 14.0f, p.onSurfaceVariant);
            AddHit({row, id, [this, exe = profile.exe] {
                        profileExe_ = exe;
                        GoTo(Page::ProfileEdit);
                    }});
            top += 64.0f;
        }
        y = card.bottom;
    }
    EndScroll(Page::Profiles, kHeaderHeight, y);
}

void Panel::DrawProfileEdit() {
    const Palette& p = P();
    GameProfile* profile = EditedProfile();
    if (!profile) {
        DrawHeader(T(Text::Profiles));
        return;
    }
    const std::wstring name = profile->path.empty() ? profile->exe : host_.AppName(profile->path);
    DrawHeader(name.c_str(), _wcsicmp(name.c_str(), profile->exe.c_str()) == 0 ? nullptr : profile->exe.c_str());
    float y = BeginScroll(Page::ProfileEdit, kHeaderHeight);
    const std::wstring exe = profile->exe;
    auto editProfile = [this, exe](std::function<void(GameProfile&)> change) {
        Edit([exe, change](Settings& s) {
            for (auto& item : s.profiles) {
                if (_wcsicmp(item.exe.c_str(), exe.c_str()) == 0) {
                    change(item);
                }
            }
        });
    };

    const bool headsetEq = features_ && features_->equalizer.bandsCommand && features_->equalizer.presetCommand;
    const D2D1_RECT_F card = Card(y, 2.0f * kListPadding + (headsetEq ? 3.0f : 2.0f) * kRowHeight);
    float top = card.top + kListPadding;

    // Balance: unchanged, then the positions from full Game to full Chat (the dial's, or 5% steps).
    const int grid = balanceDial_ ? kBalanceSteps : kCoarseSteps;
    const int balance = profile->balance ? GridIndex(*profile->balance, grid) + grid + 1 : 0;
    const int balanceCount = 2 * grid + 2;
    auto balanceAt = [grid](int index) {
        return index == 0 ? std::optional<int>() : std::optional<int>(BalanceAt(index - 1 - grid, grid));
    };
    auto setBalance = [editProfile, balanceAt](int index) {
        editProfile([&](GameProfile& item) { item.balance = balanceAt(index); });
    };
    DrawStepperRow(kIdProfileBalance, top, T(Text::ProfileBalance),
                   profile->balance ? BalanceText(*profile->balance) : std::wstring(T(Text::Unchanged)), 168.0f, balance > 0,
                   balance < balanceCount - 1, [setBalance, balance] { setBalance(balance - 1); },
                   [setBalance, balance] { setBalance(balance + 1); },
                   [this, balance, balanceCount, balanceAt, setBalance](const D2D1_RECT_F& anchor) {
                       std::vector<MenuItem> items{{T(Text::Unchanged)}};
                       for (int index = 1; index < balanceCount; ++index) {
                           items.push_back({BalanceText(*balanceAt(index))});
                       }
                       OpenMenu(anchor, std::move(items), balance, setBalance);
                   });
    top += kRowHeight;

    // Game equalizer presets of every source, by name; the library is loaded when the page opens.
    std::vector<std::wstring> names{L""};
    for (const auto& list : eqLibrary_) {
        for (const auto& preset : list) {
            if (std::find(names.begin(), names.end(), preset.name) == names.end()) {
                names.push_back(preset.name);
            }
        }
    }
    const int eqIndex = std::max(0, static_cast<int>(std::find(names.begin(), names.end(), profile->gameEq) - names.begin()) %
                                        static_cast<int>(names.size()));
    auto setGameEq = [editProfile](const std::wstring& name) {
        editProfile([&](GameProfile& item) { item.gameEq = name; });
    };
    DrawStepperRow(kIdProfileGameEq, top, T(Text::ProfileGameEq),
                   eqIndex == 0 ? std::wstring(T(Text::Unchanged)) : names[eqIndex], 168.0f, eqIndex > 0,
                   eqIndex < static_cast<int>(names.size()) - 1,
                   [setGameEq, names, eqIndex] { setGameEq(names[eqIndex - 1]); },
                   [setGameEq, names, eqIndex] { setGameEq(names[eqIndex + 1]); },
                   [this, setGameEq, current = profile->gameEq](const D2D1_RECT_F& anchor) {
                       std::vector<const NamedEq*> presets{nullptr};
                       std::vector<MenuItem> items{{T(Text::Unchanged)}};
                       for (MenuItem& item : EqLibraryMenu(presets)) {
                           items.push_back(std::move(item));
                       }
                       std::vector<std::wstring> chosen;
                       int selected = current.empty() ? 0 : -1;
                       for (size_t i = 0; i < presets.size(); ++i) {
                           chosen.push_back(presets[i] ? presets[i]->name : L"");
                           if (presets[i] && selected < 0 && _wcsicmp(presets[i]->name.c_str(), current.c_str()) == 0) {
                               selected = static_cast<int>(i);
                           }
                       }
                       OpenMenu(anchor, std::move(items), selected,
                                [setGameEq, chosen = std::move(chosen)](int index) { setGameEq(chosen[index]); });
                   });
    top += kRowHeight;

    if (headsetEq) {
        const EqualizerSpec& eq = features_->equalizer;
        const int count = eq.CustomIndex() + 2;  // Unchanged, presets, custom.
        const int index = profile->headsetEq + 1;
        // Also called later by the dropdown, so it reads the features at that time.
        auto label = [this](int i) {
            const EqualizerSpec& spec = features_->equalizer;
            return i == 0 ? std::wstring(T(Text::Unchanged))
                          : i - 1 == spec.CustomIndex() ? std::wstring(T(Text::EqCustom)) : std::wstring(spec.presets[i - 1].name);
        };
        auto setHeadsetEq = [editProfile](int choice) {
            editProfile([&](GameProfile& item) { item.headsetEq = choice - 1; });
        };
        DrawStepperRow(kIdProfileHeadsetEq, top, T(Text::ProfileHeadsetEq), label(index), 168.0f, index > 0,
                       index < count - 1, [setHeadsetEq, index] { setHeadsetEq(index - 1); },
                       [setHeadsetEq, index] { setHeadsetEq(index + 1); },
                       [this, count, index, label, setHeadsetEq](const D2D1_RECT_F& anchor) {
                           if (!features_) {
                               return;
                           }
                           std::vector<MenuItem> items;
                           for (int i = 0; i < count; ++i) {
                               items.push_back({label(i)});
                           }
                           OpenMenu(anchor, std::move(items), index, setHeadsetEq);
                       });
    }
    y = card.bottom + 16.0f;

    const D2D1_RECT_F removeCard = Card(y, 2.0f * kListPadding + kRowHeight);
    top = removeCard.top + kListPadding;
    const D2D1_RECT_F row{20.0f, top, kWidth - 20.0f, top + kRowHeight};
    StateLayer(kIdRemoveProfile, row, 16.0f, p.error);
    canvas_.Glyph(kGlyphDelete, {46.0f, top + 28.0f}, 16.0f, p.error);
    canvas_.Text(T(Text::RemoveProfile), {72.0f, top, kWidth - 36.0f, top + kRowHeight}, TextStyle::BodyLarge, p.error);
    AddHit({row, kIdRemoveProfile, [this, exe] {
                Edit([exe](Settings& s) {
                    std::erase_if(s.profiles, [&](const GameProfile& item) { return _wcsicmp(item.exe.c_str(), exe.c_str()) == 0; });
                });
                GoTo(Page::Profiles);
            }});
    y = removeCard.bottom;
    EndScroll(Page::ProfileEdit, kHeaderHeight, y);
}

}  // namespace ui
