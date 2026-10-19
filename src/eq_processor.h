#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#include "parametric_eq.h"

// Equalizer of one stream: a cascade of biquads per channel (transposed direct form II) and a soft
// limiter, since presets can boost by more than 10 dB.
class EqProcessor {
public:
    EqProcessor(uint32_t rate, int channels) : rate_(rate), channels_(channels) {}

    // Recomputes the coefficients when the parameters change; filter states carry over to avoid clicks.
    void Update(const std::shared_ptr<const ParametricEq>& eq) {
        if (eq == current_) {
            return;
        }
        current_ = eq;
        std::array<bool, kStages> wasActive{};
        for (const Stage& stage : active_) {
            wasActive[stage.band] = true;
        }
        active_.clear();
        if (!eq) {
            return;
        }
        if (state_.empty()) {
            state_.assign(static_cast<size_t>(kStages) * channels_ * 2, 0.0);
        }
        preamp_ = static_cast<float>(std::pow(10.0, eq->preamp / 20.0));
        smartVolume_ = eq->effects.smartVolume;
        for (int band = 0; band < kParametricBands; ++band) {
            const EqBand& settings = eq->bands[band];
            if (!settings.enabled || (UsesGain(settings.type) && std::fabs(settings.gain) < 0.01f)) {
                continue;
            }
            // A band that was bypassed starts from silence, not from its state of an earlier run.
            if (!wasActive[band]) {
                std::fill_n(state_.begin() + static_cast<size_t>(band) * channels_ * 2, channels_ * 2, 0.0);
            }
            active_.push_back({MakeBiquad(settings, rate_), band});
        }
        // Effects use fixed state slots after the bands; a toggled effect starts from silence.
        const auto effects = EffectBands(eq->effects);
        for (int index = 0; index < kEffectBands; ++index) {
            const int slot = kParametricBands + index;
            if (effects[index].gain < 0.01f) {
                continue;
            }
            if (!wasActive[slot]) {
                std::fill_n(state_.begin() + static_cast<size_t>(slot) * channels_ * 2, channels_ * 2, 0.0);
            }
            active_.push_back({MakeBiquad(effects[index], rate_), slot});
        }
    }

    void Process(float* samples, size_t frames) {
        if (!current_) {
            return;
        }
        for (size_t frame = 0; frame < frames; ++frame) {
            for (int channel = 0; channel < channels_; ++channel) {
                double x = samples[frame * channels_ + channel] * preamp_;
                for (const Stage& stage : active_) {
                    double* z = &state_[(static_cast<size_t>(stage.band) * channels_ + channel) * 2];
                    const Biquad& f = stage.filter;
                    const double y = f.b0 * x + z[0];
                    z[0] = f.b1 * x - f.a1 * y + z[1];
                    z[1] = f.b2 * x - f.a2 * y;
                    x = y;
                }
                samples[frame * channels_ + channel] = static_cast<float>(x);
            }
        }
        if (smartVolume_ != SmartVolume::Off) {
            Level(samples, frames);
        }
        for (size_t i = 0; i < frames * channels_; ++i) {
            samples[i] = SoftLimit(samples[i]);
        }
    }

private:
    struct Stage {
        Biquad filter;
        int band;  // State slot: bands first, then effects.
    };
    static constexpr int kStages = kParametricBands + kEffectBands;

    // Smart volume: follows the loudness over ~0.4 s and moves the gain toward the target level,
    // quickly down and slowly up. Near-silence holds the gain, so noise is not raised.
    void Level(float* samples, size_t frames) {
        constexpr float kTargets[kSmartVolumeCount] = {0.0f, -26.0f, -21.0f, -16.0f};
        constexpr float kMaxBoost = 9.0f;
        constexpr float kMaxCut = -12.0f;
        constexpr float kSilence = -55.0f;
        const size_t count = frames * channels_;
        if (count == 0) {
            return;
        }
        double energy = 0.0;
        for (size_t i = 0; i < count; ++i) {
            energy += static_cast<double>(samples[i]) * samples[i];
        }
        const float seconds = static_cast<float>(frames) / rate_;
        loudness_ += (static_cast<float>(energy / count) - loudness_) * (1.0f - std::exp(-seconds / 0.4f));
        const float levelDb = 10.0f * std::log10(loudness_ + 1e-12f);
        if (levelDb > kSilence) {
            const float wanted = std::clamp(kTargets[static_cast<int>(smartVolume_)] - levelDb, kMaxCut, kMaxBoost);
            const float time = wanted < gainDb_ ? 0.15f : 2.5f;
            gainDb_ += (wanted - gainDb_) * (1.0f - std::exp(-seconds / time));
        }
        // The gain ramps across the block to avoid steps.
        const float start = gain_;
        gain_ = std::pow(10.0f, gainDb_ / 20.0f);
        for (size_t frame = 0; frame < frames; ++frame) {
            const float gain = start + (gain_ - start) * (frame + 1) / frames;
            for (int channel = 0; channel < channels_; ++channel) {
                samples[frame * channels_ + channel] *= gain;
            }
        }
    }

public:
    // Transparent below -1 dBFS, then approaches full scale smoothly instead of clipping.
    static float SoftLimit(float x) {
        constexpr float kKnee = 0.89f;
        const float magnitude = std::fabs(x);
        if (magnitude <= kKnee) {
            return x;
        }
        const float limited = kKnee + (1.0f - kKnee) * std::tanh((magnitude - kKnee) / (1.0f - kKnee));
        return x < 0.0f ? -limited : limited;
    }

private:
    uint32_t rate_;
    int channels_;
    std::shared_ptr<const ParametricEq> current_;
    std::vector<Stage> active_;
    std::vector<double> state_;  // z1, z2 per stage slot and channel.
    float preamp_ = 1.0f;
    SmartVolume smartVolume_ = SmartVolume::Off;
    float loudness_ = 0.0f;  // Mean square.
    float gainDb_ = 0.0f;
    float gain_ = 1.0f;
};

