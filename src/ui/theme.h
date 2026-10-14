#pragma once

#include <d2d1.h>

#include <cstdint>

#include "../appearance.h"

namespace ui {

// Material 3 color roles. Primary is the Game side, tertiary the Chat side.
struct Palette {
    bool dark;
    D2D1_COLOR_F primary;
    D2D1_COLOR_F onPrimary;
    D2D1_COLOR_F primaryContainer;
    D2D1_COLOR_F onPrimaryContainer;
    D2D1_COLOR_F secondaryContainer;
    D2D1_COLOR_F onSecondaryContainer;
    D2D1_COLOR_F tertiary;
    D2D1_COLOR_F onTertiary;
    D2D1_COLOR_F tertiaryContainer;
    D2D1_COLOR_F onTertiaryContainer;
    D2D1_COLOR_F error;
    D2D1_COLOR_F surface;
    D2D1_COLOR_F surfaceContainer;
    D2D1_COLOR_F surfaceContainerHigh;
    D2D1_COLOR_F surfaceContainerHighest;
    D2D1_COLOR_F onSurface;
    D2D1_COLOR_F onSurfaceVariant;
    D2D1_COLOR_F outline;
    D2D1_COLOR_F outlineVariant;
    D2D1_COLOR_F inverseSurface;
    D2D1_COLOR_F inverseOnSurface;
    D2D1_COLOR_F inversePrimary;
};

// Builds a scheme from a seed color (0xRRGGBB). Roles are fixed tones of palettes derived from the
// seed hue, so text contrast holds for any seed: tone is CIE L*, hue and chroma come from OKLCH.
Palette MakePalette(uint32_t seed, bool dark);

// Theme used by every window; ThemePalette() follows the mode and the system app mode.
void SetTheme(ThemeMode mode, uint32_t seed);
const Palette& ThemePalette();
bool ThemeIsDark();

// Follows Settings > Personalization > Colors > app mode.
bool SystemPrefersDark();
// Settings > Personalization > Colors > accent color, 0xRRGGBB.
uint32_t WindowsAccentColor();
// The color a scheme built from seed uses for its primary role, e.g. for swatches.
D2D1_COLOR_F SeedSwatch(uint32_t seed, bool dark);
// Seed from OKLCH hue (degrees) and chroma, as edited in the color picker, and back.
uint32_t SeedFromHueChroma(float hueDegrees, float chroma);
void HueChromaOf(uint32_t seed, float& hueDegrees, float& chroma);
// A color of the hue at a fixed lightness, for drawing hue and saturation tracks.
D2D1_COLOR_F HueChromaColor(float hueDegrees, float chroma, bool dark);
D2D1_COLOR_F RgbColor(uint32_t rgb);

D2D1_COLOR_F Alpha(D2D1_COLOR_F color, float alpha);
D2D1_COLOR_F Mix(D2D1_COLOR_F from, D2D1_COLOR_F to, float t);

}  // namespace ui
