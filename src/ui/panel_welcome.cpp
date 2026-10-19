// The tour shown on the first run, and again from Settings > General: a few steps on what
// OpenChatMix does and where its settings are.

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>

#include "panel.h"
#include "panel_internal.h"

namespace ui {

using namespace detail;

namespace {

struct TourPoint {
    wchar_t glyph;
    Text text;
};

struct TourStep {
    wchar_t glyph;
    Text title;
    Text lead;
    std::array<TourPoint, 3> points;
};

constexpr TourStep kTour[] = {
    {kGlyphHeadphones,
     Text::TourWelcomeTitle,
     Text::TourWelcomeText,
     {{{kGlyphKeyboard, Text::TourHotkeys}, {kGlyphVolume, Text::TourDial}, {kGlyphMixer, Text::TourTray}}}},
    {kGlyphMixer,
     Text::TourGroupsTitle,
     Text::TourGroupsText,
     {{{kGlyphGame, Text::TourGroupsGame}, {kGlyphChat, Text::TourGroupsChat}, {kGlyphAdd, Text::TourGroupsAdd}}}},
    {kGlyphPlugin,
     Text::TourExtrasTitle,
     Text::TourExtrasText,
     {{{kGlyphEqualizer, Text::TourExtrasEq},
       {kGlyphSpeakers, Text::TourExtrasRouting},
       {kGlyphMicrophone, Text::TourExtrasMic}}}},
    {kGlyphPalette,
     Text::TourYoursTitle,
     Text::TourYoursText,
     {{{kGlyphContrast, Text::TourYoursLook},
       {kGlyphHeadphones, Text::TourYoursHeadset},
       {kGlyphPower, Text::TourYoursStart}}}},
};
constexpr int kTourSteps = static_cast<int>(std::size(kTour));

constexpr float kHeroCenterY = 104.0f;
constexpr float kHeroRadius = 40.0f;
constexpr float kSlideDistance = 48.0f;  // A new step slides in from this far to the side.
constexpr float kPointMinHeight = 56.0f;
constexpr float kFooterHeight = 104.0f;  // Page indicator and buttons.

}  // namespace

void Panel::DrawWelcome() {
    const Palette& p = P();
    tourStep_ = std::clamp(tourStep_, 0, kTourSteps - 1);
    const TourStep& step = kTour[tourStep_];
    const bool last = tourStep_ == kTourSteps - 1;
    auto finish = [this] { GoTo(Page::Mixer); };

    // Shared axis: the step slides in from the side it comes from and fades in on the way.
    const float position = Animate(AnimKey(kIdTourStep, 0), static_cast<float>(tourStep_), design::kSpringDefaultSpatial);
    const float travel = static_cast<float>(tourStep_) - position;
    const float dx = std::clamp(travel, -1.0f, 1.0f) * kSlideDistance;
    const float fade = std::clamp(1.0f - std::fabs(travel), 0.0f, 1.0f);

    if (!last) {
        TextButton(kIdTourSkip, {kWidth - kGutter - 112.0f, design::kSpace3, kWidth - kGutter, design::kSpace3 + design::kButtonHeight},
                   0, T(Text::TourSkip), false, finish);
    }

    canvas_.FillCircle({kWidth / 2.0f + dx, kHeroCenterY}, kHeroRadius, Alpha(p.primaryContainer, fade));
    canvas_.Glyph(step.glyph, {kWidth / 2.0f + dx, kHeroCenterY}, 34.0f, Alpha(p.onPrimaryContainer, fade));

    // Every step lays out the same: the lead gets the room of the longest one and every point that
    // of the longest point, so the tiles stay in place from step to step.
    const float textWidth = kWidth - 2.0f * kInset;
    const float pointTextLeft = design::kLeadingTextInset;
    const float pointTextWidth = kWidth - kInset - pointTextLeft;
    float leadArea = 0.0f;
    float pointTextArea = 0.0f;
    for (const TourStep& each : kTour) {
        leadArea = std::max(leadArea, std::ceil(canvas_.MeasureHeight(T(each.lead), TextStyle::BodyMedium, textWidth)));
        for (const TourPoint& point : each.points) {
            pointTextArea = std::max(
                pointTextArea, std::ceil(canvas_.MeasureHeight(T(point.text), TextStyle::BodyMedium, pointTextWidth)));
        }
    }
    const float pointHeight = std::max(kPointMinHeight, pointTextArea + 2.0f * design::kSpace4);

    float y = kHeroCenterY + kHeroRadius + design::kSpace5;
    canvas_.Text(T(step.title), {kInset + dx, y, kWidth - kInset + dx, y + 32.0f}, TextStyle::TitleLarge,
                 Alpha(p.onSurface, fade), TextAlign::Center);
    y += 32.0f + design::kSpace2;
    const float leadHeight = std::ceil(canvas_.MeasureHeight(T(step.lead), TextStyle::BodyMedium, textWidth));
    const float leadTop = y + (leadArea - leadHeight) / 2.0f;
    canvas_.Text(T(step.lead), {kInset + dx, leadTop, kWidth - kInset + dx, leadTop + leadHeight},
                 TextStyle::BodyMedium, Alpha(p.onSurfaceVariant, fade), TextAlign::Center, true);
    y += leadArea + design::kSpace5;

    // The points as one group of tiles, each with its icon and a wrapped line or two.
    for (size_t index = 0; index < step.points.size(); ++index) {
        const TourPoint& point = step.points[index];
        const float textHeight = std::ceil(canvas_.MeasureHeight(T(point.text), TextStyle::BodyMedium, pointTextWidth));
        const D2D1_RECT_F tile{kGutter + dx, y, kWidth - kGutter + dx, y + pointHeight};
        const float top = index == 0 ? design::kRadiusGroup : design::kRadiusTile;
        const float bottom = index + 1 == step.points.size() ? design::kRadiusGroup : design::kRadiusTile;
        canvas_.FillShape(tile, Corners::Vertical(top, bottom), Alpha(p.surfaceContainer, p.tileAlpha * fade));
        const float middle = y + pointHeight / 2.0f;
        canvas_.Glyph(point.glyph, {design::kLeadingIconCenter + dx, middle}, design::kIconSize, Alpha(p.primary, fade));
        canvas_.Text(T(point.text),
                     {pointTextLeft + dx, middle - textHeight / 2.0f, pointTextLeft + pointTextWidth + dx,
                      middle + textHeight / 2.0f},
                     TextStyle::BodyMedium, Alpha(p.onSurface, fade), TextAlign::Leading, true);
        y += pointHeight + design::kSegmentGap;
    }

    // Page indicator: the current step is a wider pill; the pills spring between their widths.
    const float footerTop = kHeight - kFooterHeight;
    constexpr float kDot = 8.0f;
    constexpr float kActiveDot = 24.0f;
    float dotsWidth = (kTourSteps - 1) * design::kSpace2;
    std::array<float, kTourSteps> widths{};
    for (int index = 0; index < kTourSteps; ++index) {
        widths[index] = Animate(AnimKey(kIdTourStep, 1 + index), index == tourStep_ ? kActiveDot : kDot,
                                design::kSpringFastSpatial);
        dotsWidth += widths[index];
    }
    float x = (kWidth - dotsWidth) / 2.0f;
    for (int index = 0; index < kTourSteps; ++index) {
        const bool current = index == tourStep_;
        canvas_.FillRound({x, footerTop + design::kSpace3, x + widths[index], footerTop + design::kSpace3 + kDot}, kDot / 2.0f,
                          current ? p.primary : p.outlineVariant);
        x += widths[index] + design::kSpace2;
    }

    const float buttonTop = kHeight - design::kSpace5 - design::kButtonHeight;
    if (tourStep_ > 0) {
        TextButton(kIdTourBack, {kGutter, buttonTop, kGutter + 120.0f, buttonTop + design::kButtonHeight}, 0,
                   T(Text::TourBack), false, [this] {
                       --tourStep_;
                       Invalidate();
                   });
    }
    TextButton(kIdTourNext,
               {kWidth - kGutter - 152.0f, buttonTop, kWidth - kGutter, buttonTop + design::kButtonHeight}, 0,
               T(last ? Text::TourDone : Text::TourNext), true, [this, last, finish] {
                   if (last) {
                       finish();
                       return;
                   }
                   ++tourStep_;
                   Invalidate();
               });
}

}  // namespace ui
