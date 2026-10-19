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
bool WritePresetGains(const std::wstring& path, const std::wstring& name, const std::array<float, kEqBands>& gainsDb);
// Gives the preset a new name; its file is renamed to match.
bool RenamePreset(const UserPreset& preset, const std::wstring& name);
// Saves a copy under another name.
bool CopyPreset(const UserPreset& preset, const std::wstring& name);
// Moves the preset's file to the Recycle Bin.
bool DeletePreset(const UserPreset& preset);
// Copies a preset file into PresetDirectory(); returns the stored copy.
std::optional<UserPreset> ImportPresetFile(const std::wstring& source);
// Rounds to 0.5 dB and clamps to the device range.
EqCurve PresetCurve(const UserPreset& preset, int8_t range);
