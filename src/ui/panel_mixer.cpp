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

constexpr float kGroupRowHeight = 64.0f;

const wchar_t* GroupName(AppGroup group) {
    switch (group) {
    case AppGroup::Chat: return T(Text::Chat);
    case AppGroup::Media: return T(Text::Media);
    case AppGroup::Game: break;
    }
    return T(Text::Game);
}

wchar_t GroupGlyph(AppGroup group) {
    switch (group) {
    case AppGroup::Chat: return kGlyphChat;
    case AppGroup::Media: return kGlyphVolume;
    case AppGroup::Game: break;
    }
    return kGlyphGame;
}

}  // namespace

std::vector<AppGroup> Panel::AvailableGroups() const {
    std::vector<AppGroup> groups{AppGroup::Game, AppGroup::Chat};
    if (settings_.MediaAvailable()) {
        groups.push_back(AppGroup::Media);
    }
    return groups;
}

std::vector<AppItem> Panel::AppsInGroup(AppGroup group) const {
    std::vector<AppItem> result;
    for (const auto& app : apps_) {
        const AppGroup effective = app.group == AppGroup::Media && !settings_.MediaAvailable() ? AppGroup::Game : app.group;
        if (effective == group) {
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
    const float gameLevel = Animate(AnimKey(kIdMark, 0), levels_.game / 100.0f);
    const float chatLevel = Animate(AnimKey(kIdMark, 1), chatMuted_ ? 0.0f : levels_.chat / 100.0f);
    auto pill = [&](float x, float level, D2D1_COLOR_F color) {
        const float height = 8.0f + 20.0f * level;
        canvas_.FillRound({x, 46.0f - height, x + 8.0f, 46.0f}, 4.0f, color);
    };
    pill(20.0f, gameLevel, p.primary);
    pill(32.0f, chatLevel, p.tertiary);
    canvas_.Text(kAppName, {52.0f, 13.0f, kWidth - 20.0f, 33.0f}, TextStyle::TitleMedium, p.onSurface);
    canvas_.FillCircle({55.0f, 42.0f}, 3.0f, deviceConnected_ ? p.primary : p.outline);
    std::wstring status = deviceDetail_.empty() ? device_ : device_ + L" · " + deviceDetail_;
    if (headset_.battery >= 0 && !headset_.headsetOff) {
        status += L" · " + Percent(headset_.battery);
    }
    canvas_.Text(status, {63.0f, 33.0f, kWidth - 20.0f, 51.0f}, TextStyle::BodySmall, p.onSurfaceVariant);

    DrawMixCard({16.0f, 64.0f, kWidth - 16.0f, 212.0f});

    float y = SectionTitle(T(Text::Groups), 224.0f);
    const std::vector<AppGroup> groups = AvailableGroups();
    const D2D1_RECT_F card = Card(y, 2.0f * kListPadding + groups.size() * kGroupRowHeight);
    float top = card.top + kListPadding;
    for (const AppGroup group : groups) {
        const std::vector<AppItem> members = AppsInGroup(group);
        std::wstring names;
        int listed = 0;
        for (const auto& app : members) {
            if (listed == 3) {
                names += L" +" + std::to_wstring(members.size() - listed);
                break;
            }
            names += (names.empty() ? L"" : L", ") + app.name;
            ++listed;
        }
        const D2D1_COLOR_F color = group == AppGroup::Chat ? p.tertiary : group == AppGroup::Media ? p.onSurfaceVariant : p.primary;
        const uint32_t id = HashId(kKindGroup, std::wstring(L"group") + GroupName(group));
        top = DrawNavRow(id, top, GroupGlyph(group), color, GroupName(group), names.empty() ? T(Text::GroupEmpty) : names,
                         [this, group] { OpenGroup(group); });
    }
    y = card.bottom + 12.0f;

    if (routing_.enabled) {
        const D2D1_RECT_F routingCard{16.0f, y, kWidth - 16.0f, y + 64.0f + 2.0f * kListPadding};
        canvas_.FillRound(routingCard, kCardRadius, Alpha(p.surfaceContainer, kCardAlpha));
        DrawNavRow(kIdRoutingStatus, routingCard.top + kListPadding, kGlyphSpeakers, routing_.error ? p.error : p.primary,
                   T(Text::RoutingMode), routing_.status, [this] { GoTo(Page::Audio); });
    }
}

void Panel::DrawMixCard(const D2D1_RECT_F& card) {
    const Palette& p = P();
    canvas_.FillRound(card, kCardRadius, Alpha(p.surfaceContainerHigh, kCardAlpha));
    Hit wheel;
    wheel.rect = card;
    wheel.id = kIdMixCard;
    wheel.wheel = [this](int notches) { NudgeBalance(notches, kCoarseSteps); };
    AddHit(std::move(wheel));

    const float left = card.left + 20.0f;
    const float right = card.right - 20.0f;
    canvas_.Glyph(kGlyphGame, {left + 8.0f, card.top + 23.0f}, 16.0f, p.primary);
    canvas_.Text(T(Text::Game), {left + 24.0f, card.top + 13.0f, left + 140.0f, card.top + 33.0f},
                 TextStyle::LabelLarge, p.onSurfaceVariant);
    canvas_.Glyph(chatMuted_ ? kGlyphMute : kGlyphChat, {right - 8.0f, card.top + 23.0f}, 16.0f,
                  chatMuted_ ? p.error : p.tertiary);
    canvas_.Text(chatMuted_ ? T(Text::ChatMuted) : T(Text::Chat),
                 {right - 160.0f, card.top + 13.0f, right - 24.0f, card.top + 33.0f}, TextStyle::LabelLarge,
                 chatMuted_ ? p.error : p.onSurfaceVariant, TextAlign::Trailing);
    canvas_.Text(Percent(levels_.game), {left, card.top + 32.0f, left + 160.0f, card.top + 72.0f}, TextStyle::Number,
                 p.primary);
    canvas_.Text(Percent(levels_.chat), {right - 160.0f, card.top + 32.0f, right, card.top + 72.0f},
                 TextStyle::Number, chatMuted_ ? Alpha(p.tertiary, 0.4f) : p.tertiary, TextAlign::Trailing);
    // The even balance gets its own mark between the two levels.
    const bool balanced = BalanceFromLevels(levels_) == 0 && !chatMuted_;
    DrawBalancedChip(canvas_, p, {(left + right) / 2.0f, card.top + 54.0f}, T(Text::Balanced),
                     Animate(AnimKey(kIdMixCard, 3), balanced ? 1.0f : 0.0f, 16.0f));
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
    const float position = Animate(AnimKey(kIdSlider, 0), static_cast<float>(BalanceFromLevels(levels_)), 20.0f);
    const float handleWidth = Animate(AnimKey(kIdSlider, 1), pressedId_ == kIdSlider ? 2.0f : 4.0f, 24.0f);
    DrawBalanceSlider(canvas_, P(), left, right, centerY, position, handleWidth, kStyle);

    Hit hit;
    hit.rect = {left - 12.0f, centerY - kStyle.handleHeight / 2.0f, right + 12.0f, centerY + kStyle.handleHeight / 2.0f};
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
    std::wstring subtitle;
    if (routing_.enabled && group_ != AppGroup::Media) {
        const std::wstring& device = routing_.names[group_ == AppGroup::Chat ? 1 : 0];
        subtitle = std::wstring(T(Text::PlaysOn)) + L" " + (device.empty() ? T(Text::NotSelected) : device.c_str());
    }
    DrawHeader(GroupName(group_), subtitle.c_str(), 112.0f);
    TextButton(kIdGroupAdd, {kWidth - 16.0f - 104.0f, 16.0f, kWidth - 16.0f, 48.0f}, kGlyphAdd, T(Text::Add), true,
               [this] { OpenAddApp(AddTarget::Group); });

    const Text hint = group_ == AppGroup::Chat ? Text::GroupChatHint : group_ == AppGroup::Media ? Text::GroupMediaHint
                                                                                                 : Text::GroupGameHint;
    const float hintHeight = HintHeight(T(hint), kWidth - 48.0f);
    canvas_.Text(T(hint), {24.0f, 62.0f, kWidth - 24.0f, 62.0f + hintHeight}, TextStyle::BodySmall, p.onSurfaceVariant,
                 TextAlign::Leading, true);

    const std::vector<AppItem> members = AppsInGroup(group_);
    const D2D1_RECT_F list{16.0f, 62.0f + hintHeight, kWidth - 16.0f, ContentBottom() - 12.0f};
    if (members.empty()) {
        canvas_.FillRound(list, kCardRadius, Alpha(p.surfaceContainer, kCardAlpha));
        canvas_.Text(T(Text::AppsEmpty), Inset(list, 32.0f, 0.0f), TextStyle::BodyMedium, p.onSurfaceVariant,
                     TextAlign::Center, true);
        return;
    }
    DrawList(list, kIdGroupList, groupScroll_, members.size(),
             [&](size_t index, float top) { DrawAppRow(members[index], top); });
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
    constexpr float kSegmentWidth = 50.0f;
    const float left = 20.0f;
    const float right = kWidth - 20.0f;
    DrawAppIcon(app, {left + 12.0f, top + 12.0f, left + 44.0f, top + 44.0f});

    const D2D1_RECT_F segments{right - 12.0f - groups.size() * kSegmentWidth, top + 12.0f, right - 12.0f, top + 44.0f};
    canvas_.Text(app.name, {left + 56.0f, top + 8.0f, segments.left - 10.0f, top + 29.0f}, TextStyle::BodyLarge,
                 app.running ? p.onSurface : p.onSurfaceVariant);
    const std::wstring detail = app.running ? app.exe : std::wstring(T(Text::NotRunning)) + L" · " + app.exe;
    canvas_.Text(detail, {left + 56.0f, top + 29.0f, segments.left - 10.0f, top + 47.0f}, TextStyle::BodySmall,
                 p.onSurfaceVariant);

    const AppGroup effective = app.group == AppGroup::Media && !settings_.MediaAvailable() ? AppGroup::Game : app.group;
    const int selected = static_cast<int>(std::find(groups.begin(), groups.end(), effective) - groups.begin());
    const D2D1_COLOR_F fill = effective == AppGroup::Chat ? p.tertiaryContainer
                              : effective == AppGroup::Media ? p.secondaryContainer
                                                             : p.primaryContainer;
    const D2D1_COLOR_F onFill = effective == AppGroup::Chat ? p.onTertiaryContainer
                                : effective == AppGroup::Media ? p.onSecondaryContainer
                                                               : p.onPrimaryContainer;
    std::vector<const wchar_t*> labels;
    for (const AppGroup group : groups) {
        labels.push_back(GroupName(group));
    }
    DrawSegmented(segments, HashId(kKindGroup, app.exe), labels, selected, fill, onFill, [this, app, groups](int index) {
        if (groups[index] != app.group) {
            host_.OnGroupChanged(app, groups[index]);
        }
    });
}

void Panel::DrawAddApp() {
    const Palette& p = P();
    const bool profile = addTarget_ == AddTarget::Profile;
    DrawHeader(profile ? T(Text::AddGame) : T(Text::AddApp));

    const D2D1_RECT_F search{16.0f, 64.0f, kWidth - 16.0f, 112.0f};
    canvas_.FillRound(search, 24.0f, Alpha(p.surfaceContainerHigh, kCardAlpha));
    canvas_.Glyph(kGlyphSearch, {44.0f, 88.0f}, 16.0f, p.onSurfaceVariant);
    const D2D1_RECT_F field{64.0f, 64.0f, kWidth - 36.0f, 112.0f};
    float caret = field.left - 2.0f;
    if (filter_.empty()) {
        canvas_.Text(T(Text::SearchApps), Inset(field, 2.0f, 0.0f), TextStyle::BodyLarge, p.onSurfaceVariant);
    } else {
        canvas_.Text(filter_, field, TextStyle::BodyLarge, p.onSurface);
        caret = std::min(field.right, field.left + canvas_.Measure(filter_, TextStyle::BodyLarge) + 1.0f);
    }
    canvas_.FillRect({caret, 78.0f, caret + 1.5f, 98.0f}, p.primary);

    if (!profile) {
        const std::vector<AppGroup> groups = AvailableGroups();
        canvas_.Text(T(Text::AddTo), {20.0f, 124.0f, kWidth / 2.0f, 156.0f}, TextStyle::BodyMedium, p.onSurfaceVariant);
        std::vector<const wchar_t*> labels;
        for (const AppGroup group : groups) {
            labels.push_back(GroupName(group));
        }
        const int selected = static_cast<int>(std::find(groups.begin(), groups.end(), addGroup_) - groups.begin());
        const D2D1_COLOR_F fill = addGroup_ == AppGroup::Chat ? p.tertiaryContainer
                                  : addGroup_ == AppGroup::Media ? p.secondaryContainer
                                                                 : p.primaryContainer;
        const D2D1_COLOR_F onFill = addGroup_ == AppGroup::Chat ? p.onTertiaryContainer
                                    : addGroup_ == AppGroup::Media ? p.onSecondaryContainer
                                                                   : p.onPrimaryContainer;
        const float labelRight = 28.0f + canvas_.Measure(T(Text::AddTo), TextStyle::BodyMedium);
        DrawSegmented({labelRight + 8.0f, 124.0f, std::min(kWidth - 20.0f, labelRight + 8.0f + 80.0f * groups.size()), 156.0f},
                      kIdAddGroup, labels, selected, fill,
                      onFill, [this, groups](int index) {
                          addGroup_ = groups[index];
                          Invalidate();
                      });
    }
    // Apps that are not running are picked by file under the list.
    const float bottom = ContentBottom() - 12.0f;
    TextButton(kIdBrowse, {16.0f, bottom - 40.0f, kWidth - 16.0f, bottom}, kGlyphFolder, T(Text::BrowseExe), true,
               [this] { RunFileDialog(FileDialog::BrowseExe); });

    const std::vector<AppItem> items = FilteredRunningApps();
    const D2D1_RECT_F list{16.0f, profile ? 124.0f : 168.0f, kWidth - 16.0f, bottom - 52.0f};
    if (items.empty()) {
        canvas_.FillRound(list, kCardRadius, Alpha(p.surfaceContainer, kCardAlpha));
        canvas_.Text(T(filter_.empty() ? Text::NoRunningApps : Text::NothingFound), Inset(list, 24.0f, 0.0f),
                     TextStyle::BodyMedium, p.onSurfaceVariant, TextAlign::Center, true);
        return;
    }
    DrawList(list, kIdRunningList, addScroll_, items.size(), [&](size_t index, float top) { DrawRunningRow(items[index], top); });
}

void Panel::DrawRunningRow(const AppItem& app, float top) {
    const Palette& p = P();
    const bool profile = addTarget_ == AddTarget::Profile;
    const uint32_t id = HashId(kKindRunning, app.exe);
    const D2D1_RECT_F row{20.0f, top, kWidth - 20.0f, top + kRowHeight};
    StateLayer(id, row, 16.0f, p.onSurface);
    DrawAppIcon(app, {32.0f, top + 12.0f, 64.0f, top + 44.0f});
    canvas_.Text(app.name, {76.0f, top + 8.0f, kWidth - 64.0f, top + 29.0f}, TextStyle::BodyLarge, p.onSurface);
    std::wstring detail = app.exe;
    if (!profile && app.group != AppGroup::Game) {
        detail += std::wstring(L" · ") + GroupName(app.group);
    }
    canvas_.Text(detail, {76.0f, top + 29.0f, kWidth - 64.0f, top + 47.0f}, TextStyle::BodySmall, p.onSurfaceVariant);
    const bool added = profile ? std::any_of(settings_.profiles.begin(), settings_.profiles.end(),
                                             [&](const GameProfile& item) {
                                                 return _wcsicmp(item.exe.c_str(), app.exe.c_str()) == 0;
                                             })
                               : app.group == addGroup_;
    canvas_.Glyph(added ? kGlyphCheck : kGlyphAdd, {kWidth - 44.0f, top + 28.0f}, 16.0f,
                  added ? p.primary : p.onSurfaceVariant);
    AddHit({row, id, [this, app, profile] {
                if (profile) {
                    Edit([app](Settings& s) {
                        const bool known = std::any_of(s.profiles.begin(), s.profiles.end(), [&](const GameProfile& item) {
                            return _wcsicmp(item.exe.c_str(), app.exe.c_str()) == 0;
                        });
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
