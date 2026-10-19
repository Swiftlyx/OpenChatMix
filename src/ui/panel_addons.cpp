// Third-party add-ons: programs that talk SteelSeries GameSense, run by OpenChatMix while SteelSeries GG is closed,
// and the screens their events put on the headset display.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "../addon_events.h"
#include "../addon_icons.h"
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
