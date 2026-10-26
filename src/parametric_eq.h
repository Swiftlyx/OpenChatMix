#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

// Ten-band parametric equalizer for the virtual device mode, modeled on SteelSeries Sonar's
// (filter type, frequency, gain and Q per band, plus a preamp).

enum class FilterType { Peaking, LowShelf, HighShelf, LowPass, HighPass, Notch };
inline constexpr int kFilterTypeCount = 6;

struct EqBand {
    FilterType type = FilterType::Peaking;
    float frequency = 1000.0f;  // Hz
    float gain = 0.0f;          // dB; unused by pass and notch filters.
    float q = 0.707f;
    bool enabled = true;

    bool operator==(const EqBand&) const = default;
};

inline constexpr int kParametricBands = 10;
inline constexpr float kEqMinFrequency = 20.0f;
inline constexpr float kEqMaxFrequency = 20000.0f;
inline constexpr float kEqMaxGain = 12.0f;
inline constexpr float kEqMinQ = 0.1f;
inline constexpr float kEqMaxQ = 10.0f;

// Loudness leveling with the target level it keeps.
enum class SmartVolume { Off, Quiet, Balanced, Loud };
inline constexpr int kSmartVolumeCount = 4;
inline constexpr float kEffectMaxGain = 12.0f;

// One-knob effects after the bands, in the manner of Sonar. Presets do not carry them.
struct EqEffects {
    float bassBoost = 0.0f;     // dB, low shelf at 120 Hz.
    float trebleBoost = 0.0f;   // dB, high shelf at 6 kHz.
    float voiceClarity = 0.0f;  // dB, presence peak at 2.5 kHz.
    SmartVolume smartVolume = SmartVolume::Off;

    bool operator==(const EqEffects&) const = default;
};

struct ParametricEq {
    std::array<EqBand, kParametricBands> bands;
    float preamp = 0.0f;  // dB
    EqEffects effects;

    bool operator==(const ParametricEq&) const = default;
};

// Peaking bands at the usual ten octave centers, all at 0 dB.
ParametricEq FlatEq();
bool UsesGain(FilterType type);
// Takes the bands and preamp of a preset and keeps the effects of target.
void AssignBands(ParametricEq& target, const ParametricEq& preset);
// The filters behind the boost effects (bass, voice clarity, treble), at 0 dB while off.
inline constexpr int kEffectBands = 3;
std::array<EqBand, kEffectBands> EffectBands(const EqEffects& effects);
const wchar_t* SmartVolumeKey(SmartVolume volume);
SmartVolume ParseSmartVolume(const std::wstring& key);

// Gains in dB, within +-maxGain, for a ten-band graphic equalizer with one-octave bands at 32 Hz-16 kHz
// (such as SteelSeries Engine on the headset) that come closest to the response of eq, preamp aside.
std::array<float, kParametricBands> GraphicEqGains(const ParametricEq& eq, float maxGain);

// Normalized biquad coefficients (a0 = 1) after the Audio EQ Cookbook by Robert Bristow-Johnson.
struct Biquad {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
};
Biquad MakeBiquad(const EqBand& band, double sampleRate);

// Magnitude response in dB at a frequency, preamp and boost effects included.
double ResponseDb(const ParametricEq& eq, double frequency, double sampleRate = 48000.0);

// Media and Aux have Game's presets: they carry music and effects rather than voice.
enum class EqChannel { Game, Chat, Mic, Media, Aux };

struct NamedEq {
    std::wstring name;
    ParametricEq eq;
};

// Presets made for OpenChatMix.
std::vector<NamedEq> BuiltinEqPresets(EqChannel channel);
// Presets of a local SteelSeries GG installation: Sonar's, and Engine's game and microphone presets that Sonar
// lacks. Empty without GG.
std::vector<NamedEq> SonarEqPresets(EqChannel channel);
// Names of GG's game presets made for the game at exePath (appName as Windows shows it): those GG links to the
// game's install first, then those whose names match the game's or its file's. Empty without GG.
std::vector<std::wstring> SuggestedEqPresets(const std::wstring& exePath, const std::wstring& appName);

// Preset files (JSON) in %APPDATA%\OpenChatMix\Presets\Parametric.
std::wstring ParametricPresetDirectory();
std::vector<NamedEq> LoadParametricPresets();
std::optional<NamedEq> ReadParametricPreset(const std::wstring& path);
bool WriteParametricPreset(const std::wstring& path, const NamedEq& preset);

// Settings storage: "type,frequency,gain,q,enabled".
std::wstring FormatBand(const EqBand& band);
std::optional<EqBand> ParseBand(const std::wstring& text);
