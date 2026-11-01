// Third-party add-ons: programs that talk SteelSeries GameSense, run by OpenChatMix while SteelSeries GG is closed,
// and the screens their events put on the headset display.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "../addon_events.h"
#include "../addon_icons.h"
#include "../addon_options.h"
#include "../app.h"
#include "../app_info.h"
#include "../reorder.h"
#include "panel.h"
#include "panel_internal.h"

namespace ui {

using namespace detail;

namespace {

constexpr int kMinDisplayMs = 500;
constexpr int kMaxDisplayMs = 30000;
constexpr int kDisplayStepMs = 500;
constexpr int kMaxTimes = 10;
constexpr float kIconCell = 40.0f;       // A cell of the icon picker.
constexpr float kIconPixel = 3.0f;       // Size of an icon pixel in the picker.
constexpr float kRowIconPixel = 2.0f;    // Size of an icon pixel in lists.
constexpr float kFieldHeight = 36.0f;    // An editable text below its label.
constexpr float kChipHeight = 28.0f;
constexpr ULONGLONG kChoicesFreshMs = 10000;  // Choices a program listed are asked for again when older.

bool IsFile(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

Text FieldLabel(const std::wstring& field) {
    if (field == L"title") {
        return Text::FieldTitle;
    }
    if (field == L"message") {
        return Text::FieldMessage;
    }
    if (field == L"user") {
        return Text::FieldUser;
    }
    return Text::FieldChannel;
}

std::wstring Seconds(int ms) {
    wchar_t text[32];
    swprintf_s(text, L"%.1f %ls", ms / 1000.0, T(Text::SecondsShort));
    return text;
}

}  // namespace

void Panel::SetAddOns(const AddOnsView& addOns) {
    addOns_ = addOns;
    Invalidate();
}

AddOnSettings* Panel::EditedAddOn() {
    const auto found = std::find_if(settings_.addOns.begin(), settings_.addOns.end(), [&](const AddOnSettings& addOn) {
        return _wcsicmp(addOn.path.c_str(), addOnPath_.c_str()) == 0;
    });
    return found == settings_.addOns.end() ? nullptr : &*found;
}

void Panel::EditAddOn(std::function<void(AddOnSettings&)> change) {
    Edit([path = addOnPath_, change = std::move(change)](Settings& s) {
        for (AddOnSettings& item : s.addOns) {
            if (_wcsicmp(item.path.c_str(), path.c_str()) == 0) {
                change(item);
            }
        }
    });
}

void Panel::EditAddOnScreen(std::function<void(AddOnEventScreen&)> change) {
    EditAddOn([event = addOnEvent_, change = std::move(change)](AddOnSettings& item) {
        AddOnEventScreen screen = ScreenFor(item, event);
        change(screen);
        item.screens[event] = std::move(screen);
    });
}

void Panel::DrawPixelIcon(int icon, D2D1_POINT_2F center, float pixel, D2D1_COLOR_F color) {
    const float left = center.x - kAddOnIconSize * pixel / 2.0f;
    const float top = center.y - kAddOnIconSize * pixel / 2.0f;
    for (int y = 0; y < kAddOnIconSize; ++y) {
        for (int x = 0; x < kAddOnIconSize; ++x) {
            if (AddOnIconPixel(icon, x, y, 0)) {
                canvas_.FillRect({left + x * pixel, top + y * pixel, left + (x + 1) * pixel, top + (y + 1) * pixel},
                                 color);
            }
        }
    }
}

std::wstring Panel::AddOnStatus(size_t index) const {
    const AddOnSettings& addOn = settings_.addOns[index];
    if (!addOn.enabled) {
        return T(Text::OptOff);
    }
    if (!IsFile(addOn.path)) {
        return T(Text::AddOnMissing);
    }
    if (index < addOns_.running.size() && addOns_.running[index]) {
        return T(Text::AddOnRunning);
    }
    switch (addOns_.state) {
    case AddOnsView::State::GgRunning: return T(Text::AddOnsWaitGg);
    case AddOnsView::State::NoDisplay: return T(Text::AddOnsNoDisplay);
    default: return T(Text::AddOnNotRunning);
    }
}

void Panel::DrawAddOns() {
    const Palette& p = P();
    DrawHeader(T(Text::AddOns), nullptr, 104.0f);
    const float buttonTop = (kHeaderHeight - design::kButtonHeight) / 2.0f;
    TextButton(kIdAddAddOn,
               {kWidth - kGutter - 104.0f, buttonTop, kWidth - kGutter, buttonTop + design::kButtonHeight}, kGlyphAdd,
               T(Text::Add), true, [this] { RunFileDialog(FileDialog::AddOn); });
    float y = BeginScroll(Page::AddOns, kHeaderHeight);
    y = PageHint(T(Text::AddOnsHint), y);
    if (!settings_.addOns.empty()) {
        if (addOns_.state == AddOnsView::State::GgRunning) {
            y = PageHint(T(Text::AddOnsGgHint), y, p.primary);
        } else if (addOns_.state == AddOnsView::State::NoDisplay) {
            y = PageHint(T(Text::AddOnsNoDisplayHint), y, p.primary);
        }
    }
    std::vector<CardRow> rows;
    for (size_t index = 0; index < settings_.addOns.size(); ++index) {
        const AddOnSettings& addOn = settings_.addOns[index];
        const uint32_t id = HashId(kKindAddOn, addOn.path);
        rows.push_back({design::kTwoLineRowHeight,
                        [this, &p, index, id, path = addOn.path](float top) {
                            const AppItem app{FileNameOf(path), path, host_.AppName(path)};
                            const float iconTop = top + (design::kTwoLineRowHeight - 32.0f) / 2.0f;
                            DrawAppIcon(app, {kInset, iconTop, kInset + 32.0f, iconTop + 32.0f});
                            const float textLeft = kInset + 32.0f + design::kSpace3;
                            const float textRight = design::kTrailingIconCenter - design::kSpace4;
                            canvas_.Text(app.name, {textLeft, top + 9.0f, textRight, top + 31.0f}, TextStyle::BodyLarge,
                                         p.onSurface);
                            canvas_.Text(AddOnStatus(index), {textLeft, top + 31.0f, textRight, top + 50.0f},
                                         TextStyle::BodySmall, p.onSurfaceVariant);
                            canvas_.Glyph(kGlyphChevronRight,
                                          {design::kTrailingIconCenter, top + design::kTwoLineRowHeight / 2.0f},
                                          design::kSmallIconSize, p.onSurfaceVariant);
                            AddHit({TileRect(top, design::kTwoLineRowHeight), id, [this, path] {
                                        addOnPath_ = path;
                                        GoTo(Page::AddOnEdit);
                                    }});
                        },
                        id});
    }
    if (!rows.empty()) {
        y = DrawCard(y, rows);
    }
    EndScroll(Page::AddOns, kHeaderHeight, y);
}

void Panel::DrawAddOnEdit() {
    const Palette& p = P();
    AddOnSettings* addOn = EditedAddOn();
    if (!addOn) {
        DrawHeader(T(Text::AddOns));
        return;
    }
    const std::wstring path = addOn->path;
    const std::wstring name = host_.AppName(path);
    const std::wstring file = FileNameOf(path);
    // The file name below only when the program has a name of its own.
    const std::wstring stem = file.substr(0, file.find_last_of(L'.'));
    const bool named = _wcsicmp(name.c_str(), file.c_str()) != 0 && _wcsicmp(name.c_str(), stem.c_str()) != 0;
    DrawHeader(name.c_str(), named ? file.c_str() : nullptr);
    float y = BeginScroll(Page::AddOnEdit, kHeaderHeight);
    y = DrawCard(y, {SwitchItem(kIdAddOnEnabled, T(Text::AddOnEnabled), addOn->enabled,
                                [this] { EditAddOn([](AddOnSettings& item) { item.enabled = !item.enabled; }); },
                                T(Text::AddOnEnabledHint))}) +
        design::kSectionGap;

    if (AddOnKindOf(path) == AddOnKind::Discord) {
        y = SectionTitle(T(Text::AddOnEvents), y);
        y = PageHint(T(Text::AddOnEventsHint), y);
        std::vector<CardRow> rows;
        for (const AddOnEventSpec& spec : DiscordEvents()) {
            const uint32_t id = HashId(kKindAddOn, spec.event);
            rows.push_back({design::kTwoLineRowHeight,
                            [this, &p, addOn, &spec, id](float top) {
                                const AddOnEventScreen screen = ScreenFor(*addOn, spec.event);
                                const float middle = top + design::kTwoLineRowHeight / 2.0f;
                                const int icon = screen.displays.empty() ? 0 : screen.displays.front().icon;
                                DrawPixelIcon(icon, {design::kLeadingIconCenter, middle}, kRowIconPixel,
                                              screen.enabled ? p.primary : p.onSurfaceVariant);
                                const float textRight = design::kTrailingIconCenter - design::kSpace4;
                                canvas_.Text(T(spec.name), {design::kLeadingTextInset, top + 9.0f, textRight, top + 31.0f},
                                             TextStyle::BodyLarge, screen.enabled ? p.onSurface : p.onSurfaceVariant);
                                canvas_.Text(ScreenSummary(screen),
                                             {design::kLeadingTextInset, top + 31.0f, textRight, top + 50.0f},
                                             TextStyle::BodySmall, p.onSurfaceVariant);
                                canvas_.Glyph(kGlyphChevronRight, {design::kTrailingIconCenter, middle},
                                              design::kSmallIconSize, p.onSurfaceVariant);
                                AddHit({TileRect(top, design::kTwoLineRowHeight), id, [this, event = spec.event] {
                                            addOnEvent_ = event;
                                            GoTo(Page::AddOnEvent);
                                        }});
                            },
                            id});
        }
        y = DrawCard(y, rows) + design::kSectionGap;
    } else if (!AddOnOptions(path).empty()) {
        y = DrawAddOnOptions(*addOn, y);
    } else {
        y = PageHint(T(Text::AddOnGenericHint), y);
    }

    y = PageHint(path.c_str(), y);
    CardRow remove{kRowHeight,
                   [this, &p, path](float top) {
                       canvas_.Glyph(kGlyphDelete, {design::kLeadingIconCenter, top + kRowHeight / 2.0f},
                                     design::kIconSize - 2.0f, p.error);
                       canvas_.Text(T(Text::AddOnRemove),
                                    {design::kLeadingTextInset, top, kWidth - kInset, top + kRowHeight},
                                    TextStyle::BodyLarge, p.error);
                       AddHit({TileRect(top, kRowHeight), kIdRemoveAddOn, [this, path] {
                                   Edit([path](Settings& s) {
                                       std::erase_if(s.addOns, [&](const AddOnSettings& item) {
                                           return _wcsicmp(item.path.c_str(), path.c_str()) == 0;
                                       });
                                   });
                                   GoTo(Page::AddOns);
                               }});
                   },
                   kIdRemoveAddOn};
    y = DrawCard(y, {remove});
    EndScroll(Page::AddOnEdit, kHeaderHeight, y);
}

float Panel::DrawAddOnOptions(const AddOnSettings& addOn, float y) {
    const Palette& p = P();
    const std::vector<AddOnOption>& options = AddOnOptions(addOn.path);
    std::vector<CardRow> rows;
    auto flush = [&] {
        if (!rows.empty()) {
            y = DrawCard(y, rows) + design::kSectionGap;
            rows.clear();
        }
    };
    auto set = [this](const std::wstring& key, std::wstring value) {
        EditAddOn([key, value = std::move(value)](AddOnSettings& item) { item.options[key] = value; });
    };
    for (const AddOnOption& option : options) {
        if (!AddOnOptionShown(addOn, options, option)) {
            continue;
        }
        const uint32_t id = HashId(kKindAddOn, L"option:" + option.key);
        const std::wstring value = AddOnOptionValue(addOn, option);
        const wchar_t* hint = option.hint.empty() ? nullptr : option.hint.c_str();
        switch (option.kind) {
        case AddOnOption::Kind::Section:
            flush();
            y = SectionTitle(option.label.c_str(), y);
            if (hint) {
                y = PageHint(hint, y);
            }
            break;
        case AddOnOption::Kind::Hint:
            flush();
            y = PageHint(option.label.c_str(), y);
            break;
        case AddOnOption::Kind::Switch: {
            const bool on = value == L"1";
            rows.push_back(SwitchItem(id, option.label.c_str(), on,
                                      [set, key = option.key, on] { set(key, on ? L"0" : L"1"); }, hint));
            break;
        }
        case AddOnOption::Kind::Choice: {
            const auto chosen = std::find_if(option.choices.begin(), option.choices.end(),
                                             [&](const AddOnOption::Choice& choice) { return choice.value == value; });
            const int selected = chosen == option.choices.end() ? 0 : static_cast<int>(chosen - option.choices.begin());
            std::vector<std::wstring> values;
            std::vector<std::wstring> labels;
            for (const AddOnOption::Choice& choice : option.choices) {
                values.push_back(choice.value);
                labels.push_back(choice.label);
            }
            auto select = [set, key = option.key, values](int index) { set(key, values[index]); };
            if (labels.size() <= 4) {
                rows.push_back(ChoiceItem(id, option.label.c_str(), std::move(labels), selected, select, hint));
            } else {
                const int count = static_cast<int>(labels.size());
                rows.push_back(DropdownItem(
                    id, option.label.c_str(), selected, count, [labels](int index) { return labels[index]; }, select,
                    hint));
            }
            break;
        }
        case AddOnOption::Kind::Multi: {
            // A dropdown of checkmarks; the field lists what is ticked.
            const std::wstring path = addOn.path;
            const std::wstring stateKey = path + L"\n" + option.key;
            if (option.query && addOnQueries_[stateKey].answeredAt == 0 && !addOnQueries_[stateKey].pending) {
                StartAddOnQuery(path, option.key, {});
            }
            const std::vector<std::wstring> ticked = TickedValues(value);
            std::wstring shown;
            for (const std::wstring& item : ticked) {
                shown += (shown.empty() ? L"" : L", ") + item;
            }
            if (shown.empty()) {
                shown = option.empty.empty() ? std::wstring(L"\u2014") : option.empty;
            }
            const float hintHeight = hint ? HintHeight(hint) : 0.0f;
            rows.push_back({hint ? design::kHintTopInRow + hintHeight : kRowHeight,
                            [this, &option, id, shown, ticked, hint, hintHeight, path, stateKey](float top) {
                                const D2D1_RECT_F field = DrawDropdown(
                                    id, kWidth - kInset, top + kRowHeight / 2.0f, shown, 200.0f,
                                    [this, option, ticked, path, stateKey](const D2D1_RECT_F& anchor) {
                                        AddOnQueryState& state = addOnQueries_[stateKey];
                                        if (option.query && !state.pending &&
                                            GetTickCount64() - state.answeredAt > kChoicesFreshMs) {
                                            StartAddOnQuery(path, option.key, {});
                                        }
                                        // What the program lists, then what is ticked but not listed now.
                                        std::vector<AddOnOption::Choice> choices =
                                            option.query ? state.answer.choices : option.choices;
                                        for (const std::wstring& item : ticked) {
                                            const bool listed = std::any_of(
                                                choices.begin(), choices.end(),
                                                [&](const AddOnOption::Choice& choice) { return choice.value == item; });
                                            if (!listed) {
                                                choices.push_back({item, item});
                                            }
                                        }
                                        std::vector<MenuItem> items;
                                        std::vector<std::wstring> values;
                                        if (option.query && state.answeredAt == 0) {
                                            items.push_back({T(Text::AddOnLoading), true});
                                            values.emplace_back();
                                        }
                                        for (const AddOnOption::Choice& choice : choices) {
                                            MenuItem item{choice.label};
                                            item.checked =
                                                std::find(ticked.begin(), ticked.end(), choice.value) != ticked.end();
                                            items.push_back(std::move(item));
                                            values.push_back(choice.value);
                                        }
                                        if (choices.empty() && state.answeredAt) {
                                            items.push_back({T(Text::NothingFound), true});
                                            values.emplace_back();
                                        }
                                        OpenCheckMenu(anchor, std::move(items), [this, key = option.key, values](int index) {
                                            const std::wstring chosen = values[static_cast<size_t>(index)];
                                            EditAddOn([key, chosen](AddOnSettings& item) {
                                                std::vector<std::wstring> list = TickedValues(item.options[key]);
                                                const auto found = std::find(list.begin(), list.end(), chosen);
                                                if (found == list.end()) {
                                                    list.push_back(chosen);
                                                } else {
                                                    list.erase(found);
                                                }
                                                std::wstring joined;
                                                for (const std::wstring& value : list) {
                                                    joined += (joined.empty() ? L"" : L"\n") + value;
                                                }
                                                item.options[key] = joined;
                                            });
                                        });
                                    });
                                DrawRowLabel(option.label.c_str(),
                                             {kInset, top, field.left - design::kSpace3, top + kRowHeight}, P().onSurface);
                                if (hint) {
                                    DrawHint(hint, top + design::kHintTopInRow, hintHeight);
                                }
                            },
                            id});
            break;
        }
        case AddOnOption::Kind::Search: {
            // The label, the field with the picked place, and below it the search's state or the supporting text.
            // Enter searches; the places found open as a menu under the field.
            const std::wstring path = addOn.path;
            const auto label = addOn.options.find(option.key + L".label");
            const std::wstring picked = label != addOn.options.end() && !label->second.empty() ? label->second : value;
            const AddOnQueryState* state = nullptr;
            if (const auto found = addOnQueries_.find(path + L"\n" + option.key); found != addOnQueries_.end()) {
                state = &found->second;
            }
            std::wstring status;
            if (state && state->pending) {
                status = T(Text::AddOnSearching);
            } else if (state && state->answeredAt && !state->answer.answered) {
                status = T(Text::AddOnNoAnswer);
            } else if (state && state->answeredAt && state->answer.choices.empty()) {
                status = state->answer.error.empty() ? std::wstring(T(Text::NothingFound)) : state->answer.error;
            }
            const std::wstring below = status.empty() ? option.hint : status;
            const float belowHeight = below.empty() ? 0.0f : HintHeight(below.c_str());
            rows.push_back({kRowHeight - 8.0f + kFieldHeight + design::kSpace3 +
                                (below.empty() ? 0.0f : belowHeight + design::kSpace1),
                            [this, &p, &option, id, picked, below, belowHeight, path](float top) {
                                canvas_.Text(option.label.c_str(), {kInset, top, kWidth - kInset, top + kRowHeight - 8.0f},
                                             TextStyle::BodyLarge, p.onSurface);
                                const float fieldTop = top + kRowHeight - 8.0f;
                                const D2D1_RECT_F field{kInset, fieldTop, kWidth - kInset, fieldTop + kFieldHeight};
                                addOnFields_[option.key] = field;
                                DrawEditableValue(id, field, picked,
                                                  [this, path, key = option.key, picked](const std::wstring& text) {
                                                      if (text != picked) {
                                                          StartAddOnQuery(path, key, text);
                                                      }
                                                      return true;
                                                  },
                                                  false);
                                if (!below.empty()) {
                                    DrawHint(below.c_str(), fieldTop + kFieldHeight + design::kSpace1, belowHeight);
                                }
                            },
                            id});
            break;
        }
        case AddOnOption::Kind::Text: {
            // The label, the editable value below it, then the supporting text.
            const float hintHeight = hint ? HintHeight(hint) : 0.0f;
            rows.push_back({kRowHeight - 8.0f + kFieldHeight + design::kSpace3 + (hint ? hintHeight + design::kSpace1 : 0.0f),
                            [this, &p, &option, id, value, hint, hintHeight, set](float top) {
                                canvas_.Text(option.label.c_str(), {kInset, top, kWidth - kInset, top + kRowHeight - 8.0f},
                                             TextStyle::BodyLarge, p.onSurface);
                                const float fieldTop = top + kRowHeight - 8.0f;
                                DrawEditableValue(id, {kInset, fieldTop, kWidth - kInset, fieldTop + kFieldHeight}, value,
                                                  [set, key = option.key](const std::wstring& text) {
                                                      set(key, text);
                                                      return true;
                                                  },
                                                  false);
                                if (hint) {
                                    DrawHint(hint, fieldTop + kFieldHeight + design::kSpace1, hintHeight);
                                }
                            },
                            id});
            break;
        }
        }
    }
    flush();
    return y;
}

std::vector<std::wstring> Panel::TickedValues(const std::wstring& value) {
    std::vector<std::wstring> values;
    for (size_t start = 0; start < value.size();) {
        size_t end = value.find(L'\n', start);
        if (end == std::wstring::npos) {
            end = value.size();
        }
        if (end > start) {
            values.push_back(value.substr(start, end - start));
        }
        start = end + 1;
    }
    return values;
}

void Panel::StartAddOnQuery(const std::wstring& path, const std::wstring& key, const std::wstring& text) {
    AddOnQueryState& state = addOnQueries_[path + L"\n" + key];
    state.pending = true;
    state.asked = text;
    const HWND owner = window_;
    std::thread([owner, path, key, text] {
        auto result = std::make_unique<AddOnQueryResult>(QueryAddOn(path, key, text));
        if (PostMessageW(owner, WM_APP_ADDON_QUERY, 0, reinterpret_cast<LPARAM>(result.get()))) {
            result.release();
        }
    }).detach();
    Invalidate();
}

void Panel::OnAddOnQueryDone(AddOnQueryResult* result) {
    std::unique_ptr<AddOnQueryResult> answer(result);
    AddOnQueryState& state = addOnQueries_[answer->path + L"\n" + answer->key];
    if (answer->text != state.asked) {
        return;  // A newer query is on its way.
    }
    state.pending = false;
    state.answeredAt = GetTickCount64();
    state.answer = std::move(*answer);
    Invalidate();
    // A search's results open under its field, for one of them to be picked.
    const AddOnQueryResult& found = state.answer;
    if (page_ != Page::AddOnEdit || _wcsicmp(addOnPath_.c_str(), found.path.c_str()) != 0 || found.choices.empty() ||
        menu_.open) {
        return;
    }
    const std::vector<AddOnOption>& options = AddOnOptions(found.path);
    const bool search = std::any_of(options.begin(), options.end(), [&](const AddOnOption& option) {
        return option.key == found.key && option.kind == AddOnOption::Kind::Search;
    });
    const auto field = addOnFields_.find(found.key);
    if (!search || field == addOnFields_.end()) {
        return;
    }
    std::vector<MenuItem> items;
    for (const AddOnOption::Choice& choice : found.choices) {
        items.push_back({choice.label});
    }
    OpenMenu(field->second, std::move(items), -1, [this, key = found.key, choices = found.choices](int index) {
        const AddOnOption::Choice& choice = choices[static_cast<size_t>(index)];
        EditAddOn([key, choice](AddOnSettings& item) {
            item.options[key] = choice.value;
            item.options[key + L".label"] = choice.label;
        });
    });
}

std::wstring Panel::ScreenSummary(const AddOnEventScreen& screen) const {
    if (!screen.enabled) {
        return T(Text::OptOff);
    }
    std::wstring summary = screen.whileOn      ? std::wstring(T(Text::RepeatWhileOn))
                           : screen.times > 1 ? std::to_wstring(screen.times) + L"×"
                                              : std::wstring(T(Text::RepeatOnce));
    if (!screen.displays.empty()) {
        summary += L" · " + DisplaySummary(screen.displays.front());
        if (screen.displays.size() > 1) {
            summary += L" +" + std::to_wstring(screen.displays.size() - 1);
        }
    }
    return summary;
}

std::wstring Panel::DisplaySummary(const AddOnDisplay& display) const {
    if (!display.image.empty()) {
        return FileNameOf(display.image);
    }
    if (display.heading.empty()) {
        return display.text;
    }
    return display.text.empty() ? display.heading : display.heading + L" / " + display.text;
}

void Panel::DrawAddOnEvent() {
    const Palette& p = P();
    AddOnSettings* addOn = EditedAddOn();
    const AddOnEventSpec* spec = FindDiscordEvent(addOnEvent_);
    if (!addOn || !spec) {
        DrawHeader(T(Text::AddOns));
        return;
    }
    DrawHeader(T(spec->name));
    float y = BeginScroll(Page::AddOnEvent, kHeaderHeight);
    const AddOnEventScreen screen = ScreenFor(*addOn, addOnEvent_);
    std::vector<CardRow> rows{SwitchItem(kIdAddOnEventEnabled, T(Text::AddOnShowEvent), screen.enabled, [this] {
        EditAddOnScreen([](AddOnEventScreen& s) { s.enabled = !s.enabled; });
    })};
    if (screen.enabled) {
        std::vector<Text> repeats{Text::RepeatOnce, Text::RepeatTimes};
        if (spec->hasEnd) {
            repeats.push_back(Text::RepeatWhileOn);
        }
        const int repeat = screen.whileOn ? 2 : screen.times > 1 ? 1 : 0;
        rows.push_back(DropdownItem(
            HashId(kKindAddOn, L"repeat"), T(Text::AddOnRepeat), repeat, static_cast<int>(repeats.size()),
            [repeats](int i) { return std::wstring(T(repeats[i])); },
            [this](int i) {
                EditAddOnScreen([i](AddOnEventScreen& s) {
                    s.whileOn = i == 2;
                    s.times = i == 1 ? std::max(2, s.times) : 1;
                });
            },
            T(screen.whileOn ? Text::RepeatWhileOnHint : Text::RepeatHint)));
        if (!screen.whileOn && screen.times > 1) {
            const int times = screen.times;
            rows.push_back(NumberItem(
                kIdAddOnTimes, T(Text::AddOnTimes), std::to_wstring(times), times > 2, times < kMaxTimes,
                [this] { EditAddOnScreen([](AddOnEventScreen& s) { s.times = std::max(2, s.times - 1); }); },
                [this] { EditAddOnScreen([](AddOnEventScreen& s) { s.times = std::min(kMaxTimes, s.times + 1); }); },
                [this](const std::wstring& text) {
                    const int typed = _wtoi(text.c_str());
                    if (typed < 1) {
                        return false;
                    }
                    EditAddOnScreen([typed](AddOnEventScreen& s) { s.times = std::clamp(typed, 1, kMaxTimes); });
                    return true;
                }));
        }
    }
    y = DrawCard(y, rows) + design::kSectionGap;

    if (screen.enabled) {
        y = SectionTitle(T(Text::AddOnDisplays), y);
        y = PageHint(T(Text::AddOnDisplaysHint), y);
        std::vector<uint32_t> ids;
        for (size_t i = 0; i < screen.displays.size(); ++i) {
            ids.push_back(HashId(kKindAddOn, L"display" + std::to_wstring(i)));
        }
        if (!ids.empty()) {
            y = DrawDragCard(
                    y, kIdAddOnDisplayList, ids, design::kTwoLineRowHeight,
                    [this, &p, screen](size_t index, size_t place, const D2D1_RECT_F& row, bool lifted) {
                        const AddOnDisplay& display = screen.displays[index];
                        const float middle = (row.top + row.bottom) / 2.0f;
                        DrawPixelIcon(display.image.empty() ? display.icon : kIconNone,
                                      {design::kLeadingIconCenter, middle}, kRowIconPixel,
                                      lifted ? p.primary : p.onSurface);
                        const float textRight = design::kTrailingIconCenter - design::kSpace4;
                        canvas_.Text(std::wstring(T(Text::AddOnDisplay)) + L" " + std::to_wstring(place + 1) +
                                         L" · " + Seconds(display.lengthMs),
                                     {design::kLeadingTextInset, row.top + 9.0f, textRight, row.top + 31.0f},
                                     TextStyle::BodyLarge, lifted ? p.primary : p.onSurface);
                        canvas_.Text(DisplaySummary(display),
                                     {design::kLeadingTextInset, row.top + 31.0f, textRight, row.top + 50.0f},
                                     TextStyle::BodySmall, p.onSurfaceVariant);
                    },
                    [this](size_t from, size_t to) {
                        EditAddOnScreen([from, to](AddOnEventScreen& s) { MoveItem(s.displays, from, to); });
                    },
                    [this](size_t index) {
                        addOnDisplay_ = index;
                        GoTo(Page::AddOnDisplay);
                    }) +
                design::kGroupGap;
        }
        const AddOnEventSpec* fields = spec;
        CardRow add{kRowHeight,
                    [this, &p, fields, count = screen.displays.size()](float top) {
                        canvas_.Glyph(kGlyphAdd, {design::kLeadingIconCenter, top + kRowHeight / 2.0f},
                                      design::kIconSize - 2.0f, p.primary);
                        canvas_.Text(T(Text::AddOnAddDisplay),
                                     {design::kLeadingTextInset, top, kWidth - kInset, top + kRowHeight},
                                     TextStyle::BodyLarge, p.primary);
                        AddHit({TileRect(top, kRowHeight), kIdAddOnAddDisplay, [this, fields, count] {
                                    EditAddOnScreen([fields](AddOnEventScreen& s) {
                                        AddOnDisplay display;
                                        display.text = std::wstring(L"{") + fields->fields[0] + L"}";
                                        s.displays.push_back(std::move(display));
                                    });
                                    addOnDisplay_ = count;
                                    GoTo(Page::AddOnDisplay);
                                }});
                    },
                    kIdAddOnAddDisplay};
        y = DrawCard(y, {add}) + design::kSectionGap;

        const bool running = addOns_.state == AddOnsView::State::Running;
        y = DrawCard(y, {{kRowHeight,
                          [this, &p, running](float top) {
                              const D2D1_COLOR_F color = running ? p.primary : Alpha(p.onSurface, design::kDisabledOpacity);
                              canvas_.Glyph(kGlyphHeadphones, {design::kLeadingIconCenter, top + kRowHeight / 2.0f},
                                            design::kIconSize - 2.0f, color);
                              canvas_.Text(T(Text::AddOnPreview),
                                           {design::kLeadingTextInset, top, kWidth - kInset, top + kRowHeight},
                                           TextStyle::BodyLarge, color);
                              if (running) {
                                  AddHit({TileRect(top, kRowHeight), kIdAddOnPreview,
                                          [this] { host_.OnAddOnPreview(addOnPath_, addOnEvent_); }});
                              }
                          },
                          running ? kIdAddOnPreview : 0u}}) +
            design::kGroupGap;
        if (!running) {
            y = PageHint(T(Text::AddOnPreviewHint), y);
        }
    }
    CardRow reset{kRowHeight,
                  [this, &p](float top) {
                      canvas_.Glyph(kGlyphReset, {design::kLeadingIconCenter, top + kRowHeight / 2.0f},
                                    design::kIconSize - 2.0f, p.onSurfaceVariant);
                      canvas_.Text(T(Text::AddOnReset), {design::kLeadingTextInset, top, kWidth - kInset, top + kRowHeight},
                                   TextStyle::BodyLarge, p.onSurface);
                      AddHit({TileRect(top, kRowHeight), kIdAddOnReset, [this] {
                                  EditAddOn([event = addOnEvent_](AddOnSettings& item) { item.screens.erase(event); });
                              }});
                  },
                  kIdAddOnReset};
    y = DrawCard(y, {reset});
    EndScroll(Page::AddOnEvent, kHeaderHeight, y);
}

void Panel::DrawAddOnDisplay() {
    const Palette& p = P();
    AddOnSettings* addOn = EditedAddOn();
    const AddOnEventSpec* spec = FindDiscordEvent(addOnEvent_);
    const AddOnEventScreen screen = addOn ? ScreenFor(*addOn, addOnEvent_) : AddOnEventScreen{};
    if (!addOn || !spec || addOnDisplay_ >= screen.displays.size()) {
        DrawHeader(T(Text::AddOns));
        return;
    }
    const size_t index = addOnDisplay_;
    const AddOnDisplay display = screen.displays[index];
    const std::wstring title = std::wstring(T(Text::AddOnDisplay)) + L" " + std::to_wstring(index + 1);
    DrawHeader(title.c_str(), T(spec->name));
    float y = BeginScroll(Page::AddOnDisplay, kHeaderHeight);
    auto editDisplay = [this, index](std::function<void(AddOnDisplay&)> change) {
        EditAddOnScreen([index, change](AddOnEventScreen& s) {
            if (index < s.displays.size()) {
                change(s.displays[index]);
            }
        });
    };
    const bool image = !display.image.empty();
    std::vector<CardRow> rows;
    rows.push_back(ChoiceItem(kIdAddOnDisplayKind, T(Text::AddOnDisplayKind),
                              {T(Text::AddOnKindText), T(Text::AddOnKindImage)}, image ? 1 : 0,
                              [this, editDisplay, image](int choice) {
                                  if (choice == 0 && image) {
                                      editDisplay([](AddOnDisplay& d) { d.image.clear(); });
                                  } else if (choice == 1 && !image) {
                                      RunFileDialog(FileDialog::AddOnImage);
                                  }
                              }));
    rows.push_back(NumberItem(
        kIdAddOnLength, T(Text::AddOnLength), Seconds(display.lengthMs), display.lengthMs > kMinDisplayMs,
        display.lengthMs < kMaxDisplayMs,
        [editDisplay] { editDisplay([](AddOnDisplay& d) { d.lengthMs = std::max(kMinDisplayMs, d.lengthMs - kDisplayStepMs); }); },
        [editDisplay] { editDisplay([](AddOnDisplay& d) { d.lengthMs = std::min(kMaxDisplayMs, d.lengthMs + kDisplayStepMs); }); },
        [editDisplay](const std::wstring& text) {
            const double seconds = _wtof(text.c_str());
            if (seconds <= 0.0) {
                return false;
            }
            editDisplay([seconds](AddOnDisplay& d) {
                d.lengthMs = std::clamp(static_cast<int>(std::lround(seconds * 2.0)) * kDisplayStepMs, kMinDisplayMs,
                                        kMaxDisplayMs);
            });
            return true;
        }));
    y = DrawCard(y, rows) + design::kSectionGap;

    if (image) {
        y = SectionTitle(T(Text::AddOnKindImage), y);
        y = DrawCard(y, {{design::kTwoLineRowHeight, [this, &p, display](float top) {
                              const float right = kWidth - kInset;
                              const float buttonTop = top + (design::kTwoLineRowHeight - design::kControlHeight) / 2.0f;
                              canvas_.Text(T(Text::AddOnImageFile), {kInset, top + 9.0f, right - 120.0f, top + 31.0f},
                                           TextStyle::BodyLarge, p.onSurface);
                              canvas_.Text(FileNameOf(display.image), {kInset, top + 31.0f, right - 120.0f, top + 50.0f},
                                           TextStyle::BodySmall, p.onSurfaceVariant);
                              TextButton(kIdAddOnImage, {right - 108.0f, buttonTop, right, buttonTop + design::kControlHeight},
                                         kGlyphFolder, T(Text::IdleChoose), true,
                                         [this] { RunFileDialog(FileDialog::AddOnImage); });
                          }},
                         HintItem(T(Text::AddOnImageHint))}) +
            design::kSectionGap;
    } else {
        // The icon picker: every icon in a grid, the chosen one marked.
        y = SectionTitle(T(Text::AddOnIcon), y);
        constexpr int kColumns = 7;
        const int rowsOfIcons = (kAddOnIconCount + kColumns - 1) / kColumns;
        const float gridHeight = rowsOfIcons * kIconCell + 2.0f * design::kSpace2;
        y = DrawCard(y, {{gridHeight, [this, &p, display, editDisplay](float top) {
                              const float gridWidth = kColumns * kIconCell;
                              const float left = (kWidth - gridWidth) / 2.0f;
                              for (int icon = 0; icon < kAddOnIconCount; ++icon) {
                                  const float cellLeft = left + (icon % kColumns) * kIconCell;
                                  const float cellTop = top + design::kSpace2 + (icon / kColumns) * kIconCell;
                                  const D2D1_RECT_F cell{cellLeft + 2.0f, cellTop + 2.0f, cellLeft + kIconCell - 2.0f,
                                                         cellTop + kIconCell - 2.0f};
                                  const bool chosen = icon == display.icon;
                                  const uint32_t id = PartId(kIdAddOnIcons, static_cast<uint32_t>(icon) + 1);
                                  if (chosen) {
                                      canvas_.FillRound(cell, design::kRadiusField, p.primaryContainer);
                                  } else {
                                      StateLayer(id, cell, design::kRadiusField, p.onSurface);
                                  }
                                  const D2D1_POINT_2F center{(cell.left + cell.right) / 2.0f, (cell.top + cell.bottom) / 2.0f};
                                  if (icon == kIconNone) {
                                      canvas_.Glyph(kGlyphBlocked, center, design::kIconSize - 2.0f, p.onSurfaceVariant);
                                  } else {
                                      DrawPixelIcon(icon, center, kIconPixel, chosen ? p.onPrimaryContainer : p.onSurface);
                                  }
                                  AddHit({cell, id, [editDisplay, icon] {
                                              editDisplay([icon](AddOnDisplay& d) { d.icon = icon; });
                                          }});
                              }
                          }}}) +
            design::kSectionGap;

        // Heading and text: editable templates, with the event's fields to add.
        y = SectionTitle(T(Text::AddOnWords), y);
        y = PageHint(T(Text::AddOnFieldsHint), y);
        auto templateRow = [&](uint32_t id, Text label, const std::wstring& value, bool heading) {
            return CardRow{kRowHeight + kFieldHeight + kChipHeight + design::kSpace3,
                           [this, &p, id, label, value, heading, editDisplay, spec](float top) {
                               canvas_.Text(T(label), {kInset, top, kWidth - kInset, top + kRowHeight - 8.0f},
                                            TextStyle::BodyLarge, p.onSurface);
                               const float fieldTop = top + kRowHeight - 8.0f;
                               DrawEditableValue(id, {kInset, fieldTop, kWidth - kInset, fieldTop + kFieldHeight}, value,
                                                 [editDisplay, heading](const std::wstring& text) {
                                                     editDisplay([&](AddOnDisplay& d) {
                                                         (heading ? d.heading : d.text) = text;
                                                     });
                                                     return true;
                                                 },
                                                 false);
                               float chipLeft = kInset;
                               const float chipTop = fieldTop + kFieldHeight + design::kSpace2;
                               for (int f = 0; f < 2; ++f) {
                                   const std::wstring field = spec->fields[f];
                                   const std::wstring chip = std::wstring(L"+ ") + T(FieldLabel(field));
                                   const float width = canvas_.Measure(chip, TextStyle::LabelLarge) + 2.0f * design::kSpace4;
                                   TextButton(PartId(id, static_cast<uint32_t>(f) + 1),
                                              {chipLeft, chipTop, chipLeft + width, chipTop + kChipHeight}, 0,
                                              chip.c_str(), true, [editDisplay, heading, field] {
                                                  editDisplay([&](AddOnDisplay& d) {
                                                      std::wstring& target = heading ? d.heading : d.text;
                                                      target += (target.empty() || target.back() == L' ' ? L"" : L" ") +
                                                                std::wstring(L"{") + field + L"}";
                                                  });
                                              });
                                   chipLeft += width + design::kSpace2;
                               }
                           }};
        };
        y = DrawCard(y, {templateRow(kIdAddOnHeading, Text::AddOnHeading, display.heading, true),
                         templateRow(kIdAddOnText, Text::AddOnText, display.text, false)}) +
            design::kSectionGap;
    }

    CardRow remove{kRowHeight,
                   [this, &p, index](float top) {
                       canvas_.Glyph(kGlyphDelete, {design::kLeadingIconCenter, top + kRowHeight / 2.0f},
                                     design::kIconSize - 2.0f, p.error);
                       canvas_.Text(T(Text::AddOnRemoveDisplay),
                                    {design::kLeadingTextInset, top, kWidth - kInset, top + kRowHeight},
                                    TextStyle::BodyLarge, p.error);
                       AddHit({TileRect(top, kRowHeight), kIdAddOnRemoveDisplay, [this, index] {
                                   EditAddOnScreen([index](AddOnEventScreen& s) {
                                       if (index < s.displays.size()) {
                                           s.displays.erase(s.displays.begin() + static_cast<std::ptrdiff_t>(index));
                                       }
                                   });
                                   GoTo(Page::AddOnEvent);
                               }});
                   },
                   kIdAddOnRemoveDisplay};
    y = DrawCard(y, {remove});
    EndScroll(Page::AddOnDisplay, kHeaderHeight, y);
}

}  // namespace ui
