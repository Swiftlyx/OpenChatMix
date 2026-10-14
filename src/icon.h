#pragma once

#include <windows.h>

#include <cstdint>

// Colors of the tray icon, 0xRRGGBB.
struct LevelIconColors {
    uint32_t game;
    uint32_t chat;
};

// Renders the Game and Chat levels as two rounded, anti-aliased bars that shrink with their level.
// The caller owns the returned icon (DestroyIcon).
HICON CreateLevelIcon(int size, int game, int chat, const LevelIconColors& colors);
