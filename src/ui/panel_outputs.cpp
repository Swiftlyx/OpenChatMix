// Output devices: their order of preference and switching when they come and go.

#include <string>
#include <vector>

#include "panel.h"
#include "panel_internal.h"

namespace ui {

using namespace detail;

namespace {

std::vector<std::wstring> DeviceIds(const std::vector<const OutputView*>& outputs) {
    std::vector<std::wstring> ids;
    for (const OutputView* output : outputs) {
        ids.push_back(output->id);
    }
    return ids;
}

}  // namespace

void Panel::SetOutputs(std::vector<OutputView> outputs) {
    outputs_ = std::move(outputs);
    Invalidate();
}

void Panel::DrawOutputs() {
    const Palette& p = P();
    const OutputSettings& outputs = settings_.outputs;
    DrawHeader(T(Text::OutputDevices));
    float y = BeginScroll(Page::Outputs, kHeaderHeight);

    y = DrawCard(
            y,
            {
                SwitchItem(
                    kIdSwitchOnLoss, T(Text::SwitchOnLoss), outputs.switchOnLoss,
                    [this] { Edit([](Settings& s) { s.outputs.switchOnLoss = !s.outputs.switchOnLoss; }); },
                    T(Text::SwitchOnLossHint)),
                SwitchItem(
                    kIdReturnToPreferred, T(Text::ReturnToPreferred), outputs.returnToPreferred,
                    [this] { Edit([](Settings& s) { s.outputs.returnToPreferred = !s.outputs.returnToPreferred; }); },
                    T(Text::ReturnToPreferredHint)),
            }) +
        design::kSectionGap;

    y = SectionTitle(T(Text::Priority), y);
    y = PageHint(T(Text::PriorityHint), y);
    if (outputs_.empty()) {
        y = DrawCard(y, {HintItem(T(Text::NoPlaybackDevices))});
        EndScroll(Page::Outputs, kHeaderHeight, y);
        return;
    }
    // Excluded devices arrive last; the drag card holds the others, in their order.
    std::vector<const OutputView*> allowed;
    std::vector<const OutputView*> excluded;
    for (const OutputView& output : outputs_) {
        (output.excluded ? excluded : allowed).push_back(&output);
    }
    // Right of the name: a check on the device in use, then the button that moves the row to the other list.
    const float buttonX = design::kTrailingIconCenter - 32.0f;
    const float checkX = buttonX - design::kIconButtonSize;
    const float nameRight = checkX - design::kSmallIconSize / 2.0f - design::kSpace2;
    if (allowed.empty()) {
        y = DrawCard(y, {HintItem(T(Text::AllOutputsExcluded))});
    } else {
        std::vector<uint32_t> ids;
        for (const OutputView* output : allowed) {
            ids.push_back(HashId(kKindOutput, output->id));
        }
        // Place in the order, the name and the check; a click uses the device.
        y = DrawDragCard(
            y, kIdOutputList, ids, kRowHeight,
            [this, &p, &allowed, buttonX, checkX, nameRight](size_t index, size_t place, const D2D1_RECT_F& row,
                                                             bool lifted) {
                const OutputView& output = *allowed[index];
                const float middle = (row.top + row.bottom) / 2.0f;
                const D2D1_COLOR_F accent = output.current || lifted ? p.primary : p.onSurfaceVariant;
                canvas_.Text(std::to_wstring(place + 1), {kInset - design::kSpace1, row.top, kInset + 16.0f, row.bottom},
                             TextStyle::LabelLarge, accent, TextAlign::Center);
                canvas_.Text(output.name, {design::kLeadingTextInset - design::kSpace3, row.top, nameRight, row.bottom},
                             TextStyle::BodyLarge, output.current ? p.primary : p.onSurface);
                if (output.current) {
                    canvas_.Glyph(kGlyphCheck, {checkX, middle}, design::kSmallIconSize, p.primary);
                }
                const std::wstring id = output.id;
                IconButton(HashId(kKindOutputExclude, id), {buttonX, middle}, kGlyphBlocked, p.onSurfaceVariant,
                           [this, id] { host_.OnOutputExclude(id, true); });
                Tooltip(HashId(kKindOutputExclude, id), T(Text::TipNeverSwitch));
            },
            [this](size_t from, size_t to) { host_.OnOutputMove(from, to); },
            // Runs on release, after this frame: it keeps its own copy of the device IDs.
            [this, deviceIds = DeviceIds(allowed)](size_t index) {
                if (index < deviceIds.size()) {
                    host_.OnOutputChoose(deviceIds[index]);
                }
            });
    }

    if (!excluded.empty()) {
        y = SectionTitle(T(Text::NeverSwitchTo), y + design::kSectionGap);
        y = PageHint(T(Text::NeverSwitchToHint), y);
        std::vector<CardRow> rows;
        for (const OutputView* output : excluded) {
            const uint32_t rowId = HashId(kKindOutput, output->id);
            rows.push_back({kRowHeight,
                            [this, &p, output, rowId, buttonX, checkX, nameRight](float top) {
                                const float middle = top + kRowHeight / 2.0f;
                                const std::wstring id = output->id;
                                AddHit({TileRect(top, kRowHeight), rowId, [this, id] { host_.OnOutputChoose(id); }});
                                canvas_.Glyph(kGlyphBlocked, {kInset + 8.0f - design::kSpace1 / 2.0f, middle},
                                              design::kSmallIconSize, p.onSurfaceVariant);
                                canvas_.Text(output->name,
                                             {design::kLeadingTextInset - design::kSpace3, top, nameRight, top + kRowHeight},
                                             TextStyle::BodyLarge, output->current ? p.primary : p.onSurfaceVariant);
                                if (output->current) {
                                    canvas_.Glyph(kGlyphCheck, {checkX, middle}, design::kSmallIconSize, p.primary);
                                }
                                IconButton(HashId(kKindOutputExclude, id), {buttonX, middle}, kGlyphUndo,
                                           p.onSurfaceVariant, [this, id] { host_.OnOutputExclude(id, false); });
                                Tooltip(HashId(kKindOutputExclude, id), T(Text::TipSwitchAgain));
                            },
                            rowId});
        }
        y = DrawCard(y, rows);
    }
    EndScroll(Page::Outputs, kHeaderHeight, y);
}

}  // namespace ui
