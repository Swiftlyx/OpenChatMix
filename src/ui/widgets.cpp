#include "widgets.h"

#include <algorithm>
#include <cmath>
#include <iterator>

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

void DrawBatteryIcon(Canvas& canvas, const Palette& palette, D2D1_POINT_2F center, float height, float charge,
                     bool charging, D2D1_COLOR_F color) {
    charge = std::clamp(charge, 0.0f, 1.0f);
    const float bodyWidth = height * 1.6f;
    const float capWidth = height * 0.2f;
    const float left = center.x - (bodyWidth + capWidth) / 2.0f;
    const D2D1_RECT_F body{left, center.y - height / 2.0f, left + bodyWidth, center.y + height / 2.0f};
    const float radius = height * 0.32f;
    const D2D1_COLOR_F faded = Alpha(color, 0.4f);
    canvas.FillRound(body, radius, faded);
    canvas.FillRound({body.right - 1.0f, center.y - height * 0.22f, body.right + capWidth, center.y + height * 0.22f},
                     capWidth / 2.0f, charge >= 1.0f ? color : faded);
    if (charge > 0.0f) {
        // The charge ends straight, wherever it is, so a low one keeps the body's shape.
        canvas.PushClip({body.left, body.top, body.left + bodyWidth * charge, body.bottom});
        canvas.FillRound(body, radius, color);
        canvas.PopClip();
    }
    if (charging) {
        constexpr float kBolt[][2] = {{0.57f, 0.06f}, {0.35f, 0.56f}, {0.49f, 0.56f}, {0.43f, 0.94f},
                                      {0.67f, 0.42f}, {0.53f, 0.42f}};
        D2D1_POINT_2F points[std::size(kBolt)];
        for (size_t i = 0; i < std::size(kBolt); ++i) {
            points[i] = {body.left + bodyWidth * kBolt[i][0], body.top + height * kBolt[i][1]};
        }
        canvas.FillPolygon(points, palette.surface);
    }
}

}  // namespace ui
