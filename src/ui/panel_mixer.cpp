// Mixer page, groups and adding apps.

#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

#include "../app.h"
#include "panel.h"
#include "panel_internal.h"
#include "widgets.h"

namespace ui {

using namespace detail;

namespace {

constexpr float kAppRowHeight = 56.0f;  // App rows: a 32 DIP icon beside two lines.
constexpr float kGroupVolumeRowHeight = 100.0f;  // A group on the mixer with its volume slider below.
constexpr float kGroupSliderY = 77.0f;           // The slider's center from the top of the row.
constexpr float kGroupPercentWidth = 56.0f;      // The volume beside the chevron.
// The compact group row, for groups that would not fit otherwise: the apps beside the group's name on one line.
constexpr float kGroupCompactRowHeight = 76.0f;
constexpr float kGroupCompactLineHeight = 40.0f;  // The line with the name; it opens the group.
constexpr float kGroupCompactSliderY = 54.0f;

// The text cut to fit maxWidth with an ellipsis, and the width it takes; the ellipsis follows the last
// character that fits, so nothing after it leaves a gap.
std::wstring Elide(Canvas& canvas, const std::wstring& text, TextStyle style, float maxWidth, float& width) {
    width = std::ceil(canvas.Measure(text, style));
    if (width <= maxWidth) {
        return text;
    }
    size_t low = 0;
    size_t high = text.size();
    while (low < high) {
        const size_t middle = (low + high + 1) / 2;
        if (canvas.Measure(text.substr(0, middle) + L"…", style) <= maxWidth) {
            low = middle;
        } else {
            high = middle - 1;
        }
    }
    std::wstring result = text.substr(0, low);
    while (!result.empty() && std::iswspace(result.back())) {
        result.pop_back();
    }
    result += L"…";
    width = std::ceil(canvas.Measure(result, style));
    return result;
}

const wchar_t* GroupName(AppGroup group) {
    switch (group) {
    case AppGroup::Chat: return T(Text::Chat);
    case AppGroup::Media: return T(Text::Media);
    case AppGroup::Aux: return T(Text::Aux);
    case AppGroup::Game: break;
    }
    return T(Text::Game);
}

wchar_t GroupGlyph(AppGroup group) {
    switch (group) {
    case AppGroup::Chat: return kGlyphChat;
    case AppGroup::Media: return kGlyphVolume;
    case AppGroup::Aux: return kGlyphApps;
    case AppGroup::Game: break;
    }
    return kGlyphGame;
}

// As many app names as fit in width, then how many more the group holds; a first name too long for it is cut.
std::wstring AppNames(Canvas& canvas, const std::vector<AppItem>& members, float width) {
    std::wstring names;
    for (size_t index = 0; index < members.size(); ++index) {
        const std::wstring candidate = names.empty() ? members[index].name : names + L", " + members[index].name;
        const size_t rest = members.size() - index - 1;
        const std::wstring more = rest ? L" +" + std::to_wstring(rest) : L"";
        if (!names.empty() && canvas.Measure(candidate + more, TextStyle::BodySmall) > width) {
            names += L" +" + std::to_wstring(members.size() - index);
            break;
        }
        names = candidate;
    }
    float shown = 0.0f;
    return Elide(canvas, names, TextStyle::BodySmall, width, shown);
}

// The group an app is shown in: Media and Aux apps are with Game while their group is not in use.
AppGroup ShownGroup(const Settings& settings, AppGroup group) {
    if ((group == AppGroup::Media && !settings.MediaAvailable()) || (group == AppGroup::Aux && !settings.AuxAvailable())) {
        return AppGroup::Game;
    }
    return group;
}

}  // namespace

std::vector<AppGroup> Panel::AvailableGroups() const {
    std::vector<AppGroup> groups{AppGroup::Game, AppGroup::Chat};
    if (settings_.MediaAvailable()) {
        groups.push_back(AppGroup::Media);
    }
    if (settings_.AuxAvailable()) {
        groups.push_back(AppGroup::Aux);
    }
    return groups;
}

std::vector<AppItem> Panel::AppsInGroup(AppGroup group) const {
    std::vector<AppItem> result;
    for (const auto& app : apps_) {
        if (ShownGroup(settings_, app.group) == group) {
            result.push_back(app);
        }
    }
    return result;
}

void Panel::OpenGroup(AppGroup group) {
    group_ = group;
    GoTo(Page::Group);
}

void Panel::OpenAddApp(AddTarget target) {
    addTarget_ = target;
    addGroup_ = group_;
    GoTo(Page::AddApp);
}

std::wstring Panel::HotkeyHint() const {
    const Hotkey& toGame = hotkeys_[kHotkeyTowardGame];
    const Hotkey& toChat = hotkeys_[kHotkeyTowardChat];
    const Hotkey& center = hotkeys_[kHotkeyCenter];
    if (toGame.IsEmpty() || toChat.IsEmpty()) {
        return T(Text::HotkeysInSettings);
    }
    std::wstring text = toGame.modifiers == toChat.modifiers
                            ? ModifiersText(toGame.modifiers) + KeyText(toGame.key) + L" / " + KeyText(toChat.key)
                            : HotkeyText(toGame) + L" / " + HotkeyText(toChat);
    if (!center.IsEmpty()) {
        text += L"   ·   ";
        text += center.modifiers == toGame.modifiers ? KeyText(center.key) : HotkeyText(center);
        text += L" ";
        text += T(Text::BalancedSuffix);
    }
    return text;
}

void Panel::DrawMixer() {
    const Palette& p = P();

    // App mark: two pills mirroring the current Game and Chat levels.
    const float gameLevel = Animate(AnimKey(kIdMark, 0), gameMuted_ ? 0.0f : levels_.game / 100.0f,
                                    design::kSpringSlowSpatial);
    const float chatLevel = Animate(AnimKey(kIdMark, 1), chatMuted_ ? 0.0f : levels_.chat / 100.0f,
                                     design::kSpringSlowSpatial);
    auto pill = [&](float x, float level, D2D1_COLOR_F color) {
        const float height = 8.0f + 20.0f * std::max(0.0f, level);
        canvas_.FillRound({x, 42.0f - height, x + 8.0f, 42.0f}, 4.0f, color);
    };
    pill(kInset - 6.0f, gameLevel, p.primary);
    pill(kInset + 6.0f, chatLevel, p.tertiary);
    const float textLeft = kInset + 24.0f;
    const float textRight = kWidth - kGutter - design::kIconButtonSize;
    canvas_.Text(kAppName, {textLeft, 9.0f, textRight, 29.0f}, TextStyle::TitleMedium, p.onSurface);
    // A wireless headset's battery follows the app's name: the icon as in the tray, then the percent.
    if (headset_.battery >= 0 && !headset_.headsetOff) {
        constexpr float kBatteryIconWidth = 14.0f;
        const float iconLeft =
            textLeft + std::ceil(canvas_.Measure(kAppName, TextStyle::TitleMedium)) + design::kSpace3;
        const float charge = headset_.battery / 100.0f;
        DrawBatteryIcon(canvas_, p, {iconLeft + kBatteryIconWidth / 2.0f, 19.0f}, 7.5f, charge, headset_.charging,
                        BatteryTintColor(settings_.appearance.battery, charge));
        canvas_.Text(Percent(headset_.battery), {iconLeft + kBatteryIconWidth + design::kSpace1, 10.0f, textRight, 28.0f},
                     TextStyle::BodySmall, p.onSurfaceVariant);
    }
    canvas_.FillCircle({textLeft + 3.0f, 38.0f}, 3.0f, deviceConnected_ ? p.primary : p.outline);
    // The status line: the name, how the headset is connected as an outlined chip, then what the dial does.
    // When it does not fit, what the dial does gives way first, then the name is cut.
    constexpr float kChipPadding = 6.0f;
    const float statusLeft = textLeft + 11.0f;
    const float chipWidth = connection_.empty()
                                ? 0.0f
                                : std::ceil(canvas_.Measure(connection_, TextStyle::LabelSmall)) + 2.0f * kChipPadding;
    const float chipSpace = chipWidth > 0.0f ? design::kSpace2 + chipWidth : 0.0f;
    float nameWidth = 0.0f;
    const std::wstring name =
        Elide(canvas_, device_, TextStyle::BodySmall, std::max(0.0f, textRight - statusLeft - chipSpace), nameWidth);
    float statusRight = statusLeft + nameWidth;
    canvas_.Text(name, {statusLeft, 29.0f, statusRight + 1.0f, 47.0f}, TextStyle::BodySmall, p.onSurfaceVariant);
    if (chipWidth > 0.0f) {
        const D2D1_RECT_F chip{statusRight + design::kSpace2, 30.0f, statusRight + chipSpace, 46.0f};
        canvas_.StrokeRound({chip.left + 0.5f, chip.top + 0.5f, chip.right - 0.5f, chip.bottom - 0.5f}, 7.5f,
                            p.outlineVariant, 1.0f);
        canvas_.Text(connection_, chip, TextStyle::LabelSmall, p.onSurfaceVariant, TextAlign::Center);
        statusRight = chip.right;
    }
    const std::wstring detail = deviceDetail_.empty() ? L"" : L" · " + deviceDetail_;
    if (!detail.empty() && statusRight + canvas_.Measure(detail, TextStyle::BodySmall) <= textRight) {
        canvas_.Text(detail, {statusRight, 29.0f, textRight, 47.0f}, TextStyle::BodySmall, p.onSurfaceVariant);
    }
    DrawPinButton();

    DrawMixCard({kGutter, kHeaderHeight, kWidth - kGutter, kHeaderHeight + 148.0f});

    // The ChatMix card stays in place; the groups below it scroll when they don't fit.
    const float scrollTop = kHeaderHeight + 148.0f;
    const float start = BeginScroll(Page::Mixer, scrollTop);
    float y = SectionTitle(T(Text::Groups), start - 4.0f + design::kSectionGap);
    const std::vector<AppGroup> groups = AvailableGroups();
    // Groups that would run past the bottom of the page take compact rows, so all of them show without scrolling;
    // the virtual device mode card below them may still need it. The page ends kSpace3 below its content.
    const float groupsTop = scrollTop + 4.0f + (y - start);
    const float rowGaps = design::kSegmentGap * static_cast<float>(groups.size() - 1);
    const bool compact =
        groupsTop + kGroupVolumeRowHeight * static_cast<float>(groups.size()) + rowGaps + design::kSpace3 > ContentBottom();
    // The text of a row ends before the volume, which sits on the chevron's line, beside it.
    const float percentRight = design::kTrailingIconCenter - design::kSpace4;
    const float namesRight = percentRight - kGroupPercentWidth - design::kSpace3;
    std::vector<CardRow> rows;
    for (const AppGroup group : groups) {
        // A compact row has the names beside the group's name, a regular one below it.
        const float namesLeft =
            design::kLeadingTextInset +
            (compact ? std::ceil(canvas_.Measure(GroupName(group), TextStyle::BodyLarge)) + design::kSpace2 : 0.0f);
        const std::wstring names = AppNames(canvas_, AppsInGroup(group), std::max(0.0f, namesRight - namesLeft));
        const D2D1_COLOR_F color = group == AppGroup::Chat    ? p.tertiary
                                   : group == AppGroup::Media ? p.onSurfaceVariant
                                   : group == AppGroup::Aux   ? p.secondary
                                                              : p.primary;
        const uint32_t id = HashId(kKindGroup, std::wstring(L"group") + GroupName(group));
        const std::wstring subtitle = names.empty() ? std::wstring(T(Text::GroupEmpty)) : names;
        // Each group carries its own volume, with what plays in it shown inside the slider.
        rows.push_back({compact ? kGroupCompactRowHeight : kGroupVolumeRowHeight,
                        [this, &p, group, id, color, subtitle, compact, namesLeft, percentRight,
                         namesRight](float top) {
                            const bool chat = group == AppGroup::Chat;
                            const bool media = group == AppGroup::Media;
                            const bool aux = group == AppGroup::Aux;
                            const int volume = aux     ? settings_.auxVolume
                                               : media ? settings_.mediaVolume
                                               : chat  ? settings_.chatVolume
                                                       : settings_.gameVolume;
                            const float lineHeight = compact ? kGroupCompactLineHeight : design::kTwoLineRowHeight;
                            if (compact) {
                                const float middle = top + lineHeight / 2.0f;
                                canvas_.Glyph(GroupGlyph(group), {design::kLeadingIconCenter, middle},
                                              design::kIconSize - 2.0f, color);
                                canvas_.Text(GroupName(group), {design::kLeadingTextInset, middle - 11.0f, namesLeft,
                                                                middle + 11.0f},
                                             TextStyle::BodyLarge, p.onSurface);
                                // One DIP lower, so the smaller text shares the name's baseline.
                                canvas_.Text(subtitle, {namesLeft, middle - 9.0f, namesRight, middle + 11.0f},
                                             TextStyle::BodySmall, p.onSurfaceVariant);
                                canvas_.Glyph(kGlyphChevronRight, {design::kTrailingIconCenter, middle},
                                              design::kSmallIconSize, p.onSurfaceVariant);
                                AddHit({TileRect(top, lineHeight), id, [this, group] { OpenGroup(group); }});
                            } else {
                                DrawNavRow(id, top, GroupGlyph(group), color, GroupName(group), subtitle,
                                           [this, group] { OpenGroup(group); }, kGroupPercentWidth + design::kSpace3);
                            }
                            canvas_.Text(Percent(volume),
                                         {percentRight - kGroupPercentWidth, top, percentRight, top + lineHeight},
                                         TextStyle::LabelLarge, p.onSurfaceVariant, TextAlign::Trailing);
                            DrawVolumeSlider(aux     ? kIdAuxVolume
                                             : media ? kIdMediaVolume
                                             : chat  ? kIdChatVolume
                                                     : kIdGameVolume,
                                             design::kLeadingTextInset, kWidth - kInset,
                                             top + (compact ? kGroupCompactSliderY : kGroupSliderY), volume,
                                             aux     ? meters_.aux
                                             : media ? meters_.media
                                             : chat  ? meters_.chat
                                                     : meters_.game,
                                             color,
                                             media || aux ? p.secondaryContainer
                                             : chat       ? p.tertiaryContainer
                                                          : p.primaryContainer,
                                             [this, group](int value) {
                                                 Edit([group, value](Settings& s) {
                                                     (group == AppGroup::Aux     ? s.auxVolume
                                                      : group == AppGroup::Media ? s.mediaVolume
                                                      : group == AppGroup::Chat  ? s.chatVolume
                                                                                 : s.gameVolume) = value;
                                                 });
                                             });
                        },
                        id});
    }
    y = DrawCard(y, rows);

    if (routing_.enabled) {
        y = DrawCard(y + design::kGroupGap,
                     {NavItem(kIdRoutingStatus, kGlyphSpeakers, routing_.error ? p.error : p.primary,
                              T(Text::RoutingMode), routing_.status, [this] { GoTo(Page::Audio); })});
    }
    EndScroll(Page::Mixer, scrollTop, y);
}

void Panel::DrawMixCard(const D2D1_RECT_F& card) {
    const Palette& p = P();
    canvas_.FillRound(card, design::kRadiusGroup, Alpha(p.surfaceContainerHigh, p.tileAlpha));
    Hit wheel;
    wheel.rect = card;
    wheel.id = kIdMixCard;
    wheel.wheel = [this](int notches) { NudgeBalance(notches, kCoarseSteps); };
    AddHit(std::move(wheel));

    const float left = card.left + design::kItemPadding + design::kSpace1;
    const float right = card.right - design::kItemPadding - design::kSpace1;
    canvas_.Glyph(gameMuted_ ? kGlyphMute : kGlyphGame, {left + 8.0f, card.top + 23.0f}, 16.0f,
                  gameMuted_ ? p.error : p.primary);
    canvas_.Text(gameMuted_ ? T(Text::GameMuted) : T(Text::Game),
                 {left + 24.0f, card.top + 13.0f, left + 160.0f, card.top + 33.0f}, TextStyle::LabelLarge,
                 gameMuted_ ? p.error : p.onSurfaceVariant);
    canvas_.Glyph(chatMuted_ ? kGlyphMute : kGlyphChat, {right - 8.0f, card.top + 23.0f}, 16.0f,
                  chatMuted_ ? p.error : p.tertiary);
    canvas_.Text(chatMuted_ ? T(Text::ChatMuted) : T(Text::Chat),
                 {right - 160.0f, card.top + 13.0f, right - 24.0f, card.top + 33.0f}, TextStyle::LabelLarge,
                 chatMuted_ ? p.error : p.onSurfaceVariant, TextAlign::Trailing);
    canvas_.Text(Percent(levels_.game), {left, card.top + 32.0f, left + 160.0f, card.top + 72.0f}, TextStyle::Number,
                 gameMuted_ ? Alpha(p.primary, 0.4f) : p.primary);
    canvas_.Text(Percent(levels_.chat), {right - 160.0f, card.top + 32.0f, right, card.top + 72.0f},
                 TextStyle::Number, chatMuted_ ? Alpha(p.tertiary, 0.4f) : p.tertiary, TextAlign::Trailing);
    // The even balance gets its own mark between the two levels.
    const bool balanced = BalanceFromLevels(levels_) == 0 && !chatMuted_ && !gameMuted_;
    DrawBalancedChip(canvas_, p, {(left + right) / 2.0f, card.top + 54.0f}, T(Text::Balanced),
                     Animate(AnimKey(kIdMixCard, 3), balanced ? 1.0f : 0.0f, design::kSpringSlowEffects));
    DrawSlider(left, right, card.top + 100.0f);
    canvas_.Text(HotkeyHint(), {card.left + 16.0f, card.bottom - 26.0f, card.right - 16.0f, card.bottom - 8.0f},
                 TextStyle::BodySmall, p.onSurfaceVariant, TextAlign::Center);
}

void Panel::NudgeBalance(int direction, int steps) {
    const int grid = balanceDial_ ? kBalanceSteps : steps;
    const int balance = BalanceFromLevels(levels_);
    const int target = StepBalance(balance, direction, grid);
    if (target != balance) {
        host_.OnBalanceChanged(target);
    }
}

void Panel::DrawSlider(float left, float right, float centerY) {
    constexpr SliderStyle kStyle{16.0f, 6.0f, 44.0f};
    const float position =
        Animate(AnimKey(kIdSlider, 0), static_cast<float>(BalanceFromLevels(levels_)), design::kSpringDefaultSpatial);
    const float handleWidth =
        Animate(AnimKey(kIdSlider, 1), pressedId_ == kIdSlider ? 2.0f : 4.0f, design::kSpringFastSpatial);
    DrawBalanceSlider(canvas_, P(), left, right, centerY, position, handleWidth, kStyle);

    Hit hit;
    hit.rect = {left - 12.0f, centerY - kStyle.handleHeight / 2.0f, right + 12.0f,
                centerY + kStyle.handleHeight / 2.0f};
    hit.id = kIdSlider;
    hit.drag = [this, left, right](float x, float) {
        // Every percent without a headset dial; with one, the dial's positions only.
        const float t = std::clamp((x - left) / (right - left), 0.0f, 1.0f);
        int target = static_cast<int>(std::lround((t * 2.0f - 1.0f) * kBalanceRange));
        if (balanceDial_) {
            target = SnapBalance(target, kBalanceSteps);
        }
        if (target != BalanceFromLevels(levels_)) {
            host_.OnBalanceChanged(target);
        }
    };
    hit.doubleClick = [this] { host_.OnBalanceChanged(0); };
    AddHit(std::move(hit));
}

void Panel::DrawGroup() {
    const Palette& p = P();
    constexpr float kAddWidth = 112.0f;
    std::wstring subtitle;
    if (routing_.enabled && (group_ == AppGroup::Game || group_ == AppGroup::Chat)) {
        const std::wstring& device = routing_.names[group_ == AppGroup::Chat ? 1 : 0];
        const std::wstring playsOn = std::wstring(T(Text::PlaysOn)) + L" ";
        subtitle = playsOn + (device.empty() ? T(Text::NotSelected) : device.c_str());
        // A device name ends with its driver's in parentheses; without room for it the endpoint's own name
        // stays, and the Audio page shows the whole.
        const float room = kWidth - 2.0f * kGutter - design::kIconButtonSize - design::kSpace1 - kAddWidth;
        const size_t driver = device.rfind(L" (");
        if (canvas_.Measure(subtitle, TextStyle::BodySmall) > room && driver != std::wstring::npos && driver > 0 &&
            device.back() == L')') {
            subtitle = playsOn + device.substr(0, driver);
        }
    }
    DrawHeader(GroupName(group_), subtitle.c_str(), kAddWidth);
    const float buttonTop = (kHeaderHeight - design::kButtonHeight) / 2.0f;
    TextButton(kIdGroupAdd, {kWidth - kGutter - 104.0f, buttonTop, kWidth - kGutter, buttonTop + design::kButtonHeight},
               kGlyphAdd, T(Text::Add), true, [this] { OpenAddApp(AddTarget::Group); });

    const Text hint = group_ == AppGroup::Chat    ? Text::GroupChatHint
                      : group_ == AppGroup::Media ? Text::GroupMediaHint
                      : group_ == AppGroup::Aux   ? Text::GroupAuxHint
                                                  : Text::GroupGameHint;
    const float listTop = PageHint(T(hint), kHeaderHeight);

    const std::vector<AppItem> members = AppsInGroup(group_);
    const D2D1_RECT_F list{kGutter, listTop, kWidth - kGutter, ContentBottom() - design::kGroupGap};
    if (members.empty()) {
        DrawCard(listTop, {HintItem(T(Text::AppsEmpty))});
        return;
    }
    DrawList(
        list, kIdGroupList, groupScroll_, members.size(), kAppRowHeight, [](size_t) { return 0u; },
        [&](size_t index, float top) { DrawAppRow(members[index], top); });
    (void)p;
}

void Panel::DrawAppIcon(const AppItem& app, const D2D1_RECT_F& rect) {
    if (!app.path.empty()) {
        if (ID2D1Bitmap* bitmap = IconFor(app.path)) {
            canvas_.Bitmap(bitmap, rect);
            return;
        }
    }
    const D2D1_POINT_2F center{(rect.left + rect.right) / 2.0f, (rect.top + rect.bottom) / 2.0f};
    canvas_.FillCircle(center, (rect.right - rect.left) / 2.0f, P().secondaryContainer);
    const wchar_t initial = app.name.empty() ? L'?' : static_cast<wchar_t>(std::towupper(app.name[0]));
    canvas_.Text(std::wstring(1, initial), rect, TextStyle::TitleMedium, P().onSecondaryContainer, TextAlign::Center);
}

void Panel::DrawAppRow(const AppItem& app, float top) {
    const Palette& p = P();
    const std::vector<AppGroup> groups = AvailableGroups();
    // Four groups take narrower segments, so the app's name keeps some room.
    const float segmentWidth = groups.size() > 3 ? 46.0f : 52.0f;
    const float middle = top + kAppRowHeight / 2.0f;
    DrawAppIcon(app, {kInset, middle - 16.0f, kInset + 32.0f, middle + 16.0f});

    // Every app can be removed: from Chat, Media or Aux it goes back to Game, from Game out of ChatMix.
    const bool removable = true;
    const float removeWidth = removable ? design::kSmallIconButtonSize + design::kSpace1 : 0.0f;
    if (removable) {
        const float centerX = kWidth - kInset - design::kSmallIconButtonSize / 2.0f + design::kSpace1;
        IconButton(HashId(kKindAppRemove, app.exe), {centerX, middle}, kGlyphDelete, p.onSurfaceVariant,
                   [this, app] { host_.OnAppRemoved(app); });
        Tooltip(HashId(kKindAppRemove, app.exe),
                T(app.group == AppGroup::Game ? Text::TipLeaveOutOfChatMix : Text::TipRemoveFromGroup));
    }
    const float segmentsRight = kWidth - kInset + design::kSpace1 - removeWidth;
    const D2D1_RECT_F segments{segmentsRight - groups.size() * segmentWidth, middle - design::kControlHeight / 2.0f,
                               segmentsRight, middle + design::kControlHeight / 2.0f};
    const float textLeft = kInset + 32.0f + design::kSpace3;
    const float textRight = segments.left - design::kSpace2;
    canvas_.Text(app.name, {textLeft, middle - 19.0f, textRight, middle + 1.0f}, TextStyle::BodyLarge,
                 app.running ? p.onSurface : p.onSurfaceVariant);
    std::wstring detail = app.exe;
    if (!app.running) {
        // Without room for both, the state stays: the name above already tells the app.
        detail = std::wstring(T(Text::NotRunning)) + L" · " + app.exe;
        if (canvas_.Measure(detail, TextStyle::BodySmall) > textRight - textLeft) {
            detail = T(Text::NotRunning);
        }
    }
    canvas_.Text(detail, {textLeft, middle + 1.0f, textRight, middle + 18.0f}, TextStyle::BodySmall,
                 p.onSurfaceVariant);

    const AppGroup effective = ShownGroup(settings_, app.group);
    const int selected = static_cast<int>(std::find(groups.begin(), groups.end(), effective) - groups.begin());
    const bool aside = effective == AppGroup::Media || effective == AppGroup::Aux;
    const D2D1_COLOR_F fill = effective == AppGroup::Chat ? p.tertiary : aside ? p.secondaryContainer : p.primary;
    const D2D1_COLOR_F onFill = effective == AppGroup::Chat ? p.onTertiary : aside ? p.onSecondaryContainer : p.onPrimary;
    std::vector<const wchar_t*> labels;
    for (const AppGroup group : groups) {
        labels.push_back(GroupName(group));
    }
    DrawSegmented(segments, HashId(kKindGroup, app.exe), labels, selected, fill, onFill,
                  [this, app, groups](int index) {
                      if (groups[index] != app.group) {
                          host_.OnGroupChanged(app, groups[index]);
                      }
                  });
}

void Panel::DrawAddApp() {
    const Palette& p = P();
    const bool profile = addTarget_ == AddTarget::Profile;
    DrawHeader(profile ? T(Text::AddGame) : T(Text::AddApp));

    // Search field: a fully rounded bar, as Material 3 search bars are.
    const D2D1_RECT_F search{kGutter, kHeaderHeight, kWidth - kGutter, kHeaderHeight + kRowHeight};
    canvas_.FillRound(search, kRowHeight / 2.0f, Alpha(p.surfaceContainerHigh, p.tileAlpha));
    const float middle = (search.top + search.bottom) / 2.0f;
    canvas_.Glyph(kGlyphSearch, {design::kLeadingIconCenter, middle}, 16.0f, p.onSurfaceVariant);
    const D2D1_RECT_F field{design::kLeadingTextInset, search.top, kWidth - kInset, search.bottom};
    float caret = field.left - 2.0f;
    if (filter_.empty()) {
        canvas_.Text(T(Text::SearchApps), Inset(field, 2.0f, 0.0f), TextStyle::BodyLarge, p.onSurfaceVariant);
    } else {
        canvas_.Text(filter_, field, TextStyle::BodyLarge, p.onSurface);
        caret = std::min(field.right, field.left + canvas_.Measure(filter_, TextStyle::BodyLarge) + 1.0f);
    }
    canvas_.FillRect({caret, middle - 10.0f, caret + 1.5f, middle + 10.0f}, p.primary);

    float top = search.bottom + design::kGroupGap;
    if (!profile) {
        const std::vector<AppGroup> groups = AvailableGroups();
        const float labelRight = kInset + canvas_.Measure(T(Text::AddTo), TextStyle::BodyMedium);
        canvas_.Text(T(Text::AddTo), {kInset, top, labelRight + 1.0f, top + design::kControlHeight},
                     TextStyle::BodyMedium, p.onSurfaceVariant);
        std::vector<const wchar_t*> labels;
        for (const AppGroup group : groups) {
            labels.push_back(GroupName(group));
        }
        const int selected = static_cast<int>(std::find(groups.begin(), groups.end(), addGroup_) - groups.begin());
        const bool aside = addGroup_ == AppGroup::Media || addGroup_ == AppGroup::Aux;
        const D2D1_COLOR_F fill = addGroup_ == AppGroup::Chat ? p.tertiary : aside ? p.secondaryContainer : p.primary;
        const D2D1_COLOR_F onFill = addGroup_ == AppGroup::Chat ? p.onTertiary
                                    : aside                     ? p.onSecondaryContainer
                                                                : p.onPrimary;
        const float groupsLeft = labelRight + design::kSpace3;
        DrawSegmented({groupsLeft, top, std::min(kWidth - kInset, groupsLeft + 84.0f * groups.size()),
                       top + design::kControlHeight},
                      kIdAddGroup, labels, selected, fill, onFill, [this, groups](int index) {
                          addGroup_ = groups[index];
                          Invalidate();
                      });
        top += design::kControlHeight + design::kGroupGap;
    }
    // Apps that are not running are picked by file under the list.
    const float bottom = ContentBottom() - design::kGroupGap;
    TextButton(kIdBrowse, {kGutter, bottom - design::kButtonHeight, kWidth - kGutter, bottom}, kGlyphFolder,
               T(Text::BrowseExe), true, [this] { RunFileDialog(FileDialog::BrowseExe); });

    const std::vector<AppItem> items = FilteredRunningApps();
    const D2D1_RECT_F list{kGutter, top, kWidth - kGutter, bottom - design::kButtonHeight - design::kGroupGap};
    if (items.empty()) {
        DrawCard(top, {HintItem(T(filter_.empty() ? Text::NoRunningApps : Text::NothingFound))});
        return;
    }
    DrawList(
        list, kIdRunningList, addScroll_, items.size(), kAppRowHeight,
        [&](size_t index) { return HashId(kKindRunning, items[index].exe); },
        [&](size_t index, float rowTop) { DrawRunningRow(items[index], rowTop); });
}

void Panel::DrawRunningRow(const AppItem& app, float top) {
    const Palette& p = P();
    const bool profile = addTarget_ == AddTarget::Profile;
    const uint32_t id = HashId(kKindRunning, app.exe);
    const float middle = top + kAppRowHeight / 2.0f;
    DrawAppIcon(app, {kInset, middle - 16.0f, kInset + 32.0f, middle + 16.0f});
    const float textLeft = kInset + 32.0f + design::kSpace3;
    const float textRight = design::kTrailingIconCenter - design::kSpace4;
    canvas_.Text(app.name, {textLeft, middle - 19.0f, textRight, middle + 1.0f}, TextStyle::BodyLarge, p.onSurface);
    std::wstring detail = app.exe;
    if (!profile && app.ignored) {
        detail += std::wstring(L" · ") + T(Text::OutOfChatMix);
    } else if (!profile && app.group != AppGroup::Game) {
        detail += std::wstring(L" · ") + GroupName(app.group);
    }
    canvas_.Text(detail, {textLeft, middle + 1.0f, textRight, middle + 18.0f}, TextStyle::BodySmall,
                 p.onSurfaceVariant);
    const bool added = profile ? std::any_of(settings_.profiles.begin(), settings_.profiles.end(),
                                             [&](const GameProfile& item) {
                                                 return _wcsicmp(item.exe.c_str(), app.exe.c_str()) == 0;
                                             })
                               : !app.ignored && app.group == addGroup_;
    canvas_.Glyph(added ? kGlyphCheck : kGlyphAdd, {design::kTrailingIconCenter, middle}, 16.0f,
                  added ? p.primary : p.onSurfaceVariant);
    AddHit({TileRect(top, kAppRowHeight), id, [this, app, profile] {
                if (profile) {
                    Edit([app](Settings& s) {
                        const bool known = std::any_of(
                            s.profiles.begin(), s.profiles.end(),
                            [&](const GameProfile& item) { return _wcsicmp(item.exe.c_str(), app.exe.c_str()) == 0; });
                        if (!known) {
                            s.profiles.push_back({app.exe, app.path});
                        }
                    });
                    profileExe_ = app.exe;
                    GoTo(Page::ProfileEdit);
                } else {
                    host_.OnGroupChanged(app, addGroup_);
                    group_ = addGroup_;
                    GoTo(Page::Group);
                }
            }});
}

}  // namespace ui
