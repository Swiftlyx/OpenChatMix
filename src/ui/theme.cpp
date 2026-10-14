#include "theme.h"

#include <windows.h>

#include <dwmapi.h>

#include <algorithm>
#include <cmath>

namespace ui {
namespace {

constexpr double kPi = 3.14159265358979323846;

struct Linear {
    double r, g, b;
};

struct OkLab {
    double L, a, b;
};

double ToLinear(double c) {
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double FromLinear(double c) {
    return c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
}

// OKLab after Björn Ottosson's published matrices.
OkLab ToOkLab(Linear c) {
    const double l = std::cbrt(0.4122214708 * c.r + 0.5363325363 * c.g + 0.0514459929 * c.b);
    const double m = std::cbrt(0.2119034982 * c.r + 0.6806995451 * c.g + 0.1073969566 * c.b);
    const double s = std::cbrt(0.0883024619 * c.r + 0.2817188376 * c.g + 0.6299787005 * c.b);
    return {0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
            1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
            0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s};
}

Linear FromOkLab(OkLab c) {
    const double l = std::pow(c.L + 0.3963377774 * c.a + 0.2158037573 * c.b, 3.0);
    const double m = std::pow(c.L - 0.1055613458 * c.a - 0.0638541728 * c.b, 3.0);
    const double s = std::pow(c.L - 0.0894841775 * c.a - 1.2914855480 * c.b, 3.0);
    return {4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
            -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
            -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s};
}

double Luminance(Linear c) {
    return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b;
}

// CIE L* (Material "tone") to relative luminance.
double ToneToY(double tone) {
    const double f = (tone + 16.0) / 116.0;
    return tone > 8.0 ? f * f * f : tone / 903.2962962962963;
}

bool InGamut(Linear c) {
    constexpr double kEpsilon = 1e-6;
    return c.r >= -kEpsilon && c.r <= 1.0 + kEpsilon && c.g >= -kEpsilon && c.g <= 1.0 + kEpsilon &&
           c.b >= -kEpsilon && c.b <= 1.0 + kEpsilon;
}

D2D1_COLOR_F ToColor(Linear c) {
    auto channel = [](double v) { return static_cast<float>(std::clamp(FromLinear(std::clamp(v, 0.0, 1.0)), 0.0, 1.0)); };
    return {channel(c.r), channel(c.g), channel(c.b), 1.0f};
}

// The color with luminance of the given tone, the hue, and the largest chroma up to the requested
// one that still fits sRGB.
D2D1_COLOR_F ToneColor(double hue, double chroma, double tone) {
    if (tone <= 0.0) {
        return {0.0f, 0.0f, 0.0f, 1.0f};
    }
    if (tone >= 100.0) {
        return {1.0f, 1.0f, 1.0f, 1.0f};
    }
    const double targetY = ToneToY(tone);
    const double cosHue = std::cos(hue);
    const double sinHue = std::sin(hue);
    // Luminance grows with OKLab L at a fixed hue and chroma, so bisection finds the tone.
    auto solve = [&](double c, Linear& out) {
        double low = 0.0;
        double high = 1.0;
        for (int i = 0; i < 40; ++i) {
            const double mid = (low + high) / 2.0;
            if (Luminance(FromOkLab({mid, c * cosHue, c * sinHue})) < targetY) {
                low = mid;
            } else {
                high = mid;
            }
        }
        out = FromOkLab({(low + high) / 2.0, c * cosHue, c * sinHue});
        return InGamut(out);
    };
    Linear result{};
    if (!solve(chroma, result)) {
        double low = 0.0;
        double high = chroma;
        solve(0.0, result);
        for (int i = 0; i < 24; ++i) {
            const double mid = (low + high) / 2.0;
            Linear candidate{};
            if (solve(mid, candidate)) {
                low = mid;
                result = candidate;
            } else {
                high = mid;
            }
        }
    }
    return ToColor(result);
}

struct TonalPalette {
    double hue;
    double chroma;
    D2D1_COLOR_F operator()(double tone) const { return ToneColor(hue, chroma, tone); }
};

struct SeedLch {
    double chroma;
    double hue;
};

SeedLch Analyze(uint32_t seed) {
    const OkLab lab = ToOkLab({ToLinear(((seed >> 16) & 0xFF) / 255.0), ToLinear(((seed >> 8) & 0xFF) / 255.0),
                               ToLinear((seed & 0xFF) / 255.0)});
    return {std::hypot(lab.a, lab.b), std::atan2(lab.b, lab.a)};
}

ThemeMode g_mode = ThemeMode::System;
Palette g_palettes[2] = {MakePalette(kDefaultAccent, false), MakePalette(kDefaultAccent, true)};

}  // namespace

Palette MakePalette(uint32_t seed, bool dark) {
    const SeedLch lch = Analyze(seed);
    const double chroma = std::min(lch.chroma, 0.20);
    // Game keeps the seed hue; Chat sits 200 degrees away so both sides stay apart for any seed, and
    // the default orange gets the blue it always had.
    const TonalPalette primary{lch.hue, chroma};
    const TonalPalette secondary{lch.hue, std::min(chroma / 3.0, 0.045)};
    const TonalPalette tertiary{lch.hue + 200.0 * kPi / 180.0, std::clamp(chroma * 0.6, 0.06, 0.11)};
    const TonalPalette neutral{lch.hue, std::min(chroma / 10.0, 0.012)};
    const TonalPalette neutralVariant{lch.hue, std::min(chroma / 5.0, 0.026)};
    const TonalPalette error{29.0 * kPi / 180.0, 0.19};

    if (dark) {
        return Palette{
            .dark = true,
            .primary = primary(80),
            .onPrimary = primary(20),
            .primaryContainer = primary(30),
            .onPrimaryContainer = primary(90),
            .secondaryContainer = secondary(30),
            .onSecondaryContainer = secondary(90),
            .tertiary = tertiary(80),
            .onTertiary = tertiary(20),
            .tertiaryContainer = tertiary(30),
            .onTertiaryContainer = tertiary(90),
            .error = error(80),
            .surface = neutral(6),
            .surfaceContainer = neutral(12),
            .surfaceContainerHigh = neutral(17),
            .surfaceContainerHighest = neutral(22),
            .onSurface = neutral(90),
            .onSurfaceVariant = neutralVariant(80),
            .outline = neutralVariant(60),
            .outlineVariant = neutralVariant(30),
            .inverseSurface = neutral(90),
            .inverseOnSurface = neutral(20),
            .inversePrimary = primary(40),
        };
    }
    return Palette{
        .dark = false,
        .primary = primary(40),
        .onPrimary = primary(100),
        .primaryContainer = primary(90),
        .onPrimaryContainer = primary(10),
        .secondaryContainer = secondary(90),
        .onSecondaryContainer = secondary(10),
        .tertiary = tertiary(40),
        .onTertiary = tertiary(100),
        .tertiaryContainer = tertiary(90),
        .onTertiaryContainer = tertiary(10),
        .error = error(40),
        .surface = neutral(98),
        .surfaceContainer = neutral(94),
        .surfaceContainerHigh = neutral(92),
        .surfaceContainerHighest = neutral(90),
        .onSurface = neutral(10),
        .onSurfaceVariant = neutralVariant(30),
        .outline = neutralVariant(50),
        .outlineVariant = neutralVariant(80),
        .inverseSurface = neutral(20),
        .inverseOnSurface = neutral(95),
        .inversePrimary = primary(80),
    };
}

void SetTheme(ThemeMode mode, uint32_t seed) {
    g_mode = mode;
    g_palettes[0] = MakePalette(seed, false);
    g_palettes[1] = MakePalette(seed, true);
}

bool ThemeIsDark() {
    switch (g_mode) {
    case ThemeMode::Light: return false;
    case ThemeMode::Dark: return true;
    case ThemeMode::System: break;
    }
    return SystemPrefersDark();
}

const Palette& ThemePalette() {
    return g_palettes[ThemeIsDark() ? 1 : 0];
}

bool SystemPrefersDark() {
    DWORD value = 1;
    DWORD size = sizeof(value);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return value == 0;
}

uint32_t WindowsAccentColor() {
    // Stored as 0xAABBGGRR.
    DWORD value = 0;
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", L"AccentColor", RRF_RT_REG_DWORD,
                     nullptr, &value, &size) == ERROR_SUCCESS) {
        return ((value & 0xFF) << 16) | (value & 0xFF00) | ((value >> 16) & 0xFF);
    }
    DWORD colorization = 0;
    BOOL opaque = FALSE;
    if (SUCCEEDED(DwmGetColorizationColor(&colorization, &opaque))) {
        return colorization & 0xFFFFFF;
    }
    return kDefaultAccent;
}

D2D1_COLOR_F SeedSwatch(uint32_t seed, bool dark) {
    const SeedLch lch = Analyze(seed);
    return ToneColor(lch.hue, std::min(lch.chroma, 0.20), dark ? 70.0 : 50.0);
}

uint32_t SeedFromHueChroma(float hueDegrees, float chroma) {
    const D2D1_COLOR_F color = ToneColor(hueDegrees * kPi / 180.0, chroma, 60.0);
    auto channel = [](float v) { return static_cast<uint32_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    return (channel(color.r) << 16) | (channel(color.g) << 8) | channel(color.b);
}

void HueChromaOf(uint32_t seed, float& hueDegrees, float& chroma) {
    const SeedLch lch = Analyze(seed);
    double degrees = lch.hue * 180.0 / kPi;
    if (degrees < 0.0) {
        degrees += 360.0;
    }
    hueDegrees = static_cast<float>(degrees);
    chroma = static_cast<float>(std::min(lch.chroma, 0.20));
}

D2D1_COLOR_F HueChromaColor(float hueDegrees, float chroma, bool dark) {
    return ToneColor(hueDegrees * kPi / 180.0, chroma, dark ? 70.0 : 55.0);
}

D2D1_COLOR_F RgbColor(uint32_t rgb) {
    return {((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, 1.0f};
}

D2D1_COLOR_F Alpha(D2D1_COLOR_F color, float alpha) {
    color.a *= alpha;
    return color;
}

D2D1_COLOR_F Mix(D2D1_COLOR_F from, D2D1_COLOR_F to, float t) {
    return {from.r + (to.r - from.r) * t, from.g + (to.g - from.g) * t, from.b + (to.b - from.b) * t,
            from.a + (to.a - from.a) * t};
}

}  // namespace ui
