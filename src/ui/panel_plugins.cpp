// Plugin lists: the microphone chain's on its page, each virtual device channel's on the Equalizer page.

#include <algorithm>
#include <string>
#include <vector>

#include "panel.h"
#include "panel_internal.h"

namespace ui {

using namespace detail;

namespace {

// DeepFilterNet's maximum reduction steps in 6 dB up to 60 dB, then unlimited.
constexpr int kDeepFilterStepDb = 6;
constexpr int kDeepFilterMaxStepsDb = 60;

std::wstring FileNameOfPath(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

}  // namespace

void Panel::SetPlugins(EqChannel channel, const PluginChainView& view) {
    pluginChains_[static_cast<size_t>(channel)] = view;
    Invalidate();
}

float Panel::DrawPlugins(float y, EqChannel channel) {
    const Palette& p = P();
    const bool mic = channel == EqChannel::Mic;
    const PluginChainView& chain = pluginChains_[static_cast<size_t>(channel)];
    // Every chain's list and button keep ids of their own, so their animations stay apart.
    const std::wstring chainKey = L"#chain" + std::to_wstring(static_cast<int>(channel));
    y = SectionTitle(T(Text::Plugins), y);
    std::wstring hint = T(mic ? Text::PluginsHint : Text::ChannelPluginsHint);
    if (channel == EqChannel::Chat) {
        hint += std::wstring(L" ") + T(Text::ChatPluginsHint);
    }
    if (!mic && chain.latencyMs > 0) {
        hint += std::wstring(L" ") + T(Text::PluginsLatency) + L": ~" + std::to_wstring(chain.latencyMs) + L" " +
                T(Text::MillisecondsShort) + L".";
    }
    y = PageHint(hint.c_str(), y);

    const std::vector<ChainPlugin>& plugins = PluginsOf(settings_, channel);
    if (!plugins.empty()) {
        std::vector<uint32_t> ids;
        for (const ChainPlugin& plugin : plugins) {
            ids.push_back(HashId(kKindPlugin, plugin.id));
        }
        y = DrawDragCard(
                y, mic ? static_cast<uint32_t>(kIdPluginList) : HashId(kKindPlugin, chainKey + L"/list"), ids,
                design::kTwoLineRowHeight,
                [this, &p, &plugins, &chain, channel](size_t index, size_t, const D2D1_RECT_F& row, bool) {
                    const ChainPlugin& plugin = plugins[index];
                    const PluginView view = index < chain.plugins.size() ? chain.plugins[index] : PluginView{};
                    const uint32_t id = HashId(kKindPlugin, plugin.id);
                    const float middle = (row.top + row.bottom) / 2.0f;
                    canvas_.Glyph(view.loaded && view.hasEditor ? kGlyphOpenWindow : kGlyphPlugin,
                                  {design::kLeadingIconCenter, middle}, design::kIconSize,
                                  view.loaded ? p.primary : p.error);
                    // Remove and the on/off switch left of the drag handle.
                    const float switchRight = design::kTrailingIconCenter - design::kIconSize / 2.0f - design::kSpace3;
                    const float switchLeft = switchRight - design::kSwitchWidth;
                    const float textRight = switchLeft - design::kIconButtonSize - design::kSpace2;
                    const std::wstring name = view.name.empty() ? FileNameOfPath(plugin.path) : view.name;
                    canvas_.Text(name, {design::kLeadingTextInset, row.top + 9.0f, textRight, row.top + 31.0f},
                                 TextStyle::BodyLarge, plugin.enabled ? p.onSurface : p.onSurfaceVariant);
                    const std::wstring detail = view.loaded
                                                    ? FileNameOfPath(plugin.path)
                                                    : FileNameOfPath(plugin.path) + L" · " + T(Text::PluginNotLoaded);
                    canvas_.Text(detail, {design::kLeadingTextInset, row.top + 31.0f, textRight, row.top + 50.0f},
                                 TextStyle::BodySmall, view.loaded ? p.onSurfaceVariant : p.error);
                    IconButton(PartId(id, 2), {switchLeft - design::kIconButtonSize / 2.0f - design::kSpace1, middle},
                               kGlyphDelete, p.onSurfaceVariant,
                               [this, channel, index] { host_.OnPluginRemove(channel, index); });
                    Tooltip(PartId(id, 2), T(Text::TipRemovePlugin));
                    const D2D1_RECT_F track{switchLeft, middle - design::kSwitchHeight / 2.0f, switchRight,
                                            middle + design::kSwitchHeight / 2.0f};
                    DrawSwitch(track, plugin.enabled, PartId(id, 3));
                    AddHit({Inset(track, -design::kSpace1, -design::kSpace2), PartId(id, 3), [this, channel, index] {
                                Edit([channel, index](Settings& s) {
                                    std::vector<ChainPlugin>& list = PluginsOf(s, channel);
                                    if (index < list.size()) {
                                        list[index].enabled = !list[index].enabled;
                                    }
                                });
                            }});
                },
                [this, channel](size_t from, size_t to) { host_.OnPluginMove(channel, from, to); },
                [this, &chain, channel](size_t index) {
                    if (index < chain.plugins.size() && chain.plugins[index].loaded && chain.plugins[index].hasEditor) {
                        host_.OnPluginEditor(channel, index);
                    }
                }) +
            design::kGroupGap;
    }
    TextButton(mic ? static_cast<uint32_t>(kIdAddPlugin) : HashId(kKindPlugin, chainKey + L"/add"),
               {design::kPageGutter, y, kWidth - design::kPageGutter, y + design::kButtonHeight}, kGlyphAdd,
               T(Text::AddPlugin), true, [this, channel] {
                   pluginDialog_ = channel;
                   RunFileDialog(FileDialog::Plugin);
               });
    y += design::kButtonHeight;

    // DeepFilterNet has no window of its own: its settings follow the list.
    for (size_t index = 0; index < plugins.size() && index < chain.plugins.size(); ++index) {
        if (!chain.plugins[index].deepFilter) {
            continue;
        }
        const DeepFilterSettings filter = *chain.plugins[index].deepFilter;
        const std::wstring& key = plugins[index].id;
        y = SectionTitle(chain.plugins[index].name.c_str(), y + design::kSectionGap);
        auto apply = [this, channel, index](DeepFilterSettings changed) {
            host_.OnPluginDeepFilter(channel, index, changed);
        };
        const int reduction = filter.maxReductionDb;
        const bool unlimited = reduction >= DeepFilterSettings::kUnlimited;
        y = DrawCard(
            y,
            {
                NumberItem(
                    HashId(kKindPlugin, key + L"/reduction"), T(Text::DeepFilterReduction),
                    unlimited ? std::wstring(T(Text::DeepFilterUnlimited)) : std::to_wstring(reduction) + L" dB",
                    reduction > kDeepFilterStepDb, !unlimited,
                    [filter, apply, reduction, unlimited] {
                        DeepFilterSettings changed = filter;
                        changed.maxReductionDb = unlimited ? kDeepFilterMaxStepsDb : reduction - kDeepFilterStepDb;
                        apply(changed);
                    },
                    [filter, apply, reduction] {
                        DeepFilterSettings changed = filter;
                        changed.maxReductionDb = reduction >= kDeepFilterMaxStepsDb ? DeepFilterSettings::kUnlimited
                                                                                    : reduction + kDeepFilterStepDb;
                        apply(changed);
                    },
                    [filter, apply](const std::wstring& text) {
                        const int value = _wtoi(text.c_str());
                        if (value <= 0) {
                            return false;
                        }
                        DeepFilterSettings changed = filter;
                        changed.maxReductionDb = value > kDeepFilterMaxStepsDb
                                                     ? DeepFilterSettings::kUnlimited
                                                     : std::max(value, kDeepFilterStepDb);
                        apply(changed);
                        return true;
                    },
                    T(mic ? Text::DeepFilterReductionHint : Text::DeepFilterChannelHint)),
                SwitchItem(HashId(kKindPlugin, key + L"/post-filter"), T(Text::DeepFilterPostFilter), filter.postFilter,
                           [filter, apply] {
                               DeepFilterSettings changed = filter;
                               changed.postFilter = !changed.postFilter;
                               apply(changed);
                           },
                           T(Text::DeepFilterPostFilterHint)),
            });
    }
    return y;
}

}  // namespace ui
