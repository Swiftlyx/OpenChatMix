// Settings and their sub-pages.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
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
constexpr int kDuckingMin = 10;  // Ducking amount in percent, in steps of 10.
constexpr int kDuckingMax = 90;
constexpr int kBatteryMin = 5;  // Battery warning threshold in percent, in steps of 5.
constexpr int kBatteryMax = 50;
constexpr float kSwatchSize = 40.0f;
constexpr SliderStyle kPickerSlider{12.0f, 5.0f, 32.0f};

std::wstring BalanceText(int balance) {
    const Levels levels = LevelsFromBalance(balance);
    return std::wstring(T(Text::Game)) + L" " + Percent(levels.game) + L" · " + T(Text::Chat) + L" " +
           Percent(levels.chat);
}

}  // namespace

GameProfile* Panel::EditedProfile() {
    const auto found = std::find_if(
        settings_.profiles.begin(), settings_.profiles.end(),
        [&](const GameProfile& profile) { return _wcsicmp(profile.exe.c_str(), profileExe_.c_str()) == 0; });
    return found == settings_.profiles.end() ? nullptr : &*found;
}

void Panel::DrawSettings() {
    const Palette& p = P();
    DrawTitle(T(Text::Settings));
    float y = BeginScroll(Page::Settings, kHeaderHeight);

    // Every group of settings has a page of its own; only the update state stays here, at the bottom.
    std::wstring profiles;
    for (const auto& profile : settings_.profiles) {
        profiles += (profiles.empty() ? L"" : L", ") + profile.exe;
    }
    std::wstring addOns;
    for (const auto& addOn : settings_.addOns) {
        addOns += (addOns.empty() ? L"" : L", ") + host_.AppName(addOn.path);
    }
    auto open = [this](Page page) { return [this, page] { GoTo(page); }; };
    y = DrawCard(y, {
                        NavItem(HashId(kKindNav, L"general"), kGlyphSettings, p.primary, T(Text::General),
                                T(Text::GeneralSummary), open(Page::General)),
                        NavItem(HashId(kKindNav, L"appearance"), kGlyphPalette, p.primary, T(Text::Appearance),
                                T(Text::AppearanceSummary), open(Page::Appearance)),
                        NavItem(HashId(kKindNav, L"audio"), kGlyphVolume, p.primary, T(Text::AudioSection),
                                T(Text::AudioSummary), open(Page::Audio)),
                        NavItem(HashId(kKindNav, L"outputs"), kGlyphSpeakers, p.primary, T(Text::OutputDevices),
                                T(Text::OutputDevicesSummary), open(Page::Outputs)),
                        NavItem(HashId(kKindNav, L"microphone"), kGlyphMicrophone, p.primary, T(Text::Microphone),
                                T(Text::MicSummary), open(Page::Microphone)),
                        NavItem(HashId(kKindNav, L"hotkeys"), kGlyphKeyboard, p.primary, T(Text::Hotkeys), HotkeyHint(),
                                open(Page::Hotkeys)),
                        NavItem(HashId(kKindNav, L"notifications"), kGlyphBell, p.primary, T(Text::Notifications),
                                T(Text::NotificationsSummary), open(Page::Notifications)),
                        NavItem(HashId(kKindNav, L"profiles"), kGlyphGame, p.primary, T(Text::Profiles),
                                profiles.empty() ? T(Text::ProfilesSummary) : profiles, open(Page::Profiles)),
                        NavItem(HashId(kKindNav, L"addons"), kGlyphPlugin, p.primary, T(Text::AddOns),
                                addOns.empty() ? T(Text::AddOnsSummary) : addOns, open(Page::AddOns)),
                    });

    y = SectionTitle(T(Text::Updates), y + design::kSectionGap);
    CardRow status{kRowHeight, [this, &p](float top) {
        using State = UpdateView::State;
        const std::wstring current = std::wstring(T(Text::VersionLabel)) + L" " + APP_VERSION_WSTRING;
        std::wstring text = current;
        const wchar_t* action = T(Text::UpdateCheckNow);
        std::function<void()> click = [this] { host_.OnCheckForUpdates(); };
        D2D1_COLOR_F color = p.onSurface;
        switch (update_.state) {
        case State::Idle:
            break;
        case State::Checking:
            text = T(Text::UpdateChecking);
            action = nullptr;
            break;
        case State::UpToDate:
            text = std::wstring(T(Text::UpdateCurrent)) + L" · " + APP_VERSION_WSTRING;
            break;
        case State::Available:
            text = std::wstring(T(Text::UpdateAvailable)) + L" · " + update_.version;
            color = p.primary;
            action = T(Text::UpdateInstall);
            click = [this] { host_.OnInstallUpdate(); };
            break;
        case State::Installing:
            text = T(Text::UpdateDownloading);
            action = nullptr;
            break;
        case State::Failed:
            text = T(Text::UpdateFailed);
            color = p.error;
            action = T(Text::UpdateOpenPage);
            click = [this] { host_.OnOpenReleasePage(); };
            break;
        case State::CheckFailed:
            text = T(Text::UpdateCheckFailed);
            color = p.error;
            break;
        }
        const float buttonWidth =
            action ? canvas_.Measure(action, TextStyle::LabelLarge) + 2.0f * design::kSpace5 : 0.0f;
        const float right = kWidth - design::kContentInset;
        canvas_.Text(text, {design::kContentInset, top, right - buttonWidth - design::kSpace2, top + kRowHeight},
                     TextStyle::BodyLarge, color);
        if (action) {
            const float buttonTop = top + (kRowHeight - design::kControlHeight) / 2.0f;
            TextButton(kIdUpdateAction, {right - buttonWidth, buttonTop, right, buttonTop + design::kControlHeight}, 0,
                       action, true, std::move(click));
        }
    }};
    y = DrawCard(y, {
        SwitchItem(kIdCheckUpdates, T(Text::CheckUpdates), settings_.checkUpdates,
                   [this] { Edit([](Settings& s) { s.checkUpdates = !s.checkUpdates; }); }, T(Text::CheckUpdatesHint)),
        status,
    });
    EndScroll(Page::Settings, kHeaderHeight, y);
}

void Panel::DrawGeneral() {
    const Palette& p = P();
    DrawHeader(T(Text::General));
    float y = BeginScroll(Page::General, kHeaderHeight);

    // A list, so more languages fit later; each is named in its own language.
    const int languages = static_cast<int>(std::size(kLanguages));
    const int current = static_cast<int>(std::find(std::begin(kLanguages), std::end(kLanguages), CurrentLanguage()) -
                                         std::begin(kLanguages)) %
                        languages;
    const CardRow language = DropdownItem(
        kIdLanguage, T(Text::LanguageLabel), current, languages, [](int index) { return std::wstring(kLanguageLabels[index]); },
        [this](int index) {
            if (kLanguages[index] != CurrentLanguage()) {
                host_.OnLanguageChanged(kLanguages[index]);
            }
        },
        nullptr);
    y = DrawCard(y, {
        language,
        SwitchItem(kIdAutostart, T(Text::StartWithWindows), autostart_, [this] {
            autostart_ = !autostart_;
            host_.OnAutostartChanged(autostart_);
            Invalidate();
        }),
    }) + design::kGroupGap;

    y = DrawCard(y, {NavItem(kIdTourShow, kGlyphInfo, p.primary, T(Text::TourShow), T(Text::TourShowHint),
                             [this] { GoTo(Page::Welcome); })}) +
        design::kSectionGap;

    // Every setting in one file, and back: two buttons with what they do, or how the last one went.
    y = SectionTitle(T(Text::SettingsBackup), y);
    CardRow transfer{kRowHeight, [this](float top) {
                         const float width = (kWidth - 2.0f * kInset - design::kSpace2) / 2.0f;
                         const float buttonTop = top + (kRowHeight - design::kControlHeight) / 2.0f;
                         const float bottom = buttonTop + design::kControlHeight;
                         TextButton(kIdSettingsExport, {kInset, buttonTop, kInset + width, bottom}, kGlyphSave,
                                    T(Text::SettingsExport), true, [this] { RunFileDialog(FileDialog::ExportSettings); });
                         TextButton(kIdSettingsImport, {kWidth - kInset - width, buttonTop, kWidth - kInset, bottom},
                                    kGlyphImport, T(Text::SettingsImport), true,
                                    [this] { RunFileDialog(FileDialog::ImportSettings); });
                     }};
    y = DrawCard(y, {transfer, HintItem(transferNote_.empty() ? T(Text::SettingsTransferHint) : transferNote_.c_str())}) +
        design::kGroupGap;

    CardRow quit{kRowHeight,
                 [this, &p](float top) {
                     canvas_.Glyph(kGlyphPower, {design::kLeadingIconCenter, top + kRowHeight / 2.0f},
                                   design::kIconSize - 2.0f, p.error);
                     canvas_.Text(T(Text::Quit), {design::kLeadingTextInset, top, kWidth - kInset, top + kRowHeight},
                                  TextStyle::BodyLarge, p.error);
                     AddHit({TileRect(top, kRowHeight), kIdQuit, [this] { host_.OnQuit(); }});
                 },
                 kIdQuit};
    y = DrawCard(y, {quit}) + design::kGroupGap;

    y = PageHint(T(Text::SupportFooter), y);
    canvas_.Text(std::wstring(kAppName) + L" " + APP_VERSION_WSTRING, {kInset, y, kWidth - kInset, y + design::kSpace5},
                 TextStyle::BodySmall, Alpha(p.onSurfaceVariant, 0.7f));
    y += design::kSpace5;
    EndScroll(Page::General, kHeaderHeight, y);
}

void Panel::DrawAppearance() {
    const Palette& p = P();
    const AppearanceSettings& appearance = settings_.appearance;
    DrawHeader(T(Text::Appearance));
    float y = BeginScroll(Page::Appearance, kHeaderHeight);

    y = DrawCard(
            y,
            {
                ChoiceItem(kIdTheme, T(Text::Theme), {T(Text::ThemeAuto), T(Text::ThemeLight), T(Text::ThemeDark)},
                           static_cast<int>(appearance.theme),
                           [this](int index) {
                               Edit([index](Settings& s) { s.appearance.theme = static_cast<ThemeMode>(index); });
                           }),
                ChoiceItem(kIdBackdrop, T(Text::Background), {T(Text::Acrylic), T(Text::Mica), T(Text::Solid)},
                           static_cast<int>(appearance.backdrop),
                           [this](int index) {
                               Edit([index](Settings& s) { s.appearance.backdrop = static_cast<Backdrop>(index); });
                           }),
                ChoiceItem(kIdContrast, T(Text::Contrast),
                           {T(Text::ContrastStandard), T(Text::ContrastMedium), T(Text::ContrastHigh)},
                           static_cast<int>(appearance.contrast),
                           [this](int index) {
                               Edit([index](Settings& s) { s.appearance.contrast = static_cast<ContrastLevel>(index); });
                           }),
            }) +
        design::kSectionGap;

    // Swatches for each side: Game takes the Windows accent, a preset or a custom color; Chat is paired
    // automatically or takes a preset or a custom color, within the limits the theme sets.
    y = SectionTitle(T(Text::Color), y);
    const bool dark = palette_->dark;
    const uint32_t windowsAccent = WindowsAccentColor();
    const uint32_t accent = appearance.windowsAccent ? windowsAccent : appearance.accent;
    auto isPreset = [](uint32_t seed) {
        return std::find(std::begin(kAccentPresets), std::end(kAccentPresets), seed) != std::end(kAccentPresets);
    };

    enum class Mark { None, Windows, Paired, Edit };
    struct Swatch {
        D2D1_COLOR_F color;
        bool selected;
        Mark mark;
        std::function<void()> click;
    };
    constexpr float kLabelHeight = 28.0f;
    constexpr float kSwatchRow = 56.0f;
    constexpr float kCaptionHeight = 28.0f;
    auto swatchRow = [this, &p](const wchar_t* label, uint32_t firstId, std::vector<Swatch> swatches,
                                std::wstring caption) {
        return CardRow{kLabelHeight + 2.0f * kSwatchRow + design::kSpace2 + kCaptionHeight,
                       [this, &p, label, firstId, swatches = std::move(swatches), caption = std::move(caption)](float top) {
            canvas_.Text(label, {kInset, top + design::kHintBottomGap, kWidth - kInset, top + kLabelHeight + 6.0f},
                         TextStyle::BodyLarge, p.onSurface);
            const float spacing = (kWidth - 2.0f * kInset) / 5.0f;
            for (size_t i = 0; i < swatches.size(); ++i) {
                const float cx = kInset + spacing * (i % 5 + 0.5f);
                const float cy = top + kLabelHeight + design::kSpace2 + kSwatchRow / 2.0f + kSwatchRow * (i / 5);
                const uint32_t id = firstId + static_cast<uint32_t>(i);
                const Swatch& swatch = swatches[i];
                // The chosen swatch springs from a circle into a rounded square, as Material 3 selections do.
                const float chosen = Animate(AnimKey(id, 0), swatch.selected ? 1.0f : 0.0f, design::kSpringFastSpatial);
                const float half = kSwatchSize / 2.0f;
                const float radius = half + (design::kRadiusMedium - half) * std::clamp(chosen, 0.0f, 1.2f);
                const D2D1_RECT_F shape{cx - half, cy - half, cx + half, cy + half};
                const D2D1_RECT_F hover = Inset(shape, -4.0f, -4.0f);
                StateLayer(id, hover, radius + 4.0f, p.onSurface);
                const D2D1_COLOR_F white{1.0f, 1.0f, 1.0f, 0.95f};
                switch (swatch.mark) {
                case Mark::Windows: {
                    canvas_.FillRound(shape, radius, swatch.color);
                    // A four-pane window marks the system accent.
                    for (int index = 0; index < 4; ++index) {
                        const float x = cx - 7.0f + (index % 2) * 7.5f;
                        const float paneTop = cy - 7.0f + (index / 2) * 7.5f;
                        canvas_.FillRound({x, paneTop, x + 6.5f, paneTop + 6.5f}, 1.0f, Alpha(white, 0.97f));
                    }
                    break;
                }
                case Mark::Paired: {
                    // Game blending into the paired Chat color, as the tray icon blends the two sides.
                    const D2D1_COLOR_F blend[] = {p.primary, p.primary, swatch.color, swatch.color};
                    canvas_.FillRoundGradient(shape, radius, blend);
                    if (swatch.selected) {
                        canvas_.Glyph(kGlyphCheck, {cx, cy}, 16.0f, white);
                    }
                    break;
                }
                case Mark::Edit:
                    canvas_.FillRound(shape, radius, swatch.selected ? swatch.color : p.surfaceContainerHighest);
                    canvas_.Glyph(kGlyphEdit, {cx, cy}, 16.0f, swatch.selected ? white : p.onSurface);
                    break;
                case Mark::None:
                    canvas_.FillRound(shape, radius, swatch.color);
                    if (swatch.selected) {
                        canvas_.Glyph(kGlyphCheck, {cx, cy}, 16.0f, white);
                    }
                    break;
                }
                AddHit({hover, id, swatch.click});
            }
            const float captionTop = top + kLabelHeight + design::kSpace2 + 2.0f * kSwatchRow;
            canvas_.Text(caption, {kInset, captionTop, kWidth - kInset, captionTop + kCaptionHeight - 4.0f},
                         TextStyle::BodySmall, p.onSurfaceVariant, TextAlign::Center);
        }};
    };

    // Game: the Windows accent, the presets and the custom color.
    const bool gameCustom = !appearance.windowsAccent && !isPreset(appearance.accent);
    std::vector<Swatch> game;
    game.push_back({SeedSwatch(windowsAccent, dark), appearance.windowsAccent, Mark::Windows,
                    [this] { Edit([](Settings& s) { s.appearance.windowsAccent = true; }); }});
    for (const uint32_t seed : kAccentPresets) {
        game.push_back({SeedSwatch(seed, dark), !appearance.windowsAccent && appearance.accent == seed, Mark::None,
                        [this, seed] {
                            Edit([seed](Settings& s) {
                                s.appearance.windowsAccent = false;
                                s.appearance.accent = seed;
                            });
                        }});
    }
    game.push_back({SeedSwatch(appearance.accent, dark), gameCustom, Mark::Edit, [this] {
                        pickerChat_ = false;
                        GoTo(Page::ColorPicker);
                    }});
    const std::wstring gameCaption =
        appearance.windowsAccent ? std::wstring(T(Text::WindowsColor)) + L" · " + HexColor(windowsAccent)
        : gameCustom             ? std::wstring(T(Text::CustomColor)) + L" · " + HexColor(appearance.accent)
                                 : HexColor(appearance.accent);

    // Chat: paired automatically, the presets and the custom color. Swatches show each color as it
    // will be used next to the current Game color.
    const std::optional<uint32_t>& chatAccent = appearance.chatAccent;
    const bool chatCustom = chatAccent && !isPreset(*chatAccent);
    std::vector<Swatch> chat;
    chat.push_back({ChatSwatch(accent, std::nullopt, dark), !chatAccent, Mark::Paired,
                    [this] { Edit([](Settings& s) { s.appearance.chatAccent.reset(); }); }});
    for (const uint32_t seed : kAccentPresets) {
        chat.push_back({ChatSwatch(accent, seed, dark), chatAccent == seed, Mark::None,
                        [this, seed] { Edit([seed](Settings& s) { s.appearance.chatAccent = seed; }); }});
    }
    chat.push_back({ChatSwatch(accent, chatAccent, dark), chatCustom, Mark::Edit, [this] {
                        pickerChat_ = true;
                        GoTo(Page::ColorPicker);
                    }});
    const std::wstring chatHex = HexColor(EffectiveChatSeed(accent, chatAccent));
    const std::wstring chatCaption = !chatAccent ? std::wstring(T(Text::ChatColorAuto)) + L" · " + chatHex
                                     : chatCustom ? std::wstring(T(Text::CustomColor)) + L" · " + chatHex
                                                  : chatHex;

    std::vector<CardRow> colorRows{
        swatchRow(T(Text::GameColor), kIdSwatch, std::move(game), gameCaption),
        swatchRow(T(Text::ChatColor), kIdChatSwatch, std::move(chat), chatCaption),
    };
    // The battery's color is offered only with a headset that reports one, or while it is not the default.
    const bool batteryHeadset = features_ && (features_->batteryQuery || headset_.battery >= 0);
    if (batteryHeadset || appearance.battery != BatteryTint::Theme) {
        colorRows.push_back(BatteryTintItem(kIdBatteryTint, appearance.battery,
                                            [](Settings& s, BatteryTint tint) { s.appearance.battery = tint; }));
    }
    y = DrawCard(y, colorRows);
    // The hint sits close under the group; with its own gap below that makes a section gap.
    y = PageHint(T(Text::ColorsHint), y + design::kSpace2) + design::kSectionGap - design::kPageHintGap;

    y = SectionTitle(T(Text::TrayIcon), y);
    // Each choice of the balanced mark shows the mark itself, in the Game and Chat colors.
    CardRow balanced{design::kChoiceRowHeight, [this, &p, &appearance, batteryHeadset](float top) {
        canvas_.Text(T(Text::TrayBalancedIcon), {kInset, top + design::kHintBottomGap, kWidth - kInset, top + 34.0f},
                     TextStyle::BodyLarge, p.onSurface);
        // The battery is offered only with a headset that reports one, or while it is the choice.
        const bool battery = batteryHeadset || appearance.balancedIcon == BalancedIcon::Battery;
        const wchar_t* marks[] = {L"", L"", L"", L"", L""};
        const int markCount = static_cast<int>(std::size(marks)) - (battery ? 0 : 1);
        const float bottom = top + design::kChoiceRowHeight - design::kHintBottomGap;
        const D2D1_RECT_F choices{kInset, bottom - design::kControlHeight, kWidth - kInset, bottom};
        DrawSegmented(choices, kIdTrayBalanced, std::span(marks, markCount), static_cast<int>(appearance.balancedIcon),
                      p.secondaryContainer, p.onSecondaryContainer, [this](int index) {
                          Edit([index](Settings& s) { s.appearance.balancedIcon = static_cast<BalancedIcon>(index); });
                      });
        // The marks have no labels; their names show as tooltips.
        constexpr Text kMarkNames[] = {Text::TipMarkBars, Text::TipMarkSquare, Text::TipMarkRing, Text::TipMarkEquals,
                                       Text::TipMarkBattery};
        for (int index = 0; index < markCount; ++index) {
            Tooltip(PartId(kIdTrayBalanced, static_cast<uint32_t>(index) + 1), T(kMarkNames[index]));
        }
        const float width = (choices.right - choices.left - (markCount - 1) * design::kButtonGroupGap) / markCount;
        for (int index = 0; index < markCount; ++index) {
            const D2D1_POINT_2F c{choices.left + index * (width + design::kButtonGroupGap) + width / 2.0f,
                                  (choices.top + choices.bottom) / 2.0f};
            DrawBalancedIconPreview(static_cast<BalancedIcon>(index), c);
        }
    }};
    std::vector<CardRow> trayRows{
        ChoiceItem(kIdTray, T(Text::TrayIconColors), {T(Text::TrayColor), T(Text::TrayMono)},
                   static_cast<int>(appearance.tray),
                   [this](int index) { Edit([index](Settings& s) { s.appearance.tray = static_cast<TrayStyle>(index); }); }),
        balanced,
    };
    if (appearance.balancedIcon == BalancedIcon::Battery) {
        trayRows.push_back(HintItem(T(Text::TrayBatteryHint)));
        // The monochrome icon has the taskbar's text color only.
        if (appearance.tray == TrayStyle::Color) {
            trayRows.push_back(BatteryTintItem(kIdTrayBatteryTint, appearance.trayBattery,
                                               [](Settings& s, BatteryTint tint) { s.appearance.trayBattery = tint; }));
        }
    }
    y = DrawCard(y, trayRows) + design::kGroupGap;

    y = DrawCard(y, {
        SwitchItem(kIdReduceMotion, T(Text::ReduceMotion), appearance.reduceMotion,
                   [this] { Edit([](Settings& s) { s.appearance.reduceMotion = !s.appearance.reduceMotion; }); },
                   T(Text::ReduceMotionHint)),
    });
    EndScroll(Page::Appearance, kHeaderHeight, y);
}

void Panel::DrawBalancedIconPreview(BalancedIcon icon, D2D1_POINT_2F c) {
    const Palette& p = P();
    constexpr float kHalf = 8.0f;  // The marks fill a 16 x 16 square, like the tray icon.
    // The square and the ring blend Game into Chat across their middle third, as the tray icon does.
    const D2D1_COLOR_F blend[] = {p.primary, p.primary, p.tertiary, p.tertiary};
    switch (icon) {
    case BalancedIcon::Bars:
        canvas_.FillRound({c.x - 7.0f, c.y - kHalf, c.x - 2.0f, c.y + kHalf}, 2.5f, p.primary);
        canvas_.FillRound({c.x + 2.0f, c.y - kHalf, c.x + 7.0f, c.y + kHalf}, 2.5f, p.tertiary);
        break;
    case BalancedIcon::Square:
        canvas_.FillRoundGradient({c.x - 6.5f, c.y - 6.5f, c.x + 6.5f, c.y + 6.5f}, 3.0f, blend);
        break;
    case BalancedIcon::Ring:
        canvas_.StrokeCircleGradient(c, 5.5f, blend, 2.5f);
        break;
    case BalancedIcon::Equals:
        canvas_.FillRound({c.x - 7.0f, c.y - 5.5f, c.x + 7.0f, c.y - 1.5f}, 2.0f, p.primary);
        canvas_.FillRound({c.x - 7.0f, c.y + 1.5f, c.x + 7.0f, c.y + 5.5f}, 2.0f, p.tertiary);
        break;
    case BalancedIcon::Battery: {
        // The tray battery: a body on its side, the terminal on the right, the charge from the left in
        // its color and the rest of the body faded (the headset's charge when known, else most of one).
        const float charge = headset_.battery >= 0 ? headset_.battery / 100.0f : 0.7f;
        DrawBatteryIcon(canvas_, p, c, 8.4f, charge, false, BatteryTintColor(settings_.appearance.trayBattery, charge));
        break;
    }
    }
}

D2D1_COLOR_F Panel::BatteryTintColor(BatteryTint tint, float charge) const {
    const Palette& p = P();
    return tint == BatteryTint::Theme ? p.primary : BatteryColor(charge, p.dark);
}

Panel::CardRow Panel::BatteryTintItem(uint32_t id, BatteryTint tint, std::function<void(Settings&, BatteryTint)> edit) {
    return ChoiceItem(id, T(Text::BatteryColor), {T(Text::BatteryByTheme), T(Text::BatteryByCharge)},
                      static_cast<int>(tint),
                      [this, edit = std::move(edit)](int index) {
                          Edit([edit, index](Settings& s) { edit(s, static_cast<BatteryTint>(index)); });
                      },
                      T(tint == BatteryTint::Theme ? Text::BatteryThemeHint : Text::BatteryChargeHint));
}

void Panel::DrawColorPicker() {
    const Palette& p = P();
    DrawHeader(T(pickerChat_ ? Text::ChatColor : Text::GameColor));
    float y = BeginScroll(Page::ColorPicker, kHeaderHeight);
    const bool dark = palette_->dark;
    const AppearanceSettings& appearance = settings_.appearance;
    const uint32_t accent = appearance.windowsAccent ? WindowsAccentColor() : appearance.accent;

    // Preview of the scheme the colors produce.
    D2D1_RECT_F card = Card(y, 96.0f);
    const Palette preview = MakePalette(accent, dark, appearance.chatAccent, appearance.contrast);
    canvas_.FillCircle({60.0f, card.top + 48.0f}, 28.0f,
                       pickerChat_ ? ChatSwatch(accent, appearance.chatAccent, dark) : SeedSwatch(accent, dark));
    const D2D1_COLOR_F chips[] = {preview.primary, preview.primaryContainer, preview.secondaryContainer,
                                  preview.tertiary, preview.tertiaryContainer};
    for (int i = 0; i < 5; ++i) {
        const float left = 108.0f + i * 50.0f;
        canvas_.FillRound({left, card.top + 28.0f, left + 42.0f, card.top + 68.0f}, 12.0f, chips[i]);
    }
    y = card.bottom + design::kGroupGap;

    // Hue and saturation sliders over OKLCH; the palette uses only these two. Tracks show each color as
    // the theme will use it, so for Chat the hues kept away from Game show as a flat stretch.
    auto shown = [this, accent, dark](float hueDegrees, float chroma) {
        return pickerChat_ ? ChatSwatch(accent, SeedFromHueChroma(hueDegrees, chroma), dark)
                           : HueChromaColor(hueDegrees, chroma, dark);
    };
    card = Card(y, 2.0f * 80.0f + 16.0f);
    const float left = 40.0f;
    const float right = kWidth - 40.0f;
    auto slider = [&](float top, const wchar_t* label, const std::wstring& value, uint32_t id, float t,
                      const std::function<D2D1_COLOR_F(float)>& colorAt, const std::function<void(float)>& set) {
        canvas_.Text(label, {kInset, top + 8.0f, kWidth - 110.0f, top + 32.0f}, TextStyle::BodyLarge, p.onSurface);
        canvas_.Text(value, {kWidth - 110.0f, top + 8.0f, kWidth - kInset, top + 32.0f}, TextStyle::LabelLarge,
                     p.onSurfaceVariant, TextAlign::Trailing);
        const float centerY = top + 54.0f;
        constexpr int kStops = 13;
        std::array<D2D1_COLOR_F, kStops> stops{};
        for (int i = 0; i < kStops; ++i) {
            stops[i] = colorAt(static_cast<float>(i) / (kStops - 1));
        }
        constexpr float kTrack = 16.0f;
        canvas_.FillRoundGradient({left, centerY - kTrack / 2.0f, right, centerY + kTrack / 2.0f}, kTrack / 2.0f,
                                  stops);
        // The handle stays within the rounded ends; a ring of the card color separates it from the track.
        const float travelLeft = left + kTrack / 2.0f;
        const float travelRight = right - kTrack / 2.0f;
        const float x = travelLeft + (travelRight - travelLeft) * t;
        const float grow =
            Animate(AnimKey(id, 0), pressedId_ == id ? 1.0f : (hotId_ == id ? 0.5f : 0.0f), design::kSpringFastEffects);
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
        SetPickedColor(rgb);
    };
    wchar_t hueText[16];
    swprintf_s(hueText, L"%.0f°", pickerHue_);
    slider(card.top + 8.0f, T(Text::Hue), hueText, kIdHueSlider, pickerHue_ / 360.0f,
           [shown](float t) { return shown(t * 360.0f, kMaxSeedChroma); },
           [this, apply](float t) {
               pickerHue_ = t * 360.0f;
               apply();
           });
    slider(
        card.top + 88.0f, T(Text::Saturation),
        Percent(static_cast<int>(std::lround(pickerChroma_ / kMaxSeedChroma * 100.0f))), kIdChromaSlider,
        pickerChroma_ / kMaxSeedChroma, [this, shown](float t) { return shown(pickerHue_, t * kMaxSeedChroma); },
        [this, apply](float t) {
            pickerChroma_ = t * kMaxSeedChroma;
            apply();
        });
    y = card.bottom + design::kGroupGap;

    // HEX field: typing a complete code applies it.
    const D2D1_RECT_F field{kGutter, y, kWidth - kGutter, y + kRowHeight};
    canvas_.FillRound(field, design::kRadiusGroup, Alpha(p.surfaceContainer, p.tileAlpha));
    canvas_.StrokeRound(Inset(field, 1.0f, 1.0f), design::kRadiusGroup - 1.0f, p.primary, 2.0f);
    canvas_.Text(L"HEX", {kInset, field.top, kInset + 52.0f, field.bottom}, TextStyle::LabelLarge, p.onSurfaceVariant);
    canvas_.Text(hexInput_, {kInset + 52.0f, field.top, kWidth - kInset, field.bottom}, TextStyle::BodyLarge,
                 p.onSurface);
    const float caret = kInset + 52.0f + canvas_.Measure(hexInput_, TextStyle::BodyLarge) + 1.0f;
    canvas_.FillRect({caret, field.top + 14.0f, caret + 1.5f, field.bottom - 14.0f}, p.primary);
    y = PageHint(T(Text::HexHint), field.bottom + design::kSpace2);
    y = PageHint(T(Text::ColorsHint), y);
    EndScroll(Page::ColorPicker, kHeaderHeight, y);
}

void Panel::SetPickedColor(uint32_t rgb) {
    const bool chat = pickerChat_;
    Edit([rgb, chat](Settings& s) {
        if (chat) {
            s.appearance.chatAccent = rgb;
        } else {
            s.appearance.windowsAccent = false;
            s.appearance.accent = rgb;
        }
    });
}

void Panel::DrawAudio() {
    DrawHeader(T(Text::AudioSection));
    float y = BeginScroll(Page::Audio, kHeaderHeight);

    y = SectionTitle(T(Text::Groups), y);
    std::vector<CardRow> groups{
        SwitchItem(kIdMediaGroup, T(Text::MediaGroupToggle), settings_.mediaGroup,
                   [this] { Edit([](Settings& s) { s.mediaGroup = !s.mediaGroup; }); }, T(Text::MediaGroupHint)),
        SwitchItem(kIdDucking, T(Text::Ducking), settings_.ducking.enabled,
                   [this] { Edit([](Settings& s) { s.ducking.enabled = !s.ducking.enabled; }); }, T(Text::DuckingHint)),
    };
    if (settings_.ducking.enabled) {
        const int amount = settings_.ducking.amount;
        groups.push_back(NumberItem(
            kIdDuckingAmount, T(Text::DuckingAmount), Percent(amount), amount > kDuckingMin, amount < kDuckingMax,
            [this] { Edit([](Settings& s) { s.ducking.amount = std::max(kDuckingMin, s.ducking.amount - 10); }); },
            [this] { Edit([](Settings& s) { s.ducking.amount = std::min(kDuckingMax, s.ducking.amount + 10); }); },
            [this](const std::wstring& text) {
                const int value = _wtoi(text.c_str());
                if (value <= 0) {
                    return false;
                }
                Edit([value](Settings& s) { s.ducking.amount = std::clamp(value, kDuckingMin, kDuckingMax); });
                return true;
            }));
    }
    y = DrawCard(y, groups) + design::kSectionGap;
    y = DrawRoutingSettings(y) + design::kSectionGap - design::kGroupGap;

    y = SectionTitle(T(Text::Equalizers), y);
    y = DrawCard(y, {
        SwitchItem(kIdEqFeature, T(Text::Equalizers), settings_.equalizer,
                   [this] { Edit([](Settings& s) { s.equalizer = !s.equalizer; }); }, T(Text::EqFeatureHint)),
    });
    EndScroll(Page::Audio, kHeaderHeight, y);
}

void Panel::DrawHotkeys() {
    const Palette& p = P();
    DrawHeader(T(Text::Hotkeys));
    float y = BeginScroll(Page::Hotkeys, kHeaderHeight);
    const Text labels[kHotkeyCount] = {Text::MoreGame,   Text::MoreChat,       Text::Balanced,
                                       Text::MuteGame,   Text::MuteChat,       Text::MuteMedia,
                                       Text::NextOutput, Text::NextMicrophone, Text::StartScreensaver};
    bool anyFailed = false;
    std::vector<CardRow> keys;
    for (int action = 0; action < kHotkeyCount; ++action) {
        // Muting Media only means something while the Media group is in use.
        if (action == kHotkeyMuteMedia && !settings_.MediaAvailable()) {
            continue;
        }
        // Nor the next microphone while the microphone is not processed.
        if (action == kHotkeyNextMicrophone && !settings_.mic.enabled) {
            continue;
        }
        const uint32_t id = kIdHotkeyRow + action;
        const bool recording = recording_ == action;
        const bool failed = !recording && hotkeyFailed_[action];
        anyFailed = anyFailed || failed;
        keys.push_back({kRowHeight,
                        [this, &p, action, recording, failed, id, label = labels[action]](float top) {
                            std::wstring text;
                            if (recording) {
                                text = heldModifiers_ ? ModifiersText(heldModifiers_) + L"…" : T(Text::PressKeys);
                            } else {
                                text = hotkeys_[action].IsEmpty() ? T(Text::NotAssigned) : HotkeyText(hotkeys_[action]);
                            }
                            // The shortcut sits in a chip; while recording the chip fills.
                            const float chipWidth =
                                canvas_.Measure(text, TextStyle::LabelLarge) + 2.0f * design::kSpace3;
                            const float chipTop = top + (kRowHeight - design::kControlHeight) / 2.0f;
                            const D2D1_RECT_F chip{kWidth - kInset - chipWidth, chipTop, kWidth - kInset,
                                                   chipTop + design::kControlHeight};
                            DrawRowLabel(T(label), {kInset, top, chip.left - design::kSpace3, top + kRowHeight},
                                         p.onSurface);
                            if (recording) {
                                canvas_.FillRound(chip, design::kRadiusSmall, p.primaryContainer);
                                canvas_.Text(text, chip, TextStyle::LabelLarge, p.onPrimaryContainer,
                                             TextAlign::Center);
                            } else {
                                canvas_.StrokeRound(Inset(chip, 0.5f, 0.5f), design::kRadiusSmall,
                                                    failed ? p.error : p.outlineVariant, 1.0f);
                                const D2D1_COLOR_F color = failed                         ? p.error
                                                           : hotkeys_[action].IsEmpty() ? p.onSurfaceVariant
                                                                                        : p.onSurface;
                                canvas_.Text(text, chip, TextStyle::LabelLarge, color, TextAlign::Center);
                            }
                            AddHit({TileRect(top, kRowHeight), id, [this, action] {
                                        if (recording_ == action) {
                                            StopRecording();
                                        } else {
                                            StartRecording(action);
                                        }
                                        Invalidate();
                                    }});
                        },
                        id});
    }
    y = DrawCard(y, keys) + design::kSpace2;
    const Text hint = recording_ >= 0 ? Text::HotkeyHintRecording
                      : anyFailed     ? Text::HotkeyTaken
                                      : Text::HotkeyHintIdle;
    y = PageHint(T(hint), y, recording_ < 0 && anyFailed ? std::optional(p.error) : std::nullopt) + design::kSpace1;

    // The balance on the mouse wheel while a modifier combination is held.
    const int choices = static_cast<int>(std::size(kWheelModifierChoices));
    const int wheel = static_cast<int>(std::find(std::begin(kWheelModifierChoices), std::end(kWheelModifierChoices),
                                                 settings_.wheelModifiers) -
                                       std::begin(kWheelModifierChoices)) %
                      choices;
    y = DrawCard(y, {
        DropdownItem(
            kIdWheelControl, T(Text::WheelControl), wheel, choices,
            [](int index) {
                const std::wstring keys = ModifiersText(kWheelModifierChoices[index]);
                return index == 0 ? std::wstring(T(Text::OptOff)) : keys.substr(0, keys.size() - 3);
            },
            [this](int index) { Edit([index](Settings& s) { s.wheelModifiers = kWheelModifierChoices[index]; }); },
            T(Text::WheelControlHint)),
    });
    EndScroll(Page::Hotkeys, kHeaderHeight, y);
}

void Panel::DrawNotifications() {
    DrawHeader(T(Text::Notifications));
    float y = BeginScroll(Page::Notifications, kHeaderHeight);

    y = DrawCard(y, {
        SwitchItem(kIdShowPopup, T(Text::ShowPopup), settings_.showPopup,
                   [this] { Edit([](Settings& s) { s.showPopup = !s.showPopup; }); }),
        SwitchItem(kIdDialPopup, T(Text::DialPopup), settings_.dialPopup,
                   [this] { Edit([](Settings& s) { s.dialPopup = !s.dialPopup; }); }, T(Text::DialPopupHint)),
    }) + design::kGroupGap;

    // Wired headsets report no battery, so the warning is not offered for them.
    if (features_ && (features_->batteryQuery || headset_.battery >= 0)) {
        std::vector<CardRow> battery{
            SwitchItem(kIdBatteryAlert, T(Text::BatteryAlert), settings_.battery.enabled,
                       [this] { Edit([](Settings& s) { s.battery.enabled = !s.battery.enabled; }); }),
        };
        if (settings_.battery.enabled) {
            const int threshold = settings_.battery.threshold;
            battery.push_back(NumberItem(
                kIdBatteryThreshold, T(Text::BatteryThreshold), Percent(threshold), threshold > kBatteryMin,
                threshold < kBatteryMax,
                [this] {
                    Edit([](Settings& s) { s.battery.threshold = std::max(kBatteryMin, s.battery.threshold - 5); });
                },
                [this] {
                    Edit([](Settings& s) { s.battery.threshold = std::min(kBatteryMax, s.battery.threshold + 5); });
                },
                [this](const std::wstring& text) {
                    const int value = _wtoi(text.c_str());
                    if (value <= 0) {
                        return false;
                    }
                    Edit([value](Settings& s) { s.battery.threshold = std::clamp(value, kBatteryMin, kBatteryMax); });
                    return true;
                }));
        }
        y = DrawCard(y, battery) + design::kGroupGap;
    }

    y = DrawCard(y, {
                        SwitchItem(kIdWarnGg, T(Text::WarnGg), settings_.warnGg,
                                   [this] { Edit([](Settings& s) { s.warnGg = !s.warnGg; }); }),
                    });
    EndScroll(Page::Notifications, kHeaderHeight, y);
}

void Panel::DrawProfiles() {
    const Palette& p = P();
    DrawHeader(T(Text::Profiles), nullptr, 104.0f);
    const float buttonTop = (kHeaderHeight - design::kButtonHeight) / 2.0f;
    TextButton(kIdAddProfile,
               {kWidth - kGutter - 104.0f, buttonTop, kWidth - kGutter, buttonTop + design::kButtonHeight}, kGlyphAdd,
               T(Text::Add), true, [this] { OpenAddApp(AddTarget::Profile); });
    float y = BeginScroll(Page::Profiles, kHeaderHeight);
    y = PageHint(T(Text::NoProfiles), y);
    std::vector<CardRow> rows;
    for (const auto& profile : settings_.profiles) {
        const uint32_t id = HashId(kKindProfile, profile.exe);
        rows.push_back({design::kTwoLineRowHeight,
                        [this, &p, &profile, id](float top) {
                            AppItem app{profile.exe, profile.path,
                                        profile.path.empty() ? profile.exe : host_.AppName(profile.path)};
                            const float iconTop = top + (design::kTwoLineRowHeight - 32.0f) / 2.0f;
                            DrawAppIcon(app, {kInset, iconTop, kInset + 32.0f, iconTop + 32.0f});
                            std::wstring summary;
                            if (profile.balance) {
                                summary = BalanceText(*profile.balance);
                            }
                            if (!profile.gameEq.empty()) {
                                summary += (summary.empty() ? L"" : L" · ") + profile.gameEq;
                            }
                            if (profile.headsetEq >= 0 && features_ &&
                                profile.headsetEq <= features_->equalizer.CustomIndex()) {
                                const int custom = features_->equalizer.CustomIndex();
                                summary += (summary.empty() ? L"" : L" · ") +
                                           std::wstring(profile.headsetEq == custom
                                                            ? T(Text::EqCustom)
                                                            : features_->equalizer.presets[profile.headsetEq].name);
                            }
                            const float textLeft = kInset + 32.0f + design::kSpace3;
                            const float textRight = design::kTrailingIconCenter - design::kSpace4;
                            canvas_.Text(app.name, {textLeft, top + 9.0f, textRight, top + 31.0f}, TextStyle::BodyLarge,
                                         p.onSurface);
                            canvas_.Text(summary.empty() ? T(Text::Unchanged) : summary,
                                         {textLeft, top + 31.0f, textRight, top + 50.0f}, TextStyle::BodySmall,
                                         p.onSurfaceVariant);
                            canvas_.Glyph(kGlyphChevronRight,
                                          {design::kTrailingIconCenter, top + design::kTwoLineRowHeight / 2.0f},
                                          design::kSmallIconSize, p.onSurfaceVariant);
                            AddHit({TileRect(top, design::kTwoLineRowHeight), id, [this, exe = profile.exe] {
                                        profileExe_ = exe;
                                        GoTo(Page::ProfileEdit);
                                    }});
                        },
                        id});
    }
    if (!rows.empty()) {
        y = DrawCard(y, rows);
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
    std::vector<CardRow> rows;

    // Balance: unchanged, then the positions from full Game to full Chat (the dial's, or 5% steps).
    const int grid = balanceDial_ ? kBalanceSteps : kCoarseSteps;
    const int balance = profile->balance ? GridIndex(*profile->balance, grid) + grid + 1 : 0;
    auto balanceAt = [grid](int index) {
        return index == 0 ? std::optional<int>() : std::optional<int>(BalanceAt(index - 1 - grid, grid));
    };
    rows.push_back(DropdownItem(
        kIdProfileBalance, T(Text::ProfileBalance), balance, 2 * grid + 2,
        [balanceAt](int index) {
            return index == 0 ? std::wstring(T(Text::Unchanged)) : BalanceText(*balanceAt(index));
        },
        [editProfile, balanceAt](int index) {
            editProfile([&](GameProfile& item) { item.balance = balanceAt(index); });
        }));

    // An equalizer preset per channel, from every source and by name; each menu loads its channel's library.
    auto eqRow = [this, &p, editProfile](uint32_t id, Text label, EqChannel channel, const std::wstring& current,
                                        std::wstring GameProfile::*field) {
        auto set = [editProfile, field](const std::wstring& name) {
            editProfile([&](GameProfile& item) { item.*field = name; });
        };
        return CardRow{kRowHeight, [this, &p, id, label, channel, current, set](float top) {
                           const D2D1_RECT_F dropdown = DrawDropdown(
                               id, kWidth - kInset, top + kRowHeight / 2.0f,
                               current.empty() ? std::wstring(T(Text::Unchanged)) : current, 200.0f,
                               [this, channel, current, set](const D2D1_RECT_F& anchor) {
                                   LoadEqLibrary(channel);
                                   std::vector<const NamedEq*> presets{nullptr};
                                   std::vector<MenuItem> items{{T(Text::Unchanged)}};
                                   for (MenuItem& item : EqLibraryMenu(presets)) {
                                       items.push_back(std::move(item));
                                   }
                                   std::vector<std::wstring> chosen;
                                   int selected = current.empty() ? 0 : -1;
                                   for (size_t i = 0; i < presets.size(); ++i) {
                                       chosen.push_back(presets[i] ? presets[i]->name : L"");
                                       if (presets[i] && selected < 0 &&
                                           _wcsicmp(presets[i]->name.c_str(), current.c_str()) == 0) {
                                           selected = static_cast<int>(i);
                                       }
                                   }
                                   OpenMenu(anchor, std::move(items), selected,
                                            [set, chosen = std::move(chosen)](int index) { set(chosen[index]); });
                               });
                           DrawRowLabel(T(label), {kInset, top, dropdown.left - design::kSpace3, top + kRowHeight},
                                        p.onSurface);
                       }};
    };
    rows.push_back(eqRow(kIdProfileGameEq, Text::ProfileGameEq, EqChannel::Game, profile->gameEq, &GameProfile::gameEq));
    rows.push_back(eqRow(kIdProfileChatEq, Text::ProfileChatEq, EqChannel::Chat, profile->chatEq, &GameProfile::chatEq));
    if (routing_.mediaRouted || !profile->mediaEq.empty()) {
        rows.push_back(
            eqRow(kIdProfileMediaEq, Text::ProfileMediaEq, EqChannel::Media, profile->mediaEq, &GameProfile::mediaEq));
    }

    if (features_ && features_->equalizer.bandsCommand && features_->equalizer.presetCommand) {
        const EqualizerSpec& eq = features_->equalizer;
        // Unchanged, presets, custom. Also called later by the dropdown, so it reads the features then.
        rows.push_back(DropdownItem(
            kIdProfileHeadsetEq, T(Text::ProfileHeadsetEq), profile->headsetEq + 1, eq.CustomIndex() + 2,
            [this](int i) {
                if (!features_) {
                    return std::wstring();
                }
                const EqualizerSpec& spec = features_->equalizer;
                return i == 0                       ? std::wstring(T(Text::Unchanged))
                       : i - 1 == spec.CustomIndex() ? std::wstring(T(Text::EqCustom))
                                                     : std::wstring(spec.presets[i - 1].name);
            },
            [editProfile](int choice) { editProfile([&](GameProfile& item) { item.headsetEq = choice - 1; }); }));
    }
    y = DrawCard(y, rows) + design::kGroupGap;

    CardRow remove{kRowHeight,
                   [this, &p, exe](float top) {
                       canvas_.Glyph(kGlyphDelete, {design::kLeadingIconCenter, top + kRowHeight / 2.0f},
                                     design::kIconSize - 2.0f, p.error);
                       canvas_.Text(T(Text::RemoveProfile),
                                    {design::kLeadingTextInset, top, kWidth - kInset, top + kRowHeight},
                                    TextStyle::BodyLarge, p.error);
                       AddHit({TileRect(top, kRowHeight), kIdRemoveProfile, [this, exe] {
                                   Edit([exe](Settings& s) {
                                       std::erase_if(s.profiles, [&](const GameProfile& item) {
                                           return _wcsicmp(item.exe.c_str(), exe.c_str()) == 0;
                                       });
                                   });
                                   GoTo(Page::Profiles);
                               }});
                   },
                   kIdRemoveProfile};
    y = DrawCard(y, {remove});
    EndScroll(Page::ProfileEdit, kHeaderHeight, y);
}

}  // namespace ui
