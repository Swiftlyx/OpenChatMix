# OpenChatMix design system

These are the rules behind the tray panel and the popup.
[`src/ui/design.h`](../src/ui/design.h) holds every size, gap, radius, opacity and spring. The
components that pages are built from are in
[`src/ui/panel_components.cpp`](../src/ui/panel_components.cpp). If a page needs a number that isn't
listed here, add a token for it first; never put a literal in the page code.

The system is **Material 3 Expressive** at desktop density. It uses:

- tonal color roles built from a Game seed color and a Chat color, in soft (pastel) tones;
- three contrast levels;
- keyboard focus with a focus ring;
- state layers;
- the M3 corner scale;
- segmented lists;
- connected button groups;
- spring motion.

The shape, state and motion values come from the Material tokens that Jetpack Compose Material 3
ships (`ShapeTokens`, `StateTokens`, `ListTokens`, `ConnectedButtonGroupSmallTokens`,
`ExpressiveMotionTokens`). Sizes use the dense variants, since this is a 400 DIP tray flyout and not a
phone screen. It is drawn in Segoe UI Variable with Fluent icons, over acrylic or mica.

## Principles

1. **One control per kind of choice.** Never offer two controls for the same choice:
   - on/off → switch;
   - two to five short options → button group;
   - a longer list → dropdown;
   - a number → − value +, where the value can also be typed;
   - an order → drag.
2. **Groups, not boxes.** The rows of one topic are separate tiles 2 DIP apart, making one group
   shape. The group's outer corners are large and its inner corners small. The gaps show where one
   row ends and the next begins, so rows need no dividers or outlines.
3. **Compact but airy.** Rows are dense (48 DIP). The air comes from the gaps between groups and from
   supporting text, not from padding inside rows.
4. **Explain in place.** If a setting's effect isn't obvious, its supporting text sits under it, in
   its tile. A rule that covers a whole list sits above the list as a page hint.
5. **Direct manipulation.** Drag to reorder, drag on the graph to move a band, click a value to type
   it. Buttons are shortcuts, never the only way.
6. **Motion has physics.** Things move on springs: quick, a little lively, and smooth when
   interrupted. With reduced motion, nothing moves except scrolling.
7. **Light to run.** The panel redraws only on input or while something animates. Animation stops
   as soon as every spring settles. Shapes are plain Direct2D geometry; there are no images, blur
   passes or offscreen layers.

## Units

All values are DIPs (1/96 inch) and scale with the monitor DPI. The panel is **400 × 640**. It sits
`kScreenMargin` (12) from the taskbar and the screen edge.

## Spacing

All spacing is on a 4 DIP grid (`kSpace1` = 4 … `kSpace8` = 32).

| Token            | DIP | Use                                                 |
|------------------|-----|-----------------------------------------------------|
| `kSegmentGap`    | 2   | Between the tiles of a group                        |
| `kButtonGroupGap`| 2   | Between the buttons of a connected button group     |
| `kPageHintGap`   | 8   | Below a page hint                                   |
| `kHintBottomGap` | 10  | Supporting text to the tile's bottom edge           |
| `kGroupGap`      | 12  | Between groups                                      |
| `kPageGutter`    | 12  | Panel edge to groups, tiles and full-width buttons  |
| `kItemPadding`   | 16  | Tile edge to its content                            |
| `kSectionGap`    | 16  | Above a section title                               |

### The page skeleton

```
 0 ┌──────────────────────────────────────────┐
   │ ‹  Title (TitleLarge)                [📌]│  kHeaderHeight 56
56 ├──────────────────────────────────────────┤
   │12╭────────────────────────────────────╮12│  group: outer corners 16
   │  │16 Label                  [switch]16│  │  one-line tile 48
   │  ├────────────────────────────────────┤  │  2 DIP gap, inner corners 4
   │  │   Label                    value › │  │
   │  │   Supporting text (BodySmall)      │  │  supporting text inside the tile
   │  ╰────────────────────────────────────╯  │
   │        12  kGroupGap                     │
   │  ╭────────────────────────────────────╮  │
   │  ╰────────────────────────────────────╯  │
   │        16  kSectionGap                   │
   │   Section title (TitleSmall, primary)    │  kSectionTitleHeight 28
   │   Page hint (BodySmall)                  │  then kPageHintGap 8
   │  ╭────────────────────────────────────╮  │
576├──────────────────────────────────────────┤
   │   (Mixer)    Equalizer     Settings      │  kNavHeight 64
640└──────────────────────────────────────────┘
```

Horizontal alignment lines, from the panel's left edge:

| Line                  | x   | What sits on it                                        |
|-----------------------|-----|--------------------------------------------------------|
| `kPageGutter`         | 12  | Group and tile edges, full-width buttons               |
| `kContentInset`       | 28  | Labels, section titles, page hints, control edges      |
| `kLeadingIconCenter`  | 38  | Leading icons (20 DIP)                                 |
| `kLeadingTextInset`   | 60  | Text after a leading icon                              |
| `kTrailingIconCenter` | 362 | Chevrons, checks, drag handles                         |

Section titles, page hints and row labels all share the 28 DIP line, so text lines up down the whole
page.

## Groups and tiles

Every row is a **tile**. `DrawCard` stacks the tiles of a group:

| Corner                     | Radius                    | Token                |
|----------------------------|---------------------------|----------------------|
| Outer corners of the group | 16 (Large)                | `kRadiusGroup`       |
| Between tiles, at rest     | 4 (Extra small)           | `kRadiusTile`        |
| Between tiles, hovered     | 12 (Medium), springs open | `kRadiusTileHovered` |
| A dragged tile             | 16 (Large) on all sides   | `kRadiusTileLifted`  |

- Tiles are `surfaceContainer` at the palette's `tileAlpha` (.72 at standard contrast) over the page
  surface.
- A tile that reacts as a whole, such as a switch row or a link, takes the hover and press layer
  across its whole shape. Its inner corners also round from 4 to 12 on the fast spatial spring. This
  is the M3 Expressive shape morph.
- A tile whose controls react on their own, such as a dropdown, the − value + stepper or a button
  group, stays still; only the controls react.
- A standalone surface, such as the mixer card or the equalizer graph, is a group of one: `Card()`
  with 16 DIP corners.

## Rows

| Row              | Height | Content                                                       |
|------------------|--------|---------------------------------------------------------------|
| One-line         | 48     | Label with a switch, dropdown, value, stepper or chevron      |
| Two-line         | 60     | Title with a subtitle: navigation, plugins, profiles          |
| App row          | 56     | A 32 DIP app icon beside two lines                            |
| Choice row       | 80     | Label above a button group or a slider                        |
| Supporting text  | text   | Starts at 42 in a one-line row, 8 below a button group; 10 below |

- Labels use **BodyLarge** in `onSurface`, and values **BodyMedium** in `onSurfaceVariant`.
- A label that doesn't fit wraps onto two lines in BodyMedium; it is never cut off.
- Supporting text uses **BodySmall** in `onSurfaceVariant` and runs the full content width.

## Controls

| Control                | Size             | Shape                                    | Use                                  |
|------------------------|------------------|------------------------------------------|--------------------------------------|
| Switch                 | 52 × 32          | Full; handle 16 off, 24 on, 28 pressed, with a check when on | On/off that applies at once |
| Connected button group | height 32        | Outer full, inner 8; pressed 4; selected springs fully round | 2–5 short options |
| Dropdown               | height 32        | 8, tonal (`surfaceContainerHighest`)     | Longer lists, long labels            |
| Number stepper         | − 32, value 80, + 32 | Round buttons, value field 8         | Numbers: step or type                |
| Value field            | height 32        | 8; outlined at rest, 2 DIP `primary` when editing | Typed numbers               |
| Button                 | height 40 (32 in rows) | Full, springs to 8 while pressed   | Actions                              |
| Icon button            | 40 hit, 18 glyph | Circle state layer                       | Back, pin, remove, band select       |
| Chip                   | height 32        | 8, outlined                              | Shortcuts, saved presets             |
| Status chip            | height 16, `LabelSmall` | Full, outlined (`outlineVariant`) | A short fact in a line of text: the headset's connection on the *Mixer* tab |
| Search bar             | height 48        | Full                                     | Filtering a list                     |

Choosing between them:

| The choice is…                              | Control             | Examples                                       |
|---------------------------------------------|---------------------|------------------------------------------------|
| On or off                                   | Switch              | Media group, mic monitoring                    |
| 2–5 options, short labels                   | Button group        | Theme, noise suppression, compressor, Smart Volume |
| More than 5 options, or long labels         | Dropdown            | Filter type, wheel combination, screensaver    |
| A number with a range                       | − value +           | EQ frequency, gain, Q, ducking, battery level  |
| An order                                    | Drag                | Output priority, mic processing, plugins       |
| A device or something on its own page       | Value row with ›    | Microphone, virtual cable, routing slots       |

## Shape

The Material 3 corner scale:

| Token                   | Radius | Used for                                           |
|-------------------------|--------|----------------------------------------------------|
| `kRadiusExtraSmall`     | 4      | Inner corners of tiles; pressed buttons in a group |
| `kRadiusSmall`          | 8      | Fields, dropdowns, chips, inner corners of button groups, pressed buttons |
| `kRadiusMedium`         | 12     | Hovered tiles, menu items, a chosen color swatch   |
| `kRadiusLarge`          | 16     | Groups, menus, a dragged tile                      |
| `kRadiusLargeIncreased` | 20     | Reserved                                           |
| `kRadiusExtraLarge`     | 28     | Reserved                                           |
| full                    | h / 2  | Buttons, switches, sliders, search, nav indicator  |

Shapes change state by **morphing**, not by swapping:

- a hovered tile rounds its inner corners;
- a selected button rounds fully;
- a pressed button squares off;
- a chosen color swatch turns from a circle into a rounded square.

## Typography

The type is Segoe UI Variable. Titles use the Display optical size, and everything else uses Text.

| Role          | Size | Weight   | Use                                                     |
|---------------|------|----------|---------------------------------------------------------|
| `Number`      | 30   | Semibold | Game and chat levels on the mixer                       |
| `TitleLarge`  | 20   | Semibold | Page titles                                             |
| `TitleMedium` | 16   | Semibold | Two-line headers, the app name                          |
| `TitleSmall`  | 14   | Semibold | Section titles, in `primary`                            |
| `BodyLarge`   | 14.5 | Regular  | Row labels, list titles                                 |
| `BodyMedium`  | 13   | Regular  | Values, status, empty states                            |
| `BodySmall`   | 12   | Regular  | Supporting text, subtitles, hints                       |
| `LabelLarge`  | 13   | Semibold | Buttons, fields, chips, list positions                  |
| `LabelMedium` | 12   | Semibold | Button group labels, navigation labels                  |
| `LabelSmall`  | 10.5 | Semibold | Axis labels, badges                                     |

Single-line text ends with an ellipsis, which is kept for names that can be any length (apps,
devices, files). Supporting text wraps instead, and so do texts that explain something: a `NavItem`
subtitle that does not fit wraps and its row grows, and a `ValueItem` value that does not fit beside
its label moves below it, as in a two-line row. A button group label is never
cut: the buttons share the width equally while every label fits; otherwise each button gets its label's
width plus an equal share of the rest, and only a group that is still too tight drops to `LabelSmall`.
Still prefer short words, so groups keep their even look in both languages.

## Color

The scheme is built from two colors: the Game seed (the Windows accent, a preset or any color) and
the Chat color (paired with the seed automatically, a preset or any color). Tones are fixed per role,
so contrast holds for any choice in both light and dark themes. Hue and chroma come from OKLCH, tone is
CIE L*.

- **Soft colors.** No seed is used with more OKLCH chroma than `kMaxSeedChroma` (0.10, a little calmer
  than Material's tonal spot scheme), so even a neon pick becomes a pastel.
- **Chat stays apart from Game.** The Chat color keeps its chroma between 0.045 and 0.09, and next to a
  colorful Game seed it keeps at least 60° of hue from it; a closer pick moves out to that distance on
  its own side. Without a chosen color, Chat sits 200° around the hue circle from Game. Swatches show
  each color as it will be used, after these limits.
- **Contrast levels.** Standard follows the Material 3 tones, except the light `secondaryContainer`:
  tone 86 instead of 90, because tiles are translucent over a usually bright backdrop, where 90 would
  vanish against them. Medium and high move text, outlines and containers further from their surfaces
  and make the page and tiles less translucent (`surfaceAlpha` / `tileAlpha` .62 / .72, .80 / .86 and
  solid).

| Role                                          | Use                                                     |
|-----------------------------------------------|---------------------------------------------------------|
| `surface`                                     | Page (at the palette's `surfaceAlpha` over acrylic or mica) |
| `surfaceContainer`                            | Tiles, the navigation bar                               |
| `surfaceContainerHigh`                        | The mixer card, the search bar                          |
| `surfaceContainerHighest`                     | Unselected buttons in a group, dropdowns, stepper buttons, menus, a dragged tile |
| `onSurface` / `onSurfaceVariant`              | Text and icons / supporting text, values, inactive icons |
| `primary` / `onPrimary`                       | Selected buttons, switches on, section titles           |
| `secondary`                                   | The keyboard focus ring                                 |
| `primaryContainer` / `onPrimaryContainer`     | Recording a shortcut                                    |
| `secondaryContainer` / `onSecondaryContainer` | Navigation indicator, the selected menu item, an open dropdown, the pin, tonal buttons |
| `tertiary` / `onTertiary`                     | The Chat side everywhere                                |
| `error`                                       | Errors, Quit, Remove, the low battery popup             |
| `outline` / `outlineVariant`                  | Switch track off, field and chip outlines               |
| `inverseSurface` / `inverseOnSurface`         | Snackbar                                                |

Rules:

- **Game is the primary family and Chat the tertiary family, everywhere:** the mixer, the EQ, app
  groups, the tray icon and the popup. Where the two meet in one shape (the square and ring tray
  marks), they blend through the middle third.
- **The battery has a color of its own**, outside the scheme: red when empty, through yellow, to
  green when full (`BatteryColor`). The hue moves smoothly between three OKLCH stops (27°, 95°,
  145°) at one tone, so no step looks brighter than another: tone 76 on a dark taskbar or panel and 48
  on a light one. A monochrome tray icon stays monochrome.
- Disabled content is its normal color at `kDisabledOpacity` (.38). There is no separate gray.
- Color never carries meaning alone. Errors also carry text, and a selection is also filled, rounded
  or checked.

## States

States are layers of the content color over the element, using the Material 3 `StateTokens`:

| State   | Opacity | Token             |
|---------|---------|-------------------|
| Hover   | .08     | `kHoverOpacity`   |
| Focus   | .10     | `kFocusOpacity`   |
| Press   | .10, on top of hover | `kPressOpacity` |
| Drag    | .16     | `kDragOpacity`    |

### Keyboard focus

Everything that reacts to a click or a drag is a focus stop, in drawing order. `Tab` and `Shift + Tab`
move the focus and scroll the page so the focused element is in view; while a menu is open, the focus
stays among its items. `Enter` (and `Space` where no text field takes typing) presses, the arrows step
what the mouse wheel steps, and `Esc` goes back. The focused element carries the focus layer and a ring
in `secondary`, `kFocusRingWidth` (3) wide and `kFocusRingOffset` (2) outside the element, following its
shape. As in Material 3, the ring shows only while the keyboard moves the focus; using the mouse hides
it.

## Elevation

Nothing casts a shadow. Tiles are separated by tone and gaps. Whatever floats above the page (menus,
a dragged tile) is set apart by tone alone: it takes `surfaceContainerHighest`, the most raised
surface. Menus also dim the page behind them with the Material scrim (`kScrimOpacity` .32) in both
themes.
A dragged tile also carries the drag layer and is fully rounded. That is focus enough without a
shadow, and it is lighter to draw.

## Motion

Every animated value follows a damped **spring** toward its target. A spring keeps its velocity when
the target changes, so motion that is interrupted (a quick double toggle, a drag that changes
direction) never jumps. The values are the Material 3 Expressive motion scheme:

| Token                   | Damping | Stiffness | Feel                        | Use                                         |
|-------------------------|---------|-----------|-----------------------------|---------------------------------------------|
| `kSpringFastSpatial`    | 0.6     | 800       | Quick, a little bounce      | Switch handles, button shapes, nav indicator, tile corners |
| `kSpringDefaultSpatial` | 0.8     | 380       | Smooth, slight settle       | Tiles making room while dragging, sliders   |
| `kSpringSlowSpatial`    | 0.8     | 200       | Calm                        | Values that travel far: the level marks     |
| `kSpringFastEffects`    | 1.0     | 3800      | Instant, no overshoot       | Hover and press layers                      |
| `kSpringDefaultEffects` | 1.0     | 1600      | Quick fade                  | Selection colors, menu fade                 |
| `kSpringSlowEffects`    | 1.0     | 800       | Gentle fade                 | The "Balanced" chip                         |
| `kSpringScroll`         | 1.0     | 1200      | Glide                       | Pages, lists and menus scrolling            |

Rules:

- **Spatial springs move things** (position, size, corners) and may overshoot a little. **Effects
  springs change color and opacity** and never overshoot. Colors are clamped anyway, so a bouncing
  value can't produce a wrong color.
- Positions inside a scrolling page animate **relative to their list**, so scrolling moves them with
  the page at once and nothing lags behind.
- A dropped row settles from where it was let go, not from where it started.
- **Reduced motion** (Appearance → Reduce animations) makes every value jump to its end. Scrolling is
  the exception: it keeps a short critically damped glide (`kSpringReduced`), so the eye can follow
  where the content went. Wheel scrolling is clamped to the content's end, so it never overshoots.
- The **tray icon** uses the same springs outside the panel. Its two bar levels and the morph into
  the balanced mark run on `kSpringFastSpatial`, so a change shows in about 60 ms:
  - at the even balance the bars become the chosen mark: they turn into the equals sign, widen into
    the square or bend into the ring, and the colors blend only as the shape gets there;
  - when the balance moves, the mark turns back into bars while the bars glide to their new level;
  - switching from one mark to another passes through the bars;
  - the battery, lying on its side as in Material 3 Expressive: the bars close into the two halves of
    its body (they meet before the shape settles, so no seam shows), then the terminal grows on the
    right and the part past the charge fades to 40%. While charging, a bolt shows in the opposite
    tone of what lies under it. Without a known battery the bars stay, and the choice is offered only
    with a headset that reports one.
  The icon is redrawn every 15 ms only while something moves.

## Components

Each component returns a `CardRow` (height, draw function and the tile's hover id), or draws a whole
group. A page is a sequence of groups:

```cpp
float y = BeginScroll(Page::Outputs, kHeaderHeight);
y = DrawCard(y, {
    SwitchItem(kIdSwitchOnLoss, T(Text::SwitchOnLoss), on, toggle, T(Text::SwitchOnLossHint)),
    SwitchItem(kIdReturnToPreferred, ...),
}) + design::kSectionGap;
y = SectionTitle(T(Text::Priority), y);
y = PageHint(T(Text::PriorityHint), y);
y = DrawDragCard(y, kIdOutputList, ids, kRowHeight, drawRow, move, click);
EndScroll(Page::Outputs, kHeaderHeight, y);
```

| Component           | What it is                                                                    |
|---------------------|-------------------------------------------------------------------------------|
| `DrawCard`          | A group of tiles, one per row. One topic per group.                           |
| `SwitchItem`        | Label and switch; the whole tile toggles it.                                  |
| `ChoiceItem`        | Label above a connected button group of 2–5 options.                          |
| `DropdownItem`      | Label and a dropdown that opens the full list as a menu.                      |
| `NumberItem`        | Label and − value +; click the value to type it.                              |
| `ValueItem`         | Label, the current value and ›; opens a picker page.                          |
| `NavItem`           | Icon, title, subtitle and ›; opens a page.                                    |
| `HintItem`          | Supporting text or an empty state as a tile.                                  |
| `DrawDragCard`      | A group whose order the user drags.                                           |
| `DrawList`          | A scrolling group that fills the rest of the page (apps, devices).            |
| `SectionTitle`      | Names the groups that follow. The page's first group doesn't need one.        |
| `PageHint`          | A rule for the whole list below it.                                           |
| `DrawEditableValue` | The value field inside `NumberItem`.                                          |
| `DrawPinButton`     | In the title row of the top-level pages only.                                 |
| `DrawVolumeSlider`  | A volume slider whose active track also shows what plays (see *Meters*).      |
| `MicLevelItem`      | The microphone's level against 0 dBFS with zones, marks and a peak hold.      |
| `DrawPixelIcon`     | A GameDAC pixel icon as the display shows it; the icon picker is a grid of them, the chosen one on `primaryContainer`. |
| Template field      | `DrawEditableValue` across the row under its label, with tonal chips below that add the event's fields. |

### Lists you can reorder

- Each tile shows its place (1, 2, 3…) on the left and a drag handle on `kTrailingIconCenter`. While
  dragging, the numbers show where each row would end up.
- A press that doesn't move is a click. Moving 4 DIP starts a drag.
- The lifted tile follows the pointer: it is fully rounded, on `surfaceContainerHighest` with the
  drag layer, and has no shadow. The other tiles spring aside to show where it would land.
- Dropping it saves the new order at once. There are no up/down arrows anywhere.
- Buttons and switches inside a tile take their own clicks. The rest of the tile drags.

### Typing a value

- Clicking the value opens the field with the number selected (without its unit), so typing
  replaces it.
- The caret works like any text field:
  - ← → move it; Home and End or ↑ ↓ jump to the ends;
  - Shift extends the selection, and Ctrl+A selects everything;
  - a click places the caret, and dragging selects;
  - Backspace and Delete work on the caret or the selection.
- Enter applies the value and Esc cancels. A click elsewhere applies too.
- Values are clamped to the control's range. A comma works as the decimal point, and `k` means
  thousands (`1.5k` → 1500 Hz). Enter on text that isn't a number keeps the field open.
- The same field renames things, such as saved headset presets. A name starts at the left, keeps
  everything typed (up to 64 characters) and opens from the rename button.

### Lists of saved items

Saved items are managed where they are used, never on a page of their own. For example, the headset's
saved presets are rows inside the equalizer card:

- A click on the row applies the item. The active one is filled with `secondaryContainer`.
- Its actions are icon buttons on the right: rename (in place), copy, delete. Delete is in `error`.
- Delete moves the file to the Recycle Bin, so it doesn't ask for confirmation.

App rows on the mixer's group pages work the same way. An app that has a saved group gets a trash
button after its segmented group choice. The button is in `onSurfaceVariant`, not `error`, because
it only forgets the app's group: the app follows Game again, and a closed app leaves the list.

### Supporting text and empty states

- Supporting text says *what the setting does*. A page hint says *what the list contains*: for
  example, that device lists show only connected devices, and where to enable the others.
- Empty states are a `HintItem`: a sentence that says what to do.

### Meters

- **Volume with a meter.** Only on the mixer, under *Game* and *Chat*: each setting lives in one
  place. The percent sits on the chevron's line. The slider's active track (the volume) is the
  side's container color.
  Inside it, the side's own color fills as far as the current peak, so one control shows both how
  loud it is set and what plays right now. It uses a 12 DIP track and a 4 × 28 handle that narrows
  while held. The wheel moves it by 5%, and a double click sets 100%.
- **Microphone level.** A dB scale from −60 to 0 dBFS with marks every 12 dB. Up to −12 dB it is
  `primary` (speech), up to −3 dB `tertiary` (loud), then `error` (close to distortion). A peak mark
  holds for 1.2 s, and the number shows the held peak.
- Meters rise at once and fall about 39 dB a second. The panel reads them every 33 ms, and only
  while a page that shows them is open; it redraws only when a value changes.

### Lists with a block list

Output devices can be kept out of automatic switching. Each row of the priority list has a ⦸ button
(`kGlyphBlocked`) before its drag handle. A blocked device moves to its own group under the section
*Never switch to*, with ⦸ in place of its number and ↶ (`kGlyphUndo`) to bring it back. Both buttons
are `onSurfaceVariant`: nothing is deleted. A click on any row still uses the device.

### The pinned ChatMix card

On the *Mixer* page the ChatMix card stays in place under the title. The groups below it scroll on
their own, clipped at the card's lower edge, when they don't fit, as the equalizer page does with its
graph.

### Popup

The popup in the top-right corner shows the balance after a hotkey or the dial, or a short notice
in its place: a leading icon in a 44 DIP circle, a title (TitleMedium) and a detail line. A notice
holds 1.5 s. A **warning** (the low battery) tints the circle with `error`, draws the icon in `error`
and holds 5 s; the detail names the headset.

### The tour

The first run opens the panel on a short tour; *Settings → General → Show the tour* opens it again.
It is a page of its own without the navigation bar:

- a hero icon in a `primaryContainer` circle (80 DIP), the step's title (TitleLarge) and a lead
  line (BodyMedium, `onSurfaceVariant`), centered;
- three points as one group of tiles: a leading icon in `primary` and a line or two of text;
- a page indicator: the current step is a 24 DIP `primary` pill, the others 8 DIP `outlineVariant`
  dots, and the widths spring (fast spatial);
- *Skip* (a text button) top right until the last step, *Back* (text) bottom left from the second,
  *Next* and finally *Get started* (tonal) bottom right.

Steps change on a shared axis: the new step slides 48 DIP in from the side it comes from and fades
in on the default spatial spring. With reduced motion it changes at once.

### Tooltips

Icon buttons and icon-only choices carry a Material 3 plain tooltip: BodySmall in `inverseOnSurface`
on `inverseSurface`, 4 DIP corners, 8 DIP side padding, at least 24 DIP high and up to two lines
(232 DIP wide). It appears 0.5 s after the pointer rests on the control, or at once when keyboard
focus lands there, fades in on the fast effects spring, and hides on a press, when the pointer
leaves and when the page changes. It sits 4 DIP below the control, or above it where the page ends.

### Tray card and menu

The tray icon has no plain shell tooltip; it gets Material 3 surfaces of its own, next to the taskbar
on whichever edge it is, with a soft level 2 shadow and an `outlineVariant` outline over any desktop:

- **Card** (a rich tooltip) while the pointer rests on the icon: `surfaceContainer`, 12 DIP corners,
  the app name (TitleSmall), the device with its battery (BodyMedium, `onSurfaceVariant`) and the
  balance in the Game and Chat colors. It ignores the mouse and never takes focus.
- **Menu** on a right click: `surfaceContainer`, 16 DIP corners, the status as a header (LabelMedium),
  40 DIP items with a leading icon, a check for *Start with Windows*, a divider, and *Quit* in `error`.
  ↑ ↓ Home End move, Enter or Space chooses, Esc or a click elsewhere closes.

Both rise 8 DIP from the taskbar while they fade in over 150 ms; with reduced motion they appear at
once.

### Navigation bar

There are three or four destinations, each an icon with its label below. The active one gets a
`secondaryContainer` pill that springs open around the icon (fast spatial spring). Its label and icon
fade to full contrast.

### Pin

The pin in the page title keeps the panel open when focus leaves it, for example while you adjust a
plugin window. It fills with `secondaryContainer` while it is on, and its icon becomes the filled pin.

## Writing

- Use sentence case everywhere.
- Labels name the setting; supporting text says what happens. Don't repeat the label in the hint.
- Use the user's words ("headset", "game", "chat"), not driver or API terms.
- Every string exists in English and Ukrainian (`src/i18n.cpp`). Labels in button groups must fit
  both.

## Checklist for a new page or setting

1. The page is built from the components above, with no raw sizes, colors or speeds.
2. The control matches the kind of choice (see *Controls*). No setting offers two controls for one
   choice.
3. Each group holds one topic. Sections have titles, and lists that need it have a page hint.
4. Every non-obvious setting has supporting text, in both languages.
5. It works with reduced motion, light and dark themes, every contrast level, and any Game and Chat
   colors.
6. Every control can be reached with `Tab` and used with `Enter` or the arrows: it registers its hit
   with an id and a click, drag or wheel action.
7. It scrolls when it overflows, and the last group ends `kGroupGap` above the scroll end.
