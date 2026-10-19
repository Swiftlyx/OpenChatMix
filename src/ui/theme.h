#pragma once

#include <d2d1.h>

#include <cstdint>
#include <optional>

#include "../appearance.h"

namespace ui {

// Accent colors are soft (pastel), a little calmer than Material's tonal spot scheme: no seed is used
// with more OKLCH chroma than this, so even a neon pick turns into a calm color.
inline constexpr float kMaxSeedChroma = 0.10f;

// Material 3 color roles. Primary is the Game side, tertiary the Chat side.
struct Palette {
    bool dark;
    D2D1_COLOR_F primary;
    D2D1_COLOR_F onPrimary;
    D2D1_COLOR_F primaryContainer;
    D2D1_COLOR_F onPrimaryContainer;
    D2D1_COLOR_F secondary;  // Keyboard focus ring.
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
    // Opacity of the page surface over the acrylic or mica backdrop, and of tiles over the page.
    // Higher contrast levels let less of the backdrop through.
    float surfaceAlpha;
    float tileAlpha;
};

// Builds a scheme from a seed color (0xRRGGBB) for Game and, optionally, one for Chat; without it Chat
// is paired with the seed automatically. Roles are fixed tones of palettes derived from the seeds, so
// text contrast holds for any choice: tone is CIE L*, hue and chroma come from OKLCH. A chosen Chat
// color is kept soft and at least 60 degrees of hue from a colorful Game side.
Palette MakePalette(uint32_t seed, bool dark, std::optional<uint32_t> chatSeed = std::nullopt,
                    ContrastLevel contrast = ContrastLevel::Standard);

// Theme used by every window; ThemePalette() follows the mode and the system app mode.
void SetTheme(ThemeMode mode, uint32_t seed, std::optional<uint32_t> chatSeed = std::nullopt,
              ContrastLevel contrast = ContrastLevel::Standard);
const Palette& ThemePalette();
bool ThemeIsDark();

// Follows Settings > Personalization > Colors > app mode.
bool SystemPrefersDark();
// Settings > Personalization > Colors > accent color, 0xRRGGBB.
uint32_t WindowsAccentColor();
// The color a scheme built from seed uses for its primary role, e.g. for swatches.
D2D1_COLOR_F SeedSwatch(uint32_t seed, bool dark);
// The same for the Chat side as MakePalette() makes it next to gameSeed, after its limits.
D2D1_COLOR_F ChatSwatch(uint32_t gameSeed, std::optional<uint32_t> chatSeed, bool dark);
// That Chat color as a seed (0xRRGGBB): the chosen one after its limits, or the automatic pairing.
uint32_t EffectiveChatSeed(uint32_t gameSeed, std::optional<uint32_t> chatSeed);
// Seed from OKLCH hue (degrees) and chroma, as edited in the color picker, and back.
uint32_t SeedFromHueChroma(float hueDegrees, float chroma);
void HueChromaOf(uint32_t seed, float& hueDegrees, float& chroma);
// A color of the hue at a fixed lightness, for drawing hue and saturation tracks.
D2D1_COLOR_F HueChromaColor(float hueDegrees, float chroma, bool dark);
// Battery color for a charge of 0..1: red when empty, through yellow, to green when full, smoothly
// and at one tone, light enough for a dark background or dark enough for a light one.
D2D1_COLOR_F BatteryColor(float charge, bool darkBackground);

D2D1_COLOR_F Alpha(D2D1_COLOR_F color, float alpha);
D2D1_COLOR_F Mix(D2D1_COLOR_F from, D2D1_COLOR_F to, float t);

}  // namespace ui
