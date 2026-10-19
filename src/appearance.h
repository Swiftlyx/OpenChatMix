#pragma once

#include <cstdint>

// Appearance choices shared by the settings and the UI.

enum class ThemeMode { System, Light, Dark };

enum class Backdrop { Acrylic, Mica, Solid };

// Material 3 contrast levels: text, outlines and containers move further apart in tone, and surfaces
// become less translucent. Order matches the panel.
enum class ContrastLevel { Standard, Medium, High };

enum class TrayStyle { Color, Monochrome };

// Tray icon while the balance is even: the level bars, or a mark of its own. Battery shows the
// headset's charge and stays the bars while no battery level is known. Order matches the panel.
enum class BalancedIcon { Bars, Square, Ring, Equals, Battery };

// What colors the headset battery: the theme's accent, or its charge from red when empty to green when
// full. Order matches the panel.
enum class BatteryTint { Theme, Charge };

// Seed color of the default theme (the OpenChatMix orange), 0xRRGGBB.
constexpr uint32_t kDefaultAccent = 0xF07A2A;
