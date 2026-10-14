#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

struct Hotkey {
    UINT modifiers = 0;  // MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN
    UINT key = 0;        // Virtual-key code; 0 = not assigned.

    bool IsEmpty() const { return key == 0; }
    bool operator==(const Hotkey&) const = default;
};

enum HotkeyAction : int {
    kHotkeyTowardGame,
    kHotkeyTowardChat,
    kHotkeyCenter,
    kHotkeyMuteChat,    // Silences the Chat side until pressed again; the balance stays.
    kHotkeyNextPreset,  // Next equalizer preset.
    kHotkeyCount,
};

Hotkey DefaultHotkey(int action);
uint32_t PackHotkey(Hotkey hotkey);
Hotkey UnpackHotkey(uint32_t packed);

// Display text such as "Ctrl + Alt + ←".
std::wstring HotkeyText(Hotkey hotkey);
std::wstring ModifiersText(UINT modifiers);
std::wstring KeyText(UINT key);

bool IsModifierKey(UINT key);
// Modifier keys currently held, as MOD_* flags.
UINT HeldModifiers();
// Single keys that make sense without modifiers (F13-F24 and media keys).
