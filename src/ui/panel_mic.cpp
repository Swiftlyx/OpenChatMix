// Microphone chain: devices, the processing steps and their order, and the plugin list.

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

    y = DrawPlugins(y, EqChannel::Mic);
    EndScroll(Page::Microphone, kHeaderHeight, y);
}

}  // namespace ui
