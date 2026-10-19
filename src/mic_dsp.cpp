#include "mic_dsp.h"

#include <algorithm>
#include <cmath>

extern "C" {
#include "../third_party/rnnoise/include/rnnoise.h"
}

namespace {

// RNNoise works on samples in the 16-bit range.
constexpr float kRnnoiseScale = 32768.0f;
constexpr float kGateOpen = 0.6f;       // Voice probability that opens the gate.
constexpr int kGateHoldBlocks = 30;     // 300 ms, so word endings and short pauses pass.
constexpr float kGateClosed = 0.03f;    // About -30 dB while nobody speaks.

struct CompressorPreset {
    float threshold;  // dBFS
    float ratio;
    float makeup;     // dB
};
// Indexed by MicCompressor; Off is never used.
constexpr CompressorPreset kCompressorPresets[kMicCompressorCount] = {
    {0.0f, 1.0f, 0.0f}, {-20.0f, 2.0f, 5.0f}, {-26.0f, 3.0f, 9.0f}, {-32.0f, 5.0f, 14.0f}};
constexpr float kKneeDb = 6.0f;
// Below this level the compressor lowers instead of raising, so the makeup gain does not lift the
// room noise between words.
constexpr float kExpanderDb = -55.0f;
constexpr float kExpanderRatio = 2.0f;
constexpr float kAttackSeconds = 0.005f;
constexpr float kReleaseSeconds = 0.15f;

float Coefficient(float seconds) {
    return 1.0f - std::exp(-1.0f / (seconds * MicProcessor::kRate));
}

}  // namespace

MicProcessor::MicProcessor() : denoise_(rnnoise_create(nullptr)) {}

MicProcessor::~MicProcessor() {
    if (denoise_) {
        rnnoise_destroy(denoise_);
    }
}

void MicProcessor::Run(MicStage stage, float* block, const MicDspSettings& settings) {
    switch (stage) {
    case MicStage::Noise:
        if (settings.noise != NoiseSuppression::Off) {
            Denoise(block, settings.noise == NoiseSuppression::Strong);
        } else {
            gateGain_ = 1.0f;
            gateHold_ = 0;
        }
        break;
    case MicStage::Equalizer:
        eq_.Update(settings.eq);
        eq_.Process(block, kBlock);
        break;
    case MicStage::Compressor:
        if (settings.compressor != MicCompressor::Off) {
            Compress(block, settings.compressor);
        } else {
            envelopeDb_ = -120.0f;
        }
        break;
    case MicStage::Plugins:
        break;
    }
}

void MicProcessor::Process(float* block, const MicDspSettings& settings) {
    for (const MicStage stage : settings.order) {
        Run(stage, block, settings);
    }
}

void MicProcessor::Denoise(float* block, bool gate) {
    if (!denoise_) {
        return;
    }
    float frame[kBlock];
    for (size_t i = 0; i < kBlock; ++i) {
        frame[i] = block[i] * kRnnoiseScale;
    }
    const float voice = rnnoise_process_frame(denoise_, frame, frame);
    float target = 1.0f;
    if (gate) {
        if (voice >= kGateOpen) {
            gateHold_ = kGateHoldBlocks;
        } else if (gateHold_ > 0) {
            --gateHold_;
        }
        target = gateHold_ > 0 ? 1.0f : kGateClosed;
    }
    // Opens within one block, closes over about five, without steps inside a block.
    const float start = gateGain_;
    gateGain_ = target > gateGain_ ? target : gateGain_ + (target - gateGain_) * 0.2f;
    for (size_t i = 0; i < kBlock; ++i) {
        const float gain = start + (gateGain_ - start) * (i + 1) / kBlock;
        block[i] = frame[i] / kRnnoiseScale * gain;
    }
}

void MicProcessor::Compress(float* block, MicCompressor amount) {
    const CompressorPreset& preset = kCompressorPresets[static_cast<int>(amount)];
    static const float attack = Coefficient(kAttackSeconds);
    static const float release = Coefficient(kReleaseSeconds);
    for (size_t i = 0; i < kBlock; ++i) {
        const float levelDb = 20.0f * std::log10(std::fabs(block[i]) + 1e-9f);
        envelopeDb_ += (levelDb - envelopeDb_) * (levelDb > envelopeDb_ ? attack : release);

        // Soft-knee downward compression above the threshold.
        const float over = envelopeDb_ - preset.threshold;
        float reduction = 0.0f;
        if (over >= kKneeDb / 2.0f) {
            reduction = (1.0f / preset.ratio - 1.0f) * over;
        } else if (over > -kKneeDb / 2.0f) {
            const float into = over + kKneeDb / 2.0f;
            reduction = (1.0f / preset.ratio - 1.0f) * into * into / (2.0f * kKneeDb);
        }
        // Downward expansion below the noise floor.
        if (envelopeDb_ < kExpanderDb) {
            reduction -= (kExpanderDb - envelopeDb_) * (kExpanderRatio - 1.0f);
        }
        // The gain follows the envelope, which already has the attack and release.
        block[i] *= std::pow(10.0f, std::max(reduction + preset.makeup, -60.0f) / 20.0f);
    }
}

void MicProcessor::Limit(float* block, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        block[i] = std::clamp(EqProcessor::SoftLimit(block[i]), -1.0f, 1.0f);
    }
}
