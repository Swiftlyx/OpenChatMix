#pragma once

#include <windows.h>

// Pixel icons an add-on display can show beside its text, 9 x 9 each, as the panel's picker lists them.
enum AddOnIcon {
    kIconNone,
    kIconMessage,
    kIconMention,
    kIconMic,
    kIconMicOff,
    kIconTalking,  // Bars that move like a level meter.
    kIconHeadset,
    kIconUser,
    kIconJoin,
    kIconLeave,
    kIconBell,
    kIconHeart,
    kIconStar,
    kIconCheck,
    kIconCross,
    kIconMusic,
    kIconGamepad,
    kIconSpeaker,
    kIconAlert,
    kAddOnIconCount,
};

inline constexpr int kAddOnIconSize = 9;

// Whether a pixel of the icon is lit; elapsedMs moves the talking icon.
bool AddOnIconPixel(int icon, int x, int y, ULONGLONG elapsedMs);
