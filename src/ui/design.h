#pragma once

// OpenChatMix design tokens. Every size, gap, radius, opacity and spring in the panel and the popup
// comes from here; docs/design-system.md explains the rules behind them. Values are DIPs (1/96 inch).
// The system is Material 3 Expressive at desktop density: shapes, state opacities and springs are the
// Material tokens; sizes are the dense variants (one density step is 4 DIP).

namespace ui::design {

// ---- Spacing: a 4 DIP grid ------------------------------------------------------------------------
inline constexpr float kSpace1 = 4.0f;
inline constexpr float kSpace2 = 8.0f;
inline constexpr float kSpace3 = 12.0f;
inline constexpr float kSpace4 = 16.0f;
inline constexpr float kSpace5 = 20.0f;
inline constexpr float kSpace6 = 24.0f;
inline constexpr float kSpace8 = 32.0f;

// ---- Panel layout ------------------------------------------------------------------------------------
inline constexpr float kPanelWidth = 400.0f;
inline constexpr float kPanelHeight = 640.0f;
inline constexpr float kScreenMargin = kSpace3;    // Between the panel and the taskbar or screen edge.
inline constexpr float kHeaderHeight = 56.0f;      // Page title, or back button with title.
inline constexpr float kNavHeight = 64.0f;         // Navigation bar at the bottom.
inline constexpr float kPageGutter = kSpace3;      // Page edge to the edge of groups, tiles and buttons.
inline constexpr float kItemPadding = kSpace4;     // Tile edge to its content, left and right.
inline constexpr float kContentInset = kPageGutter + kItemPadding;  // Page edge to row text and controls.
inline constexpr float kIconSize = 20.0f;          // Leading icons, navigation, icon buttons.
inline constexpr float kSmallIconSize = 14.0f;     // Chevrons, checks, drag handles.
inline constexpr float kLeadingIconCenter = kContentInset + kIconSize / 2.0f;
inline constexpr float kLeadingTextInset = kContentInset + kIconSize + kSpace3;  // Text after a leading icon.
inline constexpr float kTrailingIconCenter = kPanelWidth - kContentInset - kIconSize / 2.0f;

// ---- Groups: segmented lists -------------------------------------------------------------------------
// Rows of one topic are tiles stacked with a small gap. The group's outer corners are large and the
// corners between tiles small, so the group reads as one shape made of parts.
inline constexpr float kSegmentGap = 2.0f;         // Between the tiles of a group.
inline constexpr float kGroupGap = kSpace3;        // Between groups.
inline constexpr float kSectionGap = kSpace4;      // Above a section title.
inline constexpr float kSectionTitleHeight = 28.0f;
inline constexpr float kPageHintGap = kSpace2;     // Below a hint between groups.

// ---- Rows ----------------------------------------------------------------------------------------------
inline constexpr float kRowHeight = 48.0f;          // One line: label with a switch, value or chevron.
inline constexpr float kTwoLineRowHeight = 60.0f;   // Title with a subtitle.
inline constexpr float kSubtitleTop = 31.0f;        // Where the subtitle of a two-line row starts.
inline constexpr float kChoiceRowHeight = 80.0f;    // Label above a button group or slider.
inline constexpr float kHintTopInRow = 42.0f;       // Supporting text below a one-line row's label.
inline constexpr float kHintBottomGap = 10.0f;      // Below supporting text, to the tile's edge.

// ---- Controls ------------------------------------------------------------------------------------------
inline constexpr float kControlHeight = 32.0f;      // Button groups, dropdowns, value fields, chips.
inline constexpr float kButtonHeight = 40.0f;       // Full-width buttons.
inline constexpr float kIconButtonSize = 40.0f;     // Hit area and state layer of icon buttons.
inline constexpr float kSmallIconButtonSize = 32.0f;  // − and + of a number stepper.
inline constexpr float kSwitchWidth = 52.0f;
inline constexpr float kSwitchHeight = 32.0f;
inline constexpr float kFieldHeight = kControlHeight;
inline constexpr float kButtonGroupGap = 2.0f;      // Between the buttons of a connected button group.

// ---- Shape: the Material 3 corner scale ---------------------------------------------------------------
inline constexpr float kRadiusExtraSmall = 4.0f;
inline constexpr float kRadiusSmall = 8.0f;
inline constexpr float kRadiusMedium = 12.0f;
inline constexpr float kRadiusLarge = 16.0f;
inline constexpr float kRadiusLargeIncreased = 20.0f;
inline constexpr float kRadiusExtraLarge = 28.0f;
// Fully rounded shapes (buttons, switches, sliders) use half their height.

inline constexpr float kRadiusGroup = kRadiusLarge;            // Outer corners of a group.
inline constexpr float kRadiusTile = kRadiusExtraSmall;        // Corners between tiles.
inline constexpr float kRadiusTileHovered = kRadiusMedium;     // A hovered tile rounds its inner corners.
inline constexpr float kRadiusTileLifted = kRadiusLarge;       // A dragged tile is fully rounded.
inline constexpr float kRadiusMenu = kRadiusLarge;
inline constexpr float kRadiusMenuItem = kRadiusMedium;
inline constexpr float kRadiusField = kRadiusSmall;
inline constexpr float kRadiusButtonGroupInner = kRadiusSmall;
inline constexpr float kRadiusButtonGroupPressed = kRadiusExtraSmall;

// ---- Surfaces ------------------------------------------------------------------------------------------
// The page surface over the acrylic or mica backdrop and tiles over the page are translucent: 0.62 and
// 0.72 at standard contrast, less at higher levels. The palette carries them (surfaceAlpha, tileAlpha).

// ---- States (Material 3 state layers) -----------------------------------------------------------------
inline constexpr float kHoverOpacity = 0.08f;
inline constexpr float kFocusOpacity = 0.10f;
inline constexpr float kPressOpacity = 0.10f;
inline constexpr float kDragOpacity = 0.16f;
inline constexpr float kDisabledOpacity = 0.38f;
// Keyboard focus indicator: a ring in the secondary color around the focused element.
inline constexpr float kFocusRingWidth = 3.0f;
inline constexpr float kFocusRingOffset = 2.0f;  // Gap between the element and the ring.
inline constexpr float kScrimOpacity = 0.32f;  // Material scrim behind menus, in both themes.

// ---- Elevation: by tone only ------------------------------------------------------------------------
// Nothing casts a shadow. What floats (menus, a dragged tile) takes surfaceContainerHighest, and menus
// dim the page behind them with the scrim above.

// ---- Motion: Material 3 Expressive springs ---------------------------------------------------------------
// Values follow a damped spring toward their target and keep their velocity when the target changes,
// so interrupted motion stays smooth. Spatial springs move things and may overshoot a little; effects
// springs change color and opacity and never overshoot.
struct Spring {
    float damping;    // Ratio: 1 never overshoots, lower bounces.
    float stiffness;  // Higher is quicker.
};
inline constexpr Spring kSpringFastSpatial{0.6f, 800.0f};     // Switch thumbs, button shapes, indicators.
inline constexpr Spring kSpringDefaultSpatial{0.8f, 380.0f};  // Tiles making room, the balance slider.
inline constexpr Spring kSpringSlowSpatial{0.8f, 200.0f};     // Values that travel far: level marks.
inline constexpr Spring kSpringFastEffects{1.0f, 3800.0f};    // State layers, hover and press.
inline constexpr Spring kSpringDefaultEffects{1.0f, 1600.0f}; // Colors of selections, fades.
inline constexpr Spring kSpringSlowEffects{1.0f, 800.0f};     // Large fades.
inline constexpr Spring kSpringScroll{1.0f, 1200.0f};         // Scrolling.
// With reduced motion everything jumps to its target, except scrolling, which keeps this short glide
// so the eye can follow where the content went.
inline constexpr Spring kSpringReduced{1.0f, 3800.0f};

}  // namespace ui::design
