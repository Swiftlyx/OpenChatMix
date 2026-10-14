#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "headset.h"

// User equalizer presets stored as small JSON files:
// {"app": "OpenChatMix", "type": "equalizer", "name": "...", "gainsDb": [10 numbers]}
struct UserPreset {
    std::wstring name;
    std::wstring path;
    std::array<float, kEqBands> gainsDb{};
};

// <config directory>\Presets, created on demand.
std::wstring PresetDirectory();
std::vector<UserPreset> LoadUserPresets();
std::optional<UserPreset> ReadPresetFile(const std::wstring& path);
bool WritePresetFile(const std::wstring& path, const std::wstring& name, const EqCurve& curve);
// Copies a preset file into PresetDirectory(); returns the stored copy.
std::optional<UserPreset> ImportPresetFile(const std::wstring& source);
// Rounds to 0.5 dB and clamps to the device range.
EqCurve PresetCurve(const UserPreset& preset, int8_t range);
