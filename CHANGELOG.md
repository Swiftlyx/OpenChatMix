# Changelog

## 1.0.0 — 2026-10-14

First public release.

- ChatMix from the headset dial for the Arctis Nova Pro Wired / Wireless, Nova 7 family, Arctis 7+
  family and Nova 5X, without SteelSeries GG or Sonar.
- ChatMix icon on the GameDAC display while the app runs; the dial returns to plain volume control on exit.
- Dial mode (ChatMix or volume) shown in the panel; volume reports are never used for the balance.
- Game, Chat and Media groups with manual assignment of running apps or any `.exe`; the Media group
  can be turned off.
- Global hotkeys with an on-screen popup; works with any output device when no headset is connected.
  Optional hotkeys mute chat and switch to the next EQ preset. A hotkey can be a single key, such as
  a keyboard's volume knob.
- Without a ChatMix headset the balance is set in single percents (slider and arrow keys) or 10%
  steps (wheel and hotkeys); with one it keeps to the dial's 12 steps per side.
- Optional daily update check against the GitHub releases; updates install with one click after a
  SHA-256 check and restart the app.
- Material 3 tray panel with a navigation bar: automatic, light or dark theme; the Windows accent,
  a preset or a custom color; Acrylic, Mica or solid background; reduced motion; colored or
  monochrome tray icon.
- English and Ukrainian interface.
- Detects a running SteelSeries GG, re-initializes the headset when GG exits, after sleep and on
  session unlock; *Reconnect headset* in the tray menu.
- Migrates settings and the startup entry from the NovaChatMix prototype.
- Native headset settings per model: Engine EQ (SteelSeries presets and a 10-band custom curve),
  microphone, sidetone, mic mute LED, gain, volume limiter, GameDAC display, power, wireless and
  Bluetooth options, battery level. The Nova Pro Wired reports its current EQ and microphone settings.
- EQ presets can be saved to and imported from JSON files.
- Optional virtual device mode: Game and Chat come from two virtual audio devices and are mixed into
  the headset, instead of changing app volumes. VB-Audio Hi-Fi Cable, Virtual Audio Cable and VB-CABLE
  A–D are picked automatically; the devices are switched on and made the Windows defaults only while
  the mode runs. Chat apps are sent to the Chat device and other apps to the Game device on their own
  (optional).
- 10-band parametric equalizer for Game and Chat in virtual device mode, with a Sonar-style response
  graph that stays in view while editing, Bass Boost, Voice Clarity, Treble Boost and Smart Volume,
  built-in presets, preset files and the presets of a local SteelSeries Sonar installation. It can be
  turned off in the settings.
- Game presets can be put on the headset's own Engine EQ, converted to its ten bands; an unsaved
  custom curve is backed up first. Presets stored on the headset show their curve.
- Dropdown lists for presets, filter types and game profile settings.
- Balance changes reach the GameDAC reliably: the screensaver hands the display back first, quick
  changes are spaced out, and a change the device missed is sent again.
- Material 3 tray icon: rounded, anti-aliased bars that shrink with their level. The panel and the
  popup mark the even balance.
- Optional ducking (game gets quieter while chat plays), game profiles (balance, Game EQ and headset
  EQ while a game runs), a popup for the ChatMix wheel on headsets without a display, and a low
  battery warning.
- Screensaver for the Nova Pro GameDAC / base station OLED: stars, bouncing headphones, clock, Game of
  Life, screen off, or your own image or GIF while no sound plays.
- Panel and popup animations run at the monitor's refresh rate.
- The SteelSeries GG warning only appears with a SteelSeries headset, once per GG start, and can be
  hidden or turned off.
