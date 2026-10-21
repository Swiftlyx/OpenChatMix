// Design system components: cards of rows, row types, drag-to-reorder lists, editable values and the
// pin. Sizes come from design.h; docs/design-system.md has the rules for using them.

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <string>
#include <vector>

#include "panel.h"
#include "panel_internal.h"

namespace ui {

namespace detail {

std::wstring ClipboardText(HWND owner) {
    std::wstring text;
    if (!OpenClipboard(owner)) {
        return text;
    }
    if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
        if (const auto* chars = static_cast<const wchar_t*>(GlobalLock(data))) {
            text = chars;
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
    // Only the first line: fields and searches hold one.
    if (const size_t end = text.find_first_of(L"\r\n"); end != std::wstring::npos) {
        text.resize(end);
    }
    std::erase_if(text, [](wchar_t c) { return c < L' ' || c == 0x7F; });
    return text;
}

void SetClipboardText(HWND owner, const std::wstring& text) {
    if (text.empty() || !OpenClipboard(owner)) {
        return;
    }
    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* chars = GlobalLock(memory)) {
            memcpy(chars, text.c_str(), bytes);
            GlobalUnlock(memory);
            if (!SetClipboardData(CF_UNICODETEXT, memory)) {
                GlobalFree(memory);
            }
        } else {
            GlobalFree(memory);
        }
    }
    CloseClipboard();
}

}  // namespace detail

using namespace detail;

namespace {

constexpr float kDragThreshold = 4.0f;  // Pointer travel before a press becomes a drag.

}  // namespace

float Panel::DrawCard(float y, const std::vector<CardRow>& rows) {
    // In a scrolling page, rows more than a screen away from the view are only measured, so long lists (hundreds
    // of presets) stay smooth; the rows next to the view are still drawn, for keyboard focus to reach them.
    const bool inPage = pageScrollDepth_ != 0 && hitClip_.size() == pageScrollDepth_;
    const D2D1_RECT_F view = inPage ? hitClip_.back() : D2D1_RECT_F{};
    float top = y;
    for (size_t i = 0; i < rows.size(); ++i) {
        const CardRow& row = rows[i];
        if (!inPage || (top + row.height >= view.top - kHeight && top <= view.bottom + kHeight)) {
            DrawTile(TileRect(top, row.height), row.id, i == 0, i + 1 == rows.size());
            row.draw(top);
        }
        top += row.height + (i + 1 < rows.size() ? design::kSegmentGap : 0.0f);
    }
    return top;
}

Panel::CardRow Panel::SwitchItem(uint32_t id, const wchar_t* label, bool on, std::function<void()> toggle,
                                 const wchar_t* hint) {
    const float hintHeight = hint ? HintHeight(hint) : 0.0f;
    return {kRowHeight + hintHeight - (hint ? kRowHeight - design::kHintTopInRow : 0.0f),
            [this, id, label, on, toggle = std::move(toggle), hint, hintHeight](float top) {
                DrawSwitchRow(id, top, label, on, toggle);
                if (hint) {
                    DrawHint(hint, top + design::kHintTopInRow, hintHeight);
                    AddHit({TileRect(top + kRowHeight, design::kHintTopInRow + hintHeight - kRowHeight), id, toggle});
                }
            },
            id};
}

Panel::CardRow Panel::NavItem(uint32_t id, wchar_t glyph, D2D1_COLOR_F glyphColor, const std::wstring& title,
                              const std::wstring& subtitle, std::function<void()> click) {
    return {NavRowHeight(glyph, subtitle),
            [this, id, glyph, glyphColor, title, subtitle, click = std::move(click)](float top) {
                DrawNavRow(id, top, glyph, glyphColor, title, subtitle, click, 0.0f, true);
            },
            id};
}

Panel::CardRow Panel::ChoiceItem(uint32_t id, const wchar_t* label, std::vector<std::wstring> options, int selected,
                                 std::function<void(int)> select, const wchar_t* hint) {
    const float hintHeight = hint ? HintHeight(hint) : 0.0f;
    const float controlBottom = design::kChoiceRowHeight - design::kHintBottomGap;
    return {design::kChoiceRowHeight + (hint ? hintHeight - 2.0f : 0.0f),
            [this, id, label, options = std::move(options), selected, select = std::move(select), hint,
             controlBottom, hintHeight](float top) {
                const Palette& p = P();
                canvas_.Text(label, {kInset, top + design::kHintBottomGap, kWidth - kInset, top + 34.0f},
                             TextStyle::BodyLarge, p.onSurface);
                std::vector<const wchar_t*> labels;
                for (const std::wstring& option : options) {
                    labels.push_back(option.c_str());
                }
                DrawSegmented(
                    {kInset, top + controlBottom - design::kControlHeight, kWidth - kInset, top + controlBottom}, id,
                    labels, selected, p.primary, p.onPrimary, select);
                if (hint) {
                    // Supporting text starts 8 DIP below the button group.
                    DrawHint(hint, top + design::kChoiceRowHeight - 2.0f, hintHeight);
                }
            }};
}

Panel::CardRow Panel::ValueItem(uint32_t id, const wchar_t* label, const std::wstring& value,
                                std::function<void()> click) {
    // The value sits beside the label while both fit; otherwise it goes below the label, as a subtitle that
    // wraps when it needs to.
    const float valueLeft = kInset + canvas_.Measure(label, TextStyle::BodyLarge) + design::kSpace4;
    const float valueRight = design::kTrailingIconCenter - design::kSpace4;
    const bool beside = canvas_.Measure(value, TextStyle::BodyMedium) <= valueRight - valueLeft;
    const float height = beside ? kRowHeight : NavRowHeight(0, value);
    return {height,
            [this, id, label, value, click = std::move(click), valueLeft, valueRight, beside, height](float top) {
                const Palette& p = P();
                if (beside) {
                    canvas_.Text(label, {kInset, top, valueLeft, top + kRowHeight}, TextStyle::BodyLarge,
                                 p.onSurface);
                    canvas_.Text(value, {valueLeft, top, valueRight, top + kRowHeight}, TextStyle::BodyMedium,
                                 p.onSurfaceVariant, TextAlign::Trailing);
                } else {
                    canvas_.Text(label, {kInset, top + 9.0f, valueRight, top + design::kSubtitleTop},
                                 TextStyle::BodyLarge, p.onSurface);
                    canvas_.Text(value,
                                 {kInset, top + design::kSubtitleTop, valueRight,
                                  top + std::max(50.0f, height - design::kHintBottomGap)},
                                 TextStyle::BodySmall, p.onSurfaceVariant, TextAlign::Leading,
                                 height > design::kTwoLineRowHeight);
                }
                canvas_.Glyph(kGlyphChevronRight, {design::kTrailingIconCenter, top + height / 2.0f},
                              design::kSmallIconSize, p.onSurfaceVariant);
                AddHit({TileRect(top, height), id, click});
            },
            id};
}

Panel::CardRow Panel::DropdownItem(uint32_t id, const wchar_t* label, int index, int count,
                                   std::function<std::wstring(int)> labelOf, std::function<void(int)> select,
                                   const wchar_t* hint) {
    const float hintHeight = hint ? HintHeight(hint) : 0.0f;
    return {hint ? design::kHintTopInRow + hintHeight : kRowHeight,
            [this, id, label, index, count, labelOf = std::move(labelOf), select = std::move(select), hint,
             hintHeight](float top) {
                const D2D1_RECT_F field = DrawDropdown(
                    id, kWidth - kInset, top + kRowHeight / 2.0f, labelOf(index), 200.0f,
                    [this, index, count, labelOf, select](const D2D1_RECT_F& anchor) {
                        std::vector<MenuItem> items;
                        for (int i = 0; i < count; ++i) {
                            items.push_back({labelOf(i)});
                        }
                        OpenMenu(anchor, std::move(items), index, select);
                    });
                DrawRowLabel(label, {kInset, top, field.left - design::kSpace3, top + kRowHeight}, P().onSurface);
                if (hint) {
                    DrawHint(hint, top + design::kHintTopInRow, hintHeight);
                }
            }};
}

Panel::CardRow Panel::NumberItem(uint32_t id, const wchar_t* label, const std::wstring& value, bool canDecrease,
                                 bool canIncrease, std::function<void()> decrease, std::function<void()> increase,
                                 std::function<bool(const std::wstring&)> edit, const wchar_t* hint) {
    const float hintHeight = hint ? HintHeight(hint) : 0.0f;
    return {hint ? design::kHintTopInRow + hintHeight : kRowHeight,
            [this, id, label, value, canDecrease, canIncrease, decrease = std::move(decrease),
             increase = std::move(increase), edit = std::move(edit), hint, hintHeight](float top) {
                const float right = kWidth - kInset + design::kSpace1;
                DrawRowLabel(label, {kInset, top, right - NumberControlWidth() - design::kSpace3, top + kRowHeight},
                             P().onSurface);
                DrawNumberControl(id, right, top + kRowHeight / 2.0f, value, canDecrease, canIncrease, decrease,
                                  increase, edit);
                if (hint) {
                    DrawHint(hint, top + design::kHintTopInRow, hintHeight);
                }
            }};
}

Panel::CardRow Panel::HintItem(const wchar_t* text) {
    const float height = HintHeight(text) + design::kHintBottomGap;
    return {height, [this, text, height](float top) {
                DrawHint(text, top + design::kHintBottomGap, height - design::kHintBottomGap);
            }};
}

float Panel::DrawDragCard(float y, uint32_t listId, const std::vector<uint32_t>& ids, float rowHeight,
                          const std::function<void(size_t, size_t, const D2D1_RECT_F&, bool)>& drawRow,
                          const std::function<void(size_t, size_t)>& move, const std::function<void(size_t)>& click) {
    const Palette& p = P();
    const size_t count = ids.size();
    const float pitch = rowHeight + design::kSegmentGap;
    const float listTop = y;
    const bool dragging = drag_.list == listId && drag_.moved && drag_.index < count;

    // While a row is lifted the others make room where it would land.
    size_t landing = drag_.index;
    if (dragging) {
        const float center = drag_.pointerY - drag_.grabOffset + rowHeight / 2.0f;
        landing = static_cast<size_t>(
            std::clamp(std::floor((center - listTop + design::kSegmentGap / 2.0f) / pitch), 0.0f,
                       static_cast<float>(count - 1)));
    }
    for (size_t i = 0; i < count; ++i) {
        size_t slot = i;
        if (dragging && i != drag_.index) {
            if (drag_.index < i && i <= landing) {
                slot = i - 1;
            } else if (landing <= i && i < drag_.index) {
                slot = i + 1;
            }
        }
        // Positions animate relative to the list, so scrolling moves the rows with the page at once.
        const float offset = Animate(AnimKey(ids[i], 2), slot * pitch, design::kSpringDefaultSpatial);
        const float top = listTop + offset;
        const D2D1_RECT_F row = TileRect(top, rowHeight);
        Hit hit;
        hit.rect = row;
        hit.id = ids[i];
        // Press, drag and drop; the row under the pointer is recorded on press.
        hit.drag = [this, listId, i, top, rowHeight, pitch, listTop, count](float, float pointerY) {
            if (drag_.list != listId || drag_.index != i) {
                drag_ = {listId, i, pointerY, pointerY, pointerY - top, false, pitch, listTop, count};
                return;
            }
            drag_.pointerY = pointerY;
            if (!drag_.moved && std::fabs(pointerY - drag_.startY) > kDragThreshold) {
                drag_.moved = true;
            }
            Invalidate();
        };
        hit.release = [this, listId, i, move, click, key = AnimKey(ids[i], 2)] {
            const DragState state = drag_;
            drag_ = {};
            if (state.list != listId || state.index != i) {
                return;
            }
            if (!state.moved) {
                click(i);
                return;
            }
            const float released = state.pointerY - state.grabOffset - state.listTop;
            const size_t to = static_cast<size_t>(
                std::clamp(std::floor((released + state.rowHeight / 2.0f) / state.rowHeight), 0.0f,
                           static_cast<float>(state.count - 1)));
            // The row settles from where it was let go, not from where it started.
            SetAnimation(key, std::clamp(released, -state.rowHeight / 2.0f, (state.count - 0.5f) * state.rowHeight));
            if (to != i) {
                move(i, to);
            }
            Invalidate();
        };
        // The row's hit goes first, so buttons that drawRow places inside it are hit before it.
        AddHit(std::move(hit));
        if (!(dragging && i == drag_.index)) {
            DrawTile(row, ids[i], slot == 0, slot + 1 == count);
            drawRow(i, slot, row, false);
            canvas_.Glyph(kGlyphDragHandle, {design::kTrailingIconCenter, top + rowHeight / 2.0f},
                          design::kIconSize - 2.0f, p.onSurfaceVariant);
        }
    }
    if (dragging) {
        // The lifted row follows the pointer above the others, raised and fully rounded.
        const float top = std::clamp(drag_.pointerY - drag_.grabOffset, listTop - rowHeight / 2.0f,
                                     listTop + (count - 0.5f) * pitch - rowHeight / 2.0f);
        const D2D1_RECT_F row = TileRect(top, rowHeight);
        DrawTile(row, ids[drag_.index], true, true, true);
        drawRow(drag_.index, landing, row, true);
        canvas_.Glyph(kGlyphDragHandle, {design::kTrailingIconCenter, top + rowHeight / 2.0f}, design::kIconSize - 2.0f,
                      p.primary);
        animating_ = true;
    }
    return listTop + count * pitch - design::kSegmentGap;
}

void Panel::BeginField(uint32_t id, std::wstring text, std::function<bool(const std::wstring&)> commit,
                       bool numeric) {
    field_ = {};
    field_.id = id;
    field_.commit = std::move(commit);
    field_.numeric = numeric;
    field_.text = std::move(text);
    field_.anchor = 0;
    field_.caret = field_.text.size();
    Invalidate();
}

void Panel::DrawEditableValue(uint32_t id, const D2D1_RECT_F& rect, const std::wstring& value,
                              std::function<bool(const std::wstring&)> commit, bool numeric) {
    const Palette& p = P();
    if (field_.id == id) {
        canvas_.FillRound(rect, design::kRadiusField, p.surfaceContainerHighest);
        canvas_.StrokeRound(Inset(rect, 1.0f, 1.0f), design::kRadiusField - 1.0f, p.primary, 2.0f);
        const TextStyle style = field_.numeric ? TextStyle::LabelLarge : TextStyle::BodyLarge;
        const float width = canvas_.Measure(field_.text, style);
        // Numbers sit centered; names start at the left and keep the caret in view.
        const float start = field_.numeric ? (rect.left + rect.right - width) / 2.0f
                                           : std::min(rect.left + design::kSpace3, rect.right - design::kSpace3 - width);
        field_.rect = rect;
        field_.textLeft = start;
        auto xAt = [&](size_t index) {
            return start + canvas_.Measure(std::wstring_view(field_.text).substr(0, index), style);
        };
        const float textTop = rect.top + design::kSpace1 + 2.0f;
        const float textBottom = rect.bottom - design::kSpace1 - 2.0f;
        if (field_.anchor != field_.caret) {
            const float from = xAt(std::min(field_.anchor, field_.caret));
            const float to = xAt(std::max(field_.anchor, field_.caret));
            canvas_.FillRect({from, textTop, to, textBottom}, Alpha(p.primary, 0.28f));
        }
        canvas_.PushClip(Inset(rect, 2.0f, 2.0f));
        canvas_.Text(field_.text, {start, rect.top, start + width + 1.0f, rect.bottom}, style, p.onSurface);
        canvas_.PopClip();
        // A steady caret: the panel only redraws on input.
        const float caret = xAt(field_.caret);
        canvas_.FillRect({caret, textTop, caret + 1.5f, textBottom}, p.primary);
        // Pressing places the caret; dragging selects.
        Hit hit;
        hit.rect = rect;
        hit.id = id;
        hit.click = [] {};
        hit.drag = [this](float x, float) {
            field_.caret = FieldCaretAt(x);
            if (pressStarting_) {
                field_.anchor = field_.caret;
            }
            Invalidate();
        };
        AddHit(std::move(hit));
        return;
    }
    StateLayer(id, rect, design::kRadiusField, p.onSurface);
    canvas_.StrokeRound(Inset(rect, 0.5f, 0.5f), design::kRadiusField, Alpha(p.outline, 0.6f), 1.0f);
    canvas_.Text(value, rect, TextStyle::LabelLarge, p.onSurface, TextAlign::Center);
    AddHit({rect, id, [this, id, value, numeric, commit = std::move(commit)] {
                // A number is edited without its unit; all of it starts selected, so typing replaces it.
                std::wstring text;
                for (const wchar_t ch : value) {
                    if (!numeric) {
                        text += ch;
                    } else if (std::iswdigit(ch) || ch == L'.' || ch == L',' || ch == L'-' || ch == 0x2212 || ch == L'k') {
                        text += ch == 0x2212 ? L'-' : ch;
                    }
                }
                BeginField(id, std::move(text), commit, numeric);
            }});
}

size_t Panel::FieldCaretAt(float x) {
    size_t best = 0;
    float bestDistance = std::fabs(x - field_.textLeft);
    for (size_t i = 1; i <= field_.text.size(); ++i) {
        const float edge =
            field_.textLeft + canvas_.Measure(std::wstring_view(field_.text).substr(0, i),
                                              field_.numeric ? TextStyle::LabelLarge : TextStyle::BodyLarge);
        if (std::fabs(x - edge) < bestDistance) {
            best = i;
            bestDistance = std::fabs(x - edge);
        }
    }
    return best;
}

bool Panel::FieldKey(UINT key) {
    const bool shift = GetKeyState(VK_SHIFT) & 0x8000;
    const bool control = GetKeyState(VK_CONTROL) & 0x8000;
    const size_t size = field_.text.size();
    auto moveTo = [&](size_t caret) {
        field_.caret = caret;
        if (!shift) {
            field_.anchor = caret;
        }
    };
    const bool selected = field_.anchor != field_.caret;
    const size_t low = std::min(field_.anchor, field_.caret);
    const size_t high = std::max(field_.anchor, field_.caret);
    switch (key) {
    case VK_RETURN:
        CommitField(true);
        return true;
    case VK_ESCAPE:
        CancelField();
        return true;
    case VK_LEFT:
        moveTo(selected && !shift ? low : (field_.caret > 0 ? field_.caret - 1 : 0));
        break;
    case VK_RIGHT:
        moveTo(selected && !shift ? high : std::min(size, field_.caret + 1));
        break;
    case VK_HOME:
    case VK_UP:
        moveTo(0);
        break;
    case VK_END:
    case VK_DOWN:
        moveTo(size);
        break;
    case VK_DELETE:
        if (selected) {
            field_.text.erase(low, high - low);
            field_.caret = field_.anchor = low;
        } else if (field_.caret < size) {
            field_.text.erase(field_.caret, 1);
        }
        break;
    case 'A':
        if (!control) {
            return true;  // Typed text arrives through FieldChar.
        }
        field_.anchor = 0;
        field_.caret = size;
        break;
    case 'C':
    case 'X':
        if (!control) {
            return true;
        }
        if (selected) {
            SetClipboardText(window_, field_.text.substr(low, high - low));
            if (key == 'X') {
                field_.text.erase(low, high - low);
                field_.caret = field_.anchor = low;
            }
        }
        break;
    case 'V': {
        if (!control) {
            return true;
        }
        const std::wstring pasted = ClipboardText(window_);
        field_.text.erase(low, high - low);
        const size_t limit = field_.numeric ? 16u : 64u;
        const size_t room = field_.text.size() < limit ? limit - field_.text.size() : 0;
        field_.text.insert(low, pasted.substr(0, room));
        field_.caret = field_.anchor = low + std::min(pasted.size(), room);
        break;
    }
    default:
        return true;  // The field takes every key while it is open.
    }
    Invalidate();
    return true;
}

void Panel::FieldChar(wchar_t ch) {
    const size_t low = std::min(field_.anchor, field_.caret);
    const size_t high = std::max(field_.anchor, field_.caret);
    if (ch == L'\b' || ch == 0x7F) {
        if (low != high) {
            field_.text.erase(low, high - low);
            field_.caret = low;
        } else if (ch == 0x7F) {
            // Ctrl+Backspace: back to the start of the word, with the spaces after it.
            size_t start = field_.caret;
            while (start > 0 && std::iswspace(field_.text[start - 1])) {
                --start;
            }
            while (start > 0 && !std::iswspace(field_.text[start - 1])) {
                --start;
            }
            field_.text.erase(start, field_.caret - start);
            field_.caret = start;
        } else if (field_.caret > 0) {
            field_.text.erase(--field_.caret, 1);
        }
    } else if (ch >= L' ' && ch != 0x7F) {
        field_.text.erase(low, high - low);
        field_.caret = low;
        if (field_.text.size() < (field_.numeric ? 16u : 64u)) {
            field_.text.insert(field_.caret++, 1, ch);
        }
    }
    field_.anchor = field_.caret;
    Invalidate();
}

bool Panel::CommitField(bool keepOnError) {
    if (!field_.id) {
        return false;
    }
    FieldState field = std::move(field_);
    field_ = {};
    std::wstring text = field.text;
    if (field.numeric) {
        std::replace(text.begin(), text.end(), L',', L'.');
    }
    const bool ok = field.commit && field.commit(text);
    if (!ok && keepOnError) {
        field_ = std::move(field);
    }
    Invalidate();
    return ok;
}

void Panel::CancelField() {
    if (field_.id) {
        field_ = {};
        Invalidate();
    }
}

void Panel::DrawPinButton() {
    const Palette& p = P();
    const D2D1_POINT_2F center{kWidth - kGutter - design::kIconButtonSize / 2.0f, kHeaderHeight / 2.0f};
    if (pinned_) {
        canvas_.FillCircle(center, design::kIconButtonSize / 2.0f, p.secondaryContainer);
    }
    const D2D1_COLOR_F color = pinned_ ? p.onSecondaryContainer : p.onSurfaceVariant;
    IconButton(kIdPin, center, kGlyphPin, color, [this] {
        pinned_ = !pinned_;
        Invalidate();
    });
    if (pinned_) {
        // Segoe's filled pin lacks the needle; over the outline it makes the whole pin filled.
        canvas_.Glyph(kGlyphPinFill, center, design::kIconSize - 2.0f, color);
    }
    Tooltip(kIdPin, T(pinned_ ? Text::TipUnpin : Text::TipPin));
}

}  // namespace ui
