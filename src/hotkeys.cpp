#include "hotkeys.h"

#include <cwctype>

namespace {

constexpr UINT kModifierMask = MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN;

std::wstring KeyNameImpl(UINT key) {
    switch (key) {
    case VK_LEFT: return L"←";
    case VK_RIGHT: return L"→";
    case VK_UP: return L"↑";
    case VK_DOWN: return L"↓";
    case VK_PRIOR: return L"PgUp";
    case VK_NEXT: return L"PgDn";
    case VK_HOME: return L"Home";
    case VK_END: return L"End";
    case VK_INSERT: return L"Ins";
    case VK_DELETE: return L"Del";
    case VK_SPACE: return L"Space";
    case VK_PAUSE: return L"Pause";
    case VK_SCROLL: return L"ScrLk";
    case VK_MEDIA_PLAY_PAUSE: return L"Play/Pause";
    case VK_MEDIA_NEXT_TRACK: return L"Next";
    case VK_MEDIA_PREV_TRACK: return L"Prev";
    case VK_VOLUME_UP: return L"Vol+";
    case VK_VOLUME_DOWN: return L"Vol−";
    case VK_VOLUME_MUTE: return L"Mute";
    }
    if (key >= VK_F1 && key <= VK_F24) {
        return L"F" + std::to_wstring(key - VK_F1 + 1);
    }
    if (key >= VK_NUMPAD0 && key <= VK_NUMPAD9) {
        return L"Num " + std::to_wstring(key - VK_NUMPAD0);
    }
    if ((key >= '0' && key <= '9') || (key >= 'A' && key <= 'Z')) {
        return std::wstring(1, static_cast<wchar_t>(key));
    }

    // Punctuation and other layout-dependent keys use the active layout's label.
    const UINT scan = MapVirtualKeyW(key, MAPVK_VK_TO_VSC_EX);
    LONG lParam = static_cast<LONG>((scan & 0xFF) << 16);
    if (scan & 0xE000) {
        lParam |= 1 << 24;
    }
    wchar_t name[64] = {};
    if (GetKeyNameTextW(lParam, name, ARRAYSIZE(name)) > 0) {
        std::wstring text = name;
        if (text.size() == 1) {
            text[0] = static_cast<wchar_t>(std::towupper(text[0]));
        }
        return text;
    }
    wchar_t fallback[16];
    swprintf_s(fallback, L"0x%02X", key);
    return fallback;
}

}  // namespace

Hotkey DefaultHotkey(int action) {
    switch (action) {
    case kHotkeyTowardGame: return {MOD_CONTROL | MOD_ALT, VK_LEFT};
    case kHotkeyTowardChat: return {MOD_CONTROL | MOD_ALT, VK_RIGHT};
    case kHotkeyCenter: return {MOD_CONTROL | MOD_ALT, VK_DOWN};
    }
    return {};
}

uint32_t PackHotkey(Hotkey hotkey) {
    return ((hotkey.modifiers & kModifierMask) << 16) | (hotkey.key & 0xFF);
}

Hotkey UnpackHotkey(uint32_t packed) {
    return {(packed >> 16) & kModifierMask, packed & 0xFF};
}

std::wstring ModifiersText(UINT modifiers) {
    std::wstring text;
    auto append = [&](UINT flag, const wchar_t* name) {
        if (modifiers & flag) {
            text += name;
            text += L" + ";
        }
    };
    append(MOD_CONTROL, L"Ctrl");
    append(MOD_ALT, L"Alt");
    append(MOD_SHIFT, L"Shift");
    append(MOD_WIN, L"Win");
    return text;
}

std::wstring HotkeyText(Hotkey hotkey) {
    if (hotkey.IsEmpty()) {
        return {};
    }
    return ModifiersText(hotkey.modifiers) + KeyText(hotkey.key);
}

bool IsModifierKey(UINT key) {
    switch (key) {
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
    case VK_LWIN:
    case VK_RWIN:
        return true;
    }
    return false;
}

UINT HeldModifiers() {
    UINT modifiers = 0;
    if (GetKeyState(VK_CONTROL) < 0) {
        modifiers |= MOD_CONTROL;
    }
    if (GetKeyState(VK_MENU) < 0) {
        modifiers |= MOD_ALT;
    }
    if (GetKeyState(VK_SHIFT) < 0) {
        modifiers |= MOD_SHIFT;
    }
    if (GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0) {
        modifiers |= MOD_WIN;
    }
    return modifiers;
}

std::wstring KeyText(UINT key) {
    return KeyNameImpl(key);
}
