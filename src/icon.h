#pragma once

#include <windows.h>

#include <cstdint>

#include "appearance.h"

// Colors of the tray icon, 0xRRGGBB.
struct LevelIconColors {
    uint32_t game;
    uint32_t chat;
    uint32_t battery = 0;  // The battery mark.
};

// The headset battery for the battery mark: charge 0..1.
struct BatteryMark {
    float charge = 1.0f;
    bool charging = false;
};

// Renders the Game and Chat levels (percent, fractions allowed while they animate) as two rounded,
// anti-aliased bars that shrink with their level. morph turns the bars (0) into the balanced mark (1)
// shape by shape: the bars turn into the equals sign, widen into the square, bend into the two
// halves of the ring, or close into a battery lying on its side that shows its charge. Game is on
// the left or top, Chat on the right or bottom. The caller owns the returned icon (DestroyIcon).
HICON CreateLevelIcon(int size, float game, float chat, const LevelIconColors& colors,
                      BalancedIcon mark = BalancedIcon::Bars, float morph = 0.0f, BatteryMark battery = {});
