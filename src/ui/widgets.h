#pragma once

#include "canvas.h"
#include "theme.h"

namespace ui {

struct SliderStyle {
    float track;         // Track thickness.
    float gap;           // Space between the handle and the track.
    float handleHeight;
};

// Centered Material 3 slider for the Game/Chat balance. position is the balance in percent
// (negative favours Game) and may be fractional while animating.
void DrawBalanceSlider(Canvas& canvas, const Palette& palette, float left, float right, float centerY, float position,
                       float handleWidth, const SliderStyle& style);

// Tonal chip that marks the even balance, centered on center; alpha fades it in and out.
void DrawBalancedChip(Canvas& canvas, const Palette& palette, D2D1_POINT_2F center, const wchar_t* text, float alpha);

// Standard Material 3 slider; the active track grows from the left edge. t is in [0, 1].
// Unknown values draw a muted track without an active part.
void DrawValueSlider(Canvas& canvas, const Palette& palette, float left, float right, float centerY, float t,
                     float handleWidth, const SliderStyle& style, bool known);

// The headset battery as the tray icon draws it: a body on its side with the terminal on the right,
// the charge (0..1) from the left in color and the rest faded, a bolt while charging. height is the
// body's; the icon is about 1.8 times as wide.
void DrawBatteryIcon(Canvas& canvas, const Palette& palette, D2D1_POINT_2F center, float height, float charge,
                     bool charging, D2D1_COLOR_F color);

}  // namespace ui
