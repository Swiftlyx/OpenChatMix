// Microphone chain: devices, the processing steps and their order, and VST plugins.

#include <algorithm>
#include <string>
#include <vector>

#include "../reorder.h"
#include "panel.h"
#include "panel_internal.h"

namespace ui {

using namespace detail;

namespace {

constexpr Text kCompressorLabels[kMicCompressorCount] = {Text::OptOff, Text::CompressorLight, Text::CompressorMedium,
                                                         Text::CompressorStrong};
constexpr Text kStageLabels[kMicStageCount] = {Text::NoiseSuppressionLabel, Text::MicEqualizer, Text::CompressorLabel,
                                               Text::Plugins};

std::wstring FileNameOfPath(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

}  // namespace

void Panel::SetMic(const MicView& mic) {
    mic_ = mic;
    Invalidate();
}

void Panel::DrawMicrophone() {
    const Palette& p = P();
    const MicSettings& mic = settings_.mic;
    DrawHeader(T(Text::Microphone));
    float y = BeginScroll(Page::Microphone, kHeaderHeight);

    // The switch, the devices and how the chain runs.
    std::vector<CardRow> chain{
        SwitchItem(
            kIdMicToggle, T(Text::MicProcessing), mic.enabled,
            [this] { Edit([](Settings& s) { s.mic.enabled = !s.mic.enabled; }); }, T(Text::MicProcessingHint)),
    };
    if (mic.enabled) {
        chain.push_back(ValueItem(kIdMicInputRow, T(Text::MicInput),
                                  mic_.inputName.empty() ? T(Text::MicDefaultInput) : mic_.inputName,
                                  [this] { OpenPicker(RoutingSlot::MicInput); }));
        chain.push_back(ValueItem(kIdMicOutputRow, T(Text::MicOutput),
                                  mic_.outputName.empty() ? T(Text::NotSelected) : mic_.outputName,
                                  [this] { OpenPicker(RoutingSlot::MicOutput); }));
        if (!mic_.status.empty()) {
            const float height = HintHeight(mic_.status.c_str()) + design::kHintBottomGap;
            chain.push_back({height, [this, &p, height](float top) {
                                 canvas_.Text(mic_.status,
                                              {kInset, top + design::kHintBottomGap, kWidth - kInset,
                                               top + height - design::kHintBottomGap},
                                              TextStyle::BodySmall, mic_.error ? p.error : p.onSurfaceVariant,
                                              TextAlign::Leading, true);
                             }});
        }
    }
    y = DrawCard(y, chain);
    if (!mic.enabled) {
        EndScroll(Page::Microphone, kHeaderHeight, y);
        return;
    }
    y += design::kGroupGap;

    // Listening shows the level of what apps hear, against 0 dBFS.
    std::vector<CardRow> listening{
        SwitchItem(kIdMicMonitor, T(Text::MicMonitor), mic.monitor,
                   [this] { Edit([](Settings& s) { s.mic.monitor = !s.mic.monitor; }); }, T(Text::MicMonitorHint)),
    };
    if (mic.monitor) {
        listening.push_back(MicLevelItem());
    }
    listening.push_back(SwitchItem(kIdMicKeepLevels, T(Text::MicKeepLevels), mic.keepLevels,
                                   [this] { Edit([](Settings& s) { s.mic.keepLevels = !s.mic.keepLevels; }); },
                                   T(Text::MicKeepLevelsHint)));
    y = DrawCard(y, listening) + design::kGroupGap;

    // The built-in steps.
    const EqChannelSettings& eq = settings_.micEq;
    const std::wstring eqState = !eq.enabled || !settings_.equalizer ? std::wstring(T(Text::OptOff))
                                 : eq.preset.empty()                 ? std::wstring(T(Text::EqCustom))
                                                                     : eq.preset;
    y = DrawCard(
            y,
            {
                ChoiceItem(
                    kIdMicNoise, T(Text::NoiseSuppressionLabel),
                    {T(Text::OptOff), T(Text::NoiseStandard), T(Text::NoiseStrong)}, static_cast<int>(mic.noise),
                    [this](int index) {
                        Edit([index](Settings& s) { s.mic.noise = static_cast<NoiseSuppression>(index); });
                    },
                    T(Text::NoiseHint)),
                ChoiceItem(
                    kIdMicCompressor, T(Text::CompressorLabel),
                    {T(kCompressorLabels[0]), T(kCompressorLabels[1]), T(kCompressorLabels[2]),
                     T(kCompressorLabels[3])},
                    static_cast<int>(mic.compressor),
                    [this](int i) { Edit([i](Settings& s) { s.mic.compressor = static_cast<MicCompressor>(i); }); },
                    T(Text::CompressorHint)),
                NavItem(kIdMicEqEdit, kGlyphEqualizer, p.primary, T(Text::MicEqualizer), eqState,
                        [this] {
                            eqChannel_ = EqChannel::Mic;
                            eqBand_ = 0;
                            if (!settings_.equalizer) {
                                Edit([](Settings& s) { s.equalizer = true; });
                            }
                            GoTo(Page::Equalizer);
                        }),
            }) +
        design::kSectionGap;

    // The order of the steps, dragged into place.
    y = SectionTitle(T(Text::ProcessingOrder), y);
    y = PageHint(T(Text::ProcessingOrderHint), y);
    std::vector<uint32_t> stageIds;
    for (const MicStage stage : mic.order) {
        stageIds.push_back(HashId(kKindStage, std::to_wstring(static_cast<int>(stage))));
    }
    const MicStageOrder order = mic.order;
    y = DrawDragCard(
            y, kIdChainList, stageIds, kRowHeight,
            [this, &p, order](size_t index, size_t place, const D2D1_RECT_F& row, bool lifted) {
                canvas_.Text(std::to_wstring(place + 1),
                             {kInset - design::kSpace1, row.top, kInset + 16.0f, row.bottom}, TextStyle::LabelLarge,
                             lifted ? p.primary : p.onSurfaceVariant, TextAlign::Center);
                canvas_.Text(T(kStageLabels[static_cast<int>(order[index])]),
                             {design::kLeadingTextInset - design::kSpace3, row.top,
                              design::kTrailingIconCenter - design::kSpace4, row.bottom},
                             TextStyle::BodyLarge, p.onSurface);
            },
            [this](size_t from, size_t to) { Edit([from, to](Settings& s) { MoveItem(s.mic.order, from, to); }); },
            [](size_t) {}) +
        design::kSectionGap;

    y = DrawMicPlugins(y);
    EndScroll(Page::Microphone, kHeaderHeight, y);
}

// DeepFilterNet's maximum reduction steps in 6 dB up to 60 dB, then unlimited.
constexpr int kDeepFilterStepDb = 6;
constexpr int kDeepFilterMaxStepsDb = 60;

float Panel::DrawMicPlugins(float y) {
    const Palette& p = P();
    y = SectionTitle(T(Text::Plugins), y);
    y = PageHint(T(Text::PluginsHint), y);

    const std::vector<MicPlugin>& plugins = settings_.mic.plugins;
    if (!plugins.empty()) {
        std::vector<uint32_t> ids;
        for (const MicPlugin& plugin : plugins) {
            ids.push_back(HashId(kKindPlugin, plugin.id));
        }
        y = DrawDragCard(
                y, kIdPluginList, ids, design::kTwoLineRowHeight,
                [this, &p, &plugins](size_t index, size_t, const D2D1_RECT_F& row, bool) {
                    const MicPlugin& plugin = plugins[index];
                    const MicPluginView view = index < mic_.plugins.size() ? mic_.plugins[index] : MicPluginView{};
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
                               kGlyphDelete, p.onSurfaceVariant, [this, index] { host_.OnMicPluginRemove(index); });
                    Tooltip(PartId(id, 2), T(Text::TipRemovePlugin));
                    const D2D1_RECT_F track{switchLeft, middle - design::kSwitchHeight / 2.0f, switchRight,
                                            middle + design::kSwitchHeight / 2.0f};
                    DrawSwitch(track, plugin.enabled, PartId(id, 3));
                    AddHit({Inset(track, -design::kSpace1, -design::kSpace2), PartId(id, 3), [this, index] {
                                Edit([index](Settings& s) {
                                    if (index < s.mic.plugins.size()) {
                                        s.mic.plugins[index].enabled = !s.mic.plugins[index].enabled;
                                    }
                                });
                            }});
                },
                [this](size_t from, size_t to) { host_.OnMicPluginMove(from, to); },
                [this](size_t index) {
                    if (index < mic_.plugins.size() && mic_.plugins[index].loaded && mic_.plugins[index].hasEditor) {
                        host_.OnMicPluginEditor(index);
                    }
                }) +
            design::kGroupGap;
    }
    TextButton(kIdAddPlugin, {design::kPageGutter, y, kWidth - design::kPageGutter, y + design::kButtonHeight},
               kGlyphAdd, T(Text::AddPlugin), true, [this] { RunFileDialog(FileDialog::Plugin); });
    y += design::kButtonHeight;

    // DeepFilterNet has no window of its own: its settings follow the list.
    for (size_t index = 0; index < plugins.size() && index < mic_.plugins.size(); ++index) {
        if (!mic_.plugins[index].deepFilter) {
            continue;
        }
        const DeepFilterSettings filter = *mic_.plugins[index].deepFilter;
        const std::wstring& key = plugins[index].id;
        y = SectionTitle(mic_.plugins[index].name.c_str(), y + design::kSectionGap);
        auto apply = [this, index](DeepFilterSettings changed) { host_.OnMicPluginDeepFilter(index, changed); };
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
                    T(Text::DeepFilterReductionHint)),
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
