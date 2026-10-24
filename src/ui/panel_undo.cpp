// Ctrl+Z and Ctrl+Y for the equalizers: the Equalizer tab's channels and the Headset tab's equalizers.

#include <algorithm>

#include "panel.h"
#include "panel_internal.h"

namespace ui {

using namespace detail;

namespace {

constexpr size_t kUndoSteps = 100;
// Wheel notches and keys this close together make one step.
constexpr ULONGLONG kUndoRunMs = 700;

}  // namespace

Panel::UndoStep Panel::ChannelUndoStep(EqChannel channel) {
    UndoStep step;
    step.channel = channel;
    step.eq = EqStateOf(settings_, channel);
    return step;
}

Panel::UndoStep Panel::HeadsetUndoStep() const {
    UndoStep step;
    step.headset = true;
    step.headsetEq = {headset_.eqPreset,   headset_.eqCurve,         headset_.eqCurveKnown,
                      headset_.eqFilters,  headset_.eqFiltersKnown,  headset_.extraEq};
    return step;
}

void Panel::RecordUndo(UndoStep before) {
    const ULONGLONG now = GetTickCount64();
    const UndoStep* last = undo_.empty() ? nullptr : &undo_.back();
    const bool sameTarget = last && last->headset == before.headset && (before.headset || last->channel == before.channel);
    // A press keeps one step for all it changes; without one, a quick run of changes is one step.
    const bool pressed = pressedId_ != 0 || clicking_;
    const bool sameRun = pressed ? undoGesture_ == gesture_ : now - undoTick_ < kUndoRunMs;
    undoGesture_ = pressed ? gesture_ : 0;
    undoTick_ = now;
    redo_.clear();
    if (sameTarget && sameRun) {
        return;
    }
    undo_.push_back(std::move(before));
    if (undo_.size() > kUndoSteps) {
        undo_.erase(undo_.begin());
    }
}

bool Panel::UndoEqualizer(bool redo) {
    const bool headset = page_ == Page::Headset;
    if (!headset && page_ != Page::Equalizer) {
        return false;
    }
    std::vector<UndoStep>& from = redo ? redo_ : undo_;
    std::vector<UndoStep>& to = redo ? undo_ : redo_;
    // Only the steps of the page in view; the other page's stay for it.
    const auto found = std::find_if(from.rbegin(), from.rend(), [headset](const UndoStep& step) {
        return step.headset == headset;
    });
    if (found == from.rend() || (headset && !features_)) {
        return false;
    }
    const UndoStep step = *found;
    from.erase(std::next(found).base());
    // The next change starts a step of its own.
    undoGesture_ = 0;
    undoTick_ = 0;
    if (step.headset) {
        to.push_back(HeadsetUndoStep());
        RestoreHeadsetEq(step.headsetEq);
    } else {
        to.push_back(ChannelUndoStep(step.channel));
        const std::vector<EqChannel> channels = EqChannels();
        if (std::find(channels.begin(), channels.end(), step.channel) != channels.end()) {
            eqChannel_ = step.channel;
        }
        const EqChannel channel = step.channel;
        const EqChannelSettings eq = step.eq;
        Edit([channel, eq](Settings& s) { EqStateOf(s, channel) = eq; });
    }
    Invalidate();
    return true;
}

void Panel::RestoreHeadsetEq(const HeadsetEqSnapshot& before) {
    const EqualizerSpec& eq = features_->equalizer;
    // The custom setting first, then the preset that was chosen; only what differs goes to the headset. Each call
    // updates headset_ before the next one.
    if (eq.bandsCommand) {
        if (eq.Parametric()) {
            if (before.eqFiltersKnown && before.eqFilters != headset_.eqFilters) {
                host_.OnEqualizerFilters(before.eqFilters);
            }
        } else if (before.eqCurveKnown && before.eqCurve != headset_.eqCurve) {
            host_.OnEqualizerCurve(before.eqCurve);
        }
        if (before.eqPreset >= 0 && before.eqPreset != headset_.eqPreset) {
            host_.OnEqualizerPreset(before.eqPreset);
        }
    }
    for (int index = 0; index < kExtraEqualizers; ++index) {
        const auto which = static_cast<ExtraEqualizer>(index);
        if (!features_->Extra(which).bandsCommand) {
            continue;
        }
        const HeadsetState::ExtraEq& was = before.extraEq[index];
        if (was.curveKnown && was.curve != headset_.extraEq[index].curve) {
            host_.OnExtraEqualizerCurve(which, was.curve);
        }
        if (was.preset >= 0 && was.preset != headset_.extraEq[index].preset) {
            host_.OnExtraEqualizerPreset(which, was.preset);
        }
    }
}

void Panel::SetHeadsetPreset(int index) {
    RecordUndo(HeadsetUndoStep());
    host_.OnEqualizerPreset(index);
}

void Panel::SetHeadsetCurve(const EqCurve& curve) {
    RecordUndo(HeadsetUndoStep());
    host_.OnEqualizerCurve(curve);
}

void Panel::SetHeadsetFilters(const EqFilters& filters) {
    RecordUndo(HeadsetUndoStep());
    host_.OnEqualizerFilters(filters);
}

void Panel::SetExtraPreset(ExtraEqualizer which, int index) {
    RecordUndo(HeadsetUndoStep());
    host_.OnExtraEqualizerPreset(which, index);
}

void Panel::SetExtraCurve(ExtraEqualizer which, const EqCurve& curve) {
    RecordUndo(HeadsetUndoStep());
    host_.OnExtraEqualizerCurve(which, curve);
}

}  // namespace ui
