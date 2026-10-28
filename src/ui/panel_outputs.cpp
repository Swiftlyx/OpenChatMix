// Devices: the order of preference of the output devices and of the microphones, and switching when they come and go.

#include <string>
#include <vector>

#include "panel.h"
#include "panel_internal.h"

namespace ui {

using namespace detail;

namespace {

std::vector<std::wstring> DeviceIds(const std::vector<const DeviceView*>& devices) {
    std::vector<std::wstring> ids;
    for (const DeviceView* device : devices) {
        ids.push_back(device->id);
    }
    return ids;
}

}  // namespace

void Panel::SetDevices(DeviceList list, std::vector<DeviceView> devices) {
    devices_[static_cast<size_t>(list)] = std::move(devices);
    Invalidate();
}

void Panel::DrawDevices() {
    const Palette& p = P();
    const DeviceList list = deviceList_;
    const bool microphones = list == DeviceList::Microphones;
    const DevicePriority& priority = microphones ? settings_.microphones : settings_.outputs;
    DrawHeader(T(Text::DevicesTitle));
    float y = BeginScroll(Page::Outputs, kHeaderHeight);

    // The two lists share the page; the button group above them picks one.
    const wchar_t* const lists[] = {T(Text::OutputDevices), T(Text::MicrophonesTab)};
    DrawSegmented({kGutter, y, kWidth - kGutter, y + 36.0f}, kIdDeviceList, lists, static_cast<int>(list),
                  p.secondaryContainer, p.onSecondaryContainer, [this](int index) {
                      deviceList_ = static_cast<DeviceList>(index);
                      scroll_[Page::Outputs] = 0.0f;
                      Invalidate();
                  });
    y += 36.0f + design::kSpace2;

    auto edit = [this, microphones](bool DevicePriority::*flag) {
        return [this, microphones, flag] {
            Edit([microphones, flag](Settings& s) {
                DevicePriority& edited = microphones ? s.microphones : s.outputs;
                edited.*flag = !(edited.*flag);
            });
        };
    };
    y = DrawCard(y,
                 {
                     SwitchItem(kIdSwitchOnLoss, T(microphones ? Text::MicSwitchOnLoss : Text::SwitchOnLoss),
                                priority.switchOnLoss, edit(&DevicePriority::switchOnLoss),
                                T(microphones ? Text::MicSwitchOnLossHint : Text::SwitchOnLossHint)),
                     SwitchItem(kIdReturnToPreferred,
                                T(microphones ? Text::MicReturnToPreferred : Text::ReturnToPreferred),
                                priority.returnToPreferred, edit(&DevicePriority::returnToPreferred),
                                T(microphones ? Text::MicReturnToPreferredHint : Text::ReturnToPreferredHint)),
                 }) +
        design::kSectionGap;

    y = SectionTitle(T(Text::Priority), y);
    y = PageHint(T(microphones ? Text::MicPriorityHint : Text::PriorityHint), y);
    const std::vector<DeviceView>& devices = devices_[static_cast<size_t>(list)];
    if (devices.empty()) {
        y = DrawCard(y, {HintItem(T(microphones ? Text::NoMicrophones : Text::NoPlaybackDevices))});
        EndScroll(Page::Outputs, kHeaderHeight, y);
        return;
    }
    // Excluded devices arrive last; the drag card holds the others, in their order.
    std::vector<const DeviceView*> allowed;
    std::vector<const DeviceView*> excluded;
    for (const DeviceView& device : devices) {
        (device.excluded ? excluded : allowed).push_back(&device);
    }
    // Right of the name: a check on the device in use, then the button that moves the row to the other list.
    const float buttonX = design::kTrailingIconCenter - 32.0f;
    const float checkX = buttonX - design::kIconButtonSize;
    const float nameRight = checkX - design::kSmallIconSize / 2.0f - design::kSpace2;
    if (allowed.empty()) {
        y = DrawCard(y, {HintItem(T(microphones ? Text::AllMicsExcluded : Text::AllOutputsExcluded))});
    } else {
        std::vector<uint32_t> ids;
        for (const DeviceView* device : allowed) {
            ids.push_back(HashId(kKindOutput, device->id));
        }
        // Place in the order, the name and the check; a click uses the device.
        y = DrawDragCard(
            y, kIdOutputList, ids, kRowHeight,
            [this, &p, &allowed, list, buttonX, checkX, nameRight](size_t index, size_t place, const D2D1_RECT_F& row,
                                                                   bool lifted) {
                const DeviceView& device = *allowed[index];
                const float middle = (row.top + row.bottom) / 2.0f;
                const D2D1_COLOR_F accent = device.current || lifted ? p.primary : p.onSurfaceVariant;
                canvas_.Text(std::to_wstring(place + 1), {kInset - design::kSpace1, row.top, kInset + 16.0f, row.bottom},
                             TextStyle::LabelLarge, accent, TextAlign::Center);
                canvas_.Text(device.name, {design::kLeadingTextInset - design::kSpace3, row.top, nameRight, row.bottom},
                             TextStyle::BodyLarge, device.current ? p.primary : p.onSurface);
                if (device.current) {
                    canvas_.Glyph(kGlyphCheck, {checkX, middle}, design::kSmallIconSize, p.primary);
                }
                const std::wstring id = device.id;
                IconButton(HashId(kKindOutputExclude, id), {buttonX, middle}, kGlyphBlocked, p.onSurfaceVariant,
                           [this, list, id] { host_.OnDeviceExclude(list, id, true); });
                Tooltip(HashId(kKindOutputExclude, id), T(Text::TipNeverSwitch));
            },
            [this, list](size_t from, size_t to) { host_.OnDeviceMove(list, from, to); },
            // Runs on release, after this frame: it keeps its own copy of the device IDs.
            [this, list, deviceIds = DeviceIds(allowed)](size_t index) {
                if (index < deviceIds.size()) {
                    host_.OnDeviceChoose(list, deviceIds[index]);
                }
            });
    }

    if (!excluded.empty()) {
        y = SectionTitle(T(Text::NeverSwitchTo), y + design::kSectionGap);
        y = PageHint(T(microphones ? Text::MicNeverSwitchToHint : Text::NeverSwitchToHint), y);
        std::vector<CardRow> rows;
        for (const DeviceView* device : excluded) {
            const uint32_t rowId = HashId(kKindOutput, device->id);
            rows.push_back({kRowHeight,
                            [this, &p, device, list, rowId, buttonX, checkX, nameRight](float top) {
                                const float middle = top + kRowHeight / 2.0f;
                                const std::wstring id = device->id;
                                AddHit({TileRect(top, kRowHeight), rowId,
                                        [this, list, id] { host_.OnDeviceChoose(list, id); }});
                                canvas_.Glyph(kGlyphBlocked, {kInset + 8.0f - design::kSpace1 / 2.0f, middle},
                                              design::kSmallIconSize, p.onSurfaceVariant);
                                canvas_.Text(device->name,
                                             {design::kLeadingTextInset - design::kSpace3, top, nameRight, top + kRowHeight},
                                             TextStyle::BodyLarge, device->current ? p.primary : p.onSurfaceVariant);
                                if (device->current) {
                                    canvas_.Glyph(kGlyphCheck, {checkX, middle}, design::kSmallIconSize, p.primary);
                                }
                                IconButton(HashId(kKindOutputExclude, id), {buttonX, middle}, kGlyphUndo,
                                           p.onSurfaceVariant, [this, list, id] { host_.OnDeviceExclude(list, id, false); });
                                Tooltip(HashId(kKindOutputExclude, id), T(Text::TipSwitchAgain));
                            },
                            rowId});
        }
        y = DrawCard(y, rows);
    }
    EndScroll(Page::Outputs, kHeaderHeight, y);
}

}  // namespace ui
