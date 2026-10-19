#pragma once

#include <cstddef>
#include <array>
#include <cstdint>
#include <memory>

#include "eq_processor.h"
#include "parametric_eq.h"

struct DenoiseState;

// Order matches the panel.
enum class NoiseSuppression {
    Off,
    Standard,  // RNNoise removes steady and most sudden noise.
    Strong,    // Additionally closes a gate while its voice detector hears no speech.
};
inline constexpr int kNoiseSuppressionCount = 3;

// Brings quiet speech up and loud speech down. Order matches the panel.
enum class MicCompressor { Off, Light, Medium, Strong };
inline constexpr int kMicCompressorCount = 4;

// The steps of the microphone chain; the user chooses their order.
enum class MicStage { Noise, Equalizer, Compressor, Plugins };
inline constexpr int kMicStageCount = 4;
using MicStageOrder = std::array<MicStage, kMicStageCount>;
inline constexpr MicStageOrder kDefaultMicOrder = {MicStage::Noise, MicStage::Equalizer, MicStage::Compressor,
                                                   MicStage::Plugins};

struct MicDspSettings {
    NoiseSuppression noise = NoiseSuppression::Off;
    MicCompressor compressor = MicCompressor::Off;
    std::shared_ptr<const ParametricEq> eq;  // Null bypasses the equalizer.
    MicStageOrder order = kDefaultMicOrder;
};

// The built-in microphone effects on mono blocks of 10 ms at 48 kHz, the frame RNNoise works on:
// noise suppression, equalizer and compressor. Not thread-safe; used by one audio thread.
class MicProcessor {
public:
    static constexpr uint32_t kRate = 48000;
    static constexpr size_t kBlock = 480;

    MicProcessor();
    ~MicProcessor();
    MicProcessor(const MicProcessor&) = delete;
    MicProcessor& operator=(const MicProcessor&) = delete;

    // Runs one built-in step on kBlock samples in place; MicStage::Plugins is the caller's.
    void Run(MicStage stage, float* block, const MicDspSettings& settings);
    // All built-in steps in the chosen order.
    void Process(float* block, const MicDspSettings& settings);
    // Keeps the final signal below full scale; runs after any plugins.
    static void Limit(float* block, size_t count);

private:
    void Denoise(float* block, bool gate);
    void Compress(float* block, MicCompressor amount);

    DenoiseState* denoise_ = nullptr;
    EqProcessor eq_{kRate, 1};
    float gateGain_ = 1.0f;
    int gateHold_ = 0;           // Blocks the gate stays open after the last speech.
    float envelopeDb_ = -120.0f;  // Compressor level detector.
};
