#pragma once

#include <cstdint>

// Appearance choices shared by the settings and the UI.

enum class ThemeMode { System, Light, Dark };

enum class Backdrop { Acrylic, Mica, Solid };

enum class TrayStyle { Color, Monochrome };

// Seed color of the default theme (the OpenChatMix orange), 0xRRGGBB.
constexpr uint32_t kDefaultAccent = 0xF07A2A;
