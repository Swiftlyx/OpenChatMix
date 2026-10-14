#include "widgets.h"

#include <algorithm>
#include <cmath>

#include "../balance.h"

namespace ui {

void DrawBalancedChip(Canvas& canvas, const Palette& p, D2D1_POINT_2F center, const wchar_t* text, float alpha) {
    if (alpha <= 0.01f) {
        return;
    }
    const float width = canvas.Measure(text, TextStyle::LabelMedium) + 24.0f;
    const D2D1_RECT_F chip{center.x - width / 2.0f, center.y - 12.0f, center.x + width / 2.0f, center.y + 12.0f};
    canvas.FillRound(chip, 12.0f, Alpha(p.secondaryContainer, alpha));
    canvas.Text(text, chip, TextStyle::LabelMedium, Alpha(p.onSecondaryContainer, alpha), TextAlign::Center);
}

void DrawBalanceSlider(Canvas& canvas, const Palette& p, float left, float right, float centerY, float position,
                       float handleWidth, const SliderStyle& style) {
    const float handleX = left + (position + kBalanceRange) / (2.0f * kBalanceRange) * (right - left);
    const float centerX = (left + right) / 2.0f;
    const D2D1_COLOR_F active = position < -0.4f ? p.primary : position > 0.4f ? p.tertiary : p.onSurface;
    const D2D1_COLOR_F onActive = position < 0.0f ? p.onPrimary : p.onTertiary;
    const float top = centerY - style.track / 2.0f;
    const float bottom = centerY + style.track / 2.0f;
    const float round = style.track / 2.0f;

    // Fully rounded outer ends, small radius next to the handle gap.
    auto segment = [&](float a, float b, D2D1_COLOR_F color, bool outerLeft, bool outerRight) {
        if (b - a < 0.5f) {
            return;
        }
        float innerLeft = a;
        float innerRight = b;
        if (outerLeft) {
            canvas.FillRound({a, top, std::min(b, a + style.track), bottom}, round, color);
            innerLeft = std::min(b, a + round);
        }
        if (outerRight) {
            canvas.FillRound({std::max(a, b - style.track), top, b, bottom}, round, color);
            innerRight = std::max(a, b - round);
        }
        if (innerRight > innerLeft) {
            canvas.FillRound({innerLeft, top, innerRight, bottom}, std::min(2.0f, round), color);
        }
    };

    const float gapLeft = handleX - handleWidth / 2.0f - style.gap;
    const float gapRight = handleX + handleWidth / 2.0f + style.gap;
    segment(left, gapLeft, p.secondaryContainer, true, false);
    segment(gapRight, right, p.secondaryContainer, false, true);
    if (position < 0.0f) {
        segment(gapRight, centerX, active, false, false);
    } else if (position > 0.0f) {
        segment(centerX, gapLeft, active, false, false);
    }

    // Stop indicators at both ends and at the balanced point.
    const float dot = std::max(1.5f, style.track / 8.0f);
    if (gapLeft > left + style.track) {
        canvas.FillCircle({left + round, centerY}, dot, p.onSecondaryContainer);
    }
    if (gapRight < right - style.track) {
        canvas.FillCircle({right - round, centerY}, dot, p.onSecondaryContainer);
    }
    if (std::fabs(handleX - centerX) > style.gap + 4.0f) {
        canvas.FillCircle({centerX, centerY}, dot, onActive);
    }

    canvas.FillRound({handleX - handleWidth / 2.0f, centerY - style.handleHeight / 2.0f, handleX + handleWidth / 2.0f,
                      centerY + style.handleHeight / 2.0f},
                     handleWidth / 2.0f, active);
}

void DrawValueSlider(Canvas& canvas, const Palette& p, float left, float right, float centerY, float t,
                     float handleWidth, const SliderStyle& style, bool known) {
    t = std::clamp(t, 0.0f, 1.0f);
    const float handleX = left + t * (right - left);
    const float top = centerY - style.track / 2.0f;
    const float bottom = centerY + style.track / 2.0f;
    const float round = style.track / 2.0f;
    const float gapLeft = handleX - handleWidth / 2.0f - style.gap;
    const float gapRight = handleX + handleWidth / 2.0f + style.gap;
    const D2D1_COLOR_F active = known ? p.primary : p.outline;

    if (known && gapLeft > left) {
        canvas.FillRound({left, top, std::min(gapLeft, left + style.track), bottom}, round, active);
        if (gapLeft > left + round) {
            canvas.FillRound({left + round, top, gapLeft, bottom}, std::min(2.0f, round), active);
        }
    }
    if (gapRight < right) {
        canvas.FillRound({std::max(gapRight, right - style.track), top, right, bottom}, round, p.secondaryContainer);
        if (gapRight < right - round) {
            canvas.FillRound({gapRight, top, right - round, bottom}, std::min(2.0f, round), p.secondaryContainer);
        }
        canvas.FillCircle({right - round, centerY}, std::max(1.5f, style.track / 8.0f), p.onSecondaryContainer);
    }
    canvas.FillRound({handleX - handleWidth / 2.0f, centerY - style.handleHeight / 2.0f, handleX + handleWidth / 2.0f,
                      centerY + style.handleHeight / 2.0f},
                     handleWidth / 2.0f, active);
}

}  // namespace ui
