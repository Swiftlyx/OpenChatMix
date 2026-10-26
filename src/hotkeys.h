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
    kHotkeyMuteGame,    // Silences the Game side until pressed again; the balance stays.
    kHotkeyMuteChat,    // The same for the Chat side,
    kHotkeyMuteMedia,   // ...and for the Media group, which ChatMix otherwise leaves alone,
    kHotkeyMuteAux,     // ...and for the Aux group.
    kHotkeyNextOutput,  // Sends the sound to the next playback device, virtual cables left out.
    kHotkeyNextMicrophone,  // Feeds the microphone chain from the next microphone, virtual cables left out.
    kHotkeyScreensaver,  // Shows the headset display's screensaver now, or hides it.
    kHotkeyCount,
};

Hotkey DefaultHotkey(int action);
uint32_t PackHotkey(Hotkey hotkey);
Hotkey UnpackHotkey(uint32_t packed);

// Display text such as "Ctrl + Alt + ←".
std::wstring HotkeyText(Hotkey hotkey);
std::wstring ModifiersText(UINT modifiers);
std::wstring KeyText(UINT key);

// Modifier combinations offered for turning the balance with the mouse wheel; 0 turns it off.
// None of them is a common shortcut with the wheel: Ctrl and Ctrl + Alt zoom (browsers, the Windows
// Magnifier), Shift scrolls sideways.
inline constexpr UINT kWheelModifierChoices[] = {0, MOD_CONTROL | MOD_WIN, MOD_ALT | MOD_WIN,
                                                 MOD_CONTROL | MOD_ALT | MOD_SHIFT};

bool IsModifierKey(UINT key);
// Modifier keys currently held, as MOD_* flags.
UINT HeldModifiers();
// Single keys that make sense without modifiers (F13-F24 and media keys).
