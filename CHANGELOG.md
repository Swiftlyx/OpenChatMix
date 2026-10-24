# Changelog

## 2.6.0 — 2026-10-24

- Media group: a volume slider and a level meter on the *Mixer* tab, like Game and Chat.
- Undo for the equalizers: `Ctrl + Z` undoes a change on the *Equalizer* and *Headset* tabs, `Ctrl + Y` or
  `Ctrl + Shift + Z` redoes it, up to 100 steps. In a number field it undoes the typing first.
- Any app can leave its group, from Game too. The trash icon in *Game* leaves an app out of ChatMix: it keeps
  its own volume, belongs to no group and, in virtual device mode, plays past the virtual devices. *Add* in
  any group lists it as "out of ChatMix" and brings it back.
- The trash icon shows next to an app added by hand right away, not only after the panel opens again.
- Without virtual devices the Game, Chat and Media meters show only what their own apps play. Apps that
  record the headset's sound, such as OBS capturing desktop audio or Discord in a call, showed everything
  that played in their group's meter. The same mistake made ducking lower Game for any sound while Discord
  was in a call and kept GG's Discord screen up for music.
- `Ctrl + A` in a text field keeps the text selected, so typing replaces it.

## 2.5.8 — 2026-10-24

- Arctis Nova Elite: the microphone's own equalizer (experimental), the third choice of the switch above the
  equalizer on the *Headset* tab. It works in the headset itself, without a virtual cable or added latency:
  GG's Balanced, Broadcast High Pitch, Clarity High Pitch and Deep Voice presets, Flat and ten band sliders.
  From [@Lasfecas](https://github.com/Lasfecas)'s USB capture in [#6](https://github.com/Swiftlyx/OpenChatMix/issues/6); the presets match what GG sends.
- The Elite's Bluetooth equalizer works, as [@Lasfecas](https://github.com/Lasfecas) reports in [#6](https://github.com/Swiftlyx/OpenChatMix/issues/6).

## 2.5.7 — 2026-10-23

- Arctis Nova Elite: the equalizer of the sound over Bluetooth (experimental), with the SteelSeries presets and
  ten band sliders. A switch above the equalizer on the *Headset* tab chooses between it and the main one
  (*2.4 GHz*).
- Nova Elite and Nova 7 Gen 2: Bass Boost, Focus and Smiley go to the headset the way GG sends them, with
  shelves on the lowest and highest band; on the Elite with their names too. The Elite now gets notch filters
  as notches (in GG's DOOM: The Dark Ages, for one).
- The Focus preset had its 4 kHz, 8 kHz and 16 kHz bands wrong; it now matches GG's. On headsets that store
  their presets themselves (Nova Pro) this only changes how its curve is drawn.
- All of this comes from [@Lasfecas](https://github.com/Lasfecas)'s second USB capture in [#6](https://github.com/Swiftlyx/OpenChatMix/issues/6), which also confirms the shelf and pass
  filter types.

## 2.5.6 — 2026-10-23

- Arctis Nova Elite and Nova 7 Gen 2: the equalizer on the *Headset* tab is parametric, like the one on the
  *Equalizer* tab: ten filters on a response graph, each with its own type, frequency, gain and Q, dragged
  on the graph or set in the band editor below it. The SteelSeries presets and saved curves still apply.
- On these headsets GG's game presets go on with their own filters instead of being fitted to ten bands;
  for the presets in the USB capture from [@Lasfecas](https://github.com/Lasfecas) in [#6](https://github.com/Swiftlyx/OpenChatMix/issues/6) the bytes match what GG sends.
- Saved headset presets keep these filters, along with the closest ten-band curve for other headsets.

## 2.5.5 — 2026-10-23

- Arctis Nova Elite: the equalizer on the *Headset* tab (experimental), with the SteelSeries presets (Flat,
  Bass Boost, Focus, Smiley), the custom 10-band curve, game presets and saved presets. It goes to the base
  station the way SteelSeries GG sends it, as [@Lasfecas](https://github.com/Lasfecas)'s USB capture with GG in [#6](https://github.com/Swiftlyx/OpenChatMix/issues/6) shows.

## 2.5.4 — 2026-10-23

- The hover card over the tray icon no longer gets a small empty box on top of it: Windows 11 sometimes
  showed an empty tooltip of its own there, for example after the app started with Windows. The card
  now stays above anything the taskbar shows while it is up.

## 2.5.3 — 2026-10-22

- Noise control switched with the headset's button (Nova Pro Wireless, Nova Elite) shows a popup with
  the new mode: noise cancelling, transparency or off. On the Nova Pro Wireless the base station screen
  shows it for a moment too, while SteelSeries GG is closed. Each has its own switch in
  *Settings → Notifications*. Requested by [@Katan21](https://github.com/Katan21) in [#5](https://github.com/Swiftlyx/OpenChatMix/issues/5).

## 2.5.2 — 2026-10-22

- Arctis Nova Pro Wireless: noise control on the *Headset* tab (off, transparency or ANC, with the
  transparency level), the Bluetooth options (on together with the headset, lower game audio during
  calls) and the spare battery in the base station next to the headset's, on the *Headset* tab and in the
  tray. The tab follows changes made with the headset's button or on the base station, and the battery
  updates as soon as the base station reports it. Commands as [NovaPanel](https://github.com/thibautgnt/NovaPanel) documents them for this base
  station. Requested by [@Katan21](https://github.com/Katan21) in [#5](https://github.com/Swiftlyx/OpenChatMix/issues/5).

## 2.5.1 — 2026-10-22

- Presets from SteelSeries GG are read again: current GG keeps Sonar's database in a new place. Chat
  presets in their new format, Sonar's media presets for the Media channel and 46 presets that GG wrote
  with trailing commas are read too, and notch filters keep their type.
- GG Engine's game and microphone presets that Sonar lacks join the list (46 game presets, mostly
  console and mobile games); the section is now called *From SteelSeries GG*.
- App profiles: the *Game equalizer* menu starts with GG's presets made for the profile's game
  (*For this game*), found through GG's game library and Sonar's links to games, or by the game's name.
- Preset menus filter as you type, with a *Type to search…* hint at the top; Ctrl+A, Ctrl+C, Ctrl+X,
  Ctrl+V and Ctrl+Backspace work there, and Esc clears the search first. *Manage presets…* moved up,
  above GG's list, and stands out with an icon and a chevron.
- Long menus and pages stay smooth: only the rows in view are drawn, menus scroll three items per notch,
  and scrollbars can be dragged.
- The *Game presets* menu on the *Headset* tab no longer repeats presets the headset has itself (Flat,
  Bass Boost).
- Nova 7 Gen 2: the equalizer's title counts down to the delayed save and then says *Done*.
- Text fields take Ctrl+C, Ctrl+X, Ctrl+V and Ctrl+Backspace.

## 2.5.0 — 2026-10-21

- NVIDIA noise removal for RTX graphics cards, as a plugin: with NVIDIA's Audio Effects redistributable
  installed for the card, add its `NVAudioEffects.dll` under *Plugins* (the microphone, or a channel of
  virtual device mode at 48 kHz). The card below the plugin list chooses noise, room echo or both and the
  strength, and shows the model's state; the model loads on the graphics card in the background, and the
  sound passes unchanged until it is ready. Requested by [@liquidshadowfoxx](https://github.com/liquidshadowfoxx) in [#6](https://github.com/Swiftlyx/OpenChatMix/issues/6).
- The log can be turned on in *Settings → General → Diagnostics*, at once and without editing the
  settings file; *Open the log folder* shows `OpenChatMix.log`. Requested by [@liquidshadowfoxx](https://github.com/liquidshadowfoxx) in [#6](https://github.com/Swiftlyx/OpenChatMix/issues/6).
- Popups for the knob of a GameDAC or base station: turned in volume mode it shows the headset volume,
  in ChatMix mode the balance. Each has its own switch in *Settings → Notifications*, on by default and
  shown only where there is such a knob; the switch for the headset wheel is now shown only for headsets
  with one. Requested by [@liquidshadowfoxx](https://github.com/liquidshadowfoxx) in [#6](https://github.com/Swiftlyx/OpenChatMix/issues/6).
- *Settings → Notifications* has one switch for all popups (the low battery warning keeps its own) and a
  choice of the screen corner they show in.
- The Arctis Nova Elite's spare battery in the base station shows next to the headset's on the
  *Headset* tab and in the tray. Requested by [@liquidshadowfoxx](https://github.com/liquidshadowfoxx) in [#6](https://github.com/Swiftlyx/OpenChatMix/issues/6).
- Nova 7 Gen 2 protocol: the equalizer is saved to the headset once a run of changes has ended (3 seconds
  after the last), so clicking through presets pops once instead of on every click; other headsets save
  as before. Reported by [@zer0ed](https://github.com/zer0ed) in [#7](https://github.com/Swiftlyx/OpenChatMix/issues/7).
- Confirmed: the Gen 2 protocol equalizer and the output switching with the headset's power on both the
  Nova 7 (`22A1`) and the Nova 7 Gen 2 (`227E`), by [@zer0ed](https://github.com/zer0ed) in [#7](https://github.com/Swiftlyx/OpenChatMix/issues/7); the Nova Elite's settings and output
  switching, by [@liquidshadowfoxx](https://github.com/liquidshadowfoxx) in [#6](https://github.com/Swiftlyx/OpenChatMix/issues/6).

## 2.4.1 — 2026-10-21

- Arctis Nova Elite settings on the *Headset* tab, experimental: microphone volume, sidetone for the boom
  and the built-in microphone, mute LED brightness, noise control (off, transparency or ANC) with its
  level, volume limiter, line out with the stream mix, display brightness, screen dimming and home screen,
  auto power-off, and the Bluetooth options. They follow the Elite's protocol description in
  Linux-Arctis-Manager and the base station's events in the log from [#6](https://github.com/Swiftlyx/OpenChatMix/issues/6); auto power-off and saving
  to the headset were confirmed by [@liquidshadowfoxx](https://github.com/liquidshadowfoxx). The tab reads the current values from the base station and
  follows changes made on it or in SteelSeries GG, saves half a second after the last change and says the
  settings are experimental. The Elite's own equalizer is not supported yet.

## 2.4.0 — 2026-10-21

- VST 2 and VST 3 plugins on the Game, Chat and Media channels of virtual device mode: the *Equalizer*
  tab has a *Plugins* list for each channel below *Effects*, like the microphone's (drag to reorder, a
  switch per plugin, click to open its window). A convolver with impulse responses, crossfeed, a loudness
  or a bass plugin you own shapes the sound without a system-wide driver. Plugins run in stereo at the
  channel's own sample rate, before its volume, so a compressor does not undo the ChatMix balance, and
  when a sound stops they get silence until their latency and reverb tail have played out. Nothing loads
  for a channel without plugins; they load with virtual device mode and the equalizer, stay loaded while
  the bridges restart and keep their settings in `PluginStates`.
- DeepFilterNet works on Chat too: added there, [`DeepFilterNet-OpenChatMix.dll`](https://github.com/Swiftlyx/OpenChatMix/releases/latest/download/DeepFilterNet-OpenChatMix.dll) removes noise from the
  voices of others. On a channel it filters the mix of both sides and adds about 30 ms.
- Plugin latency counts: VST 2 and VST 3 plugins that delay the sound (look-ahead, linear phase) are now
  part of the latency shown for the microphone chain and virtual device mode; before, they counted as
  0 ms. The *Plugins* hint shows what a channel's plugins add.
- The audio threads no longer wait for a VST 3 plugin's window: parameter changes made there while a block
  runs reach the next block.
- *Keep levels at 100%* missed the recording end of the cable (*CABLE Output*) when one end had been renamed
  in the Windows sound settings, for example to *CABLE Input (Mic)*: the two ends were paired by name only.
  They are now also paired by the cable's driver device, so the level holds there too.
- Each group can play on a device of its own without virtual cables, for example Game and Chat on the
  headset and Media on the speakers: *Settings → Audio → Audio routing* has a row per group while virtual
  device mode is off. Windows sends the group's apps there through its per-app device, ChatMix still
  balances Game and Chat on whichever device they play, and OpenChatMix clears the choice again when it
  exits. A group whose device is not connected plays on the Windows default meanwhile; sound on those
  other devices no longer keeps the GameDAC screensaver away.
- Output devices follow a wireless SteelSeries headset's power: while the headset is switched off it counts
  as disconnected, so *Switch when a device disconnects* moves the sound to the next device and *Return to a
  device that reconnects* brings it back when the headset is switched on; the *Next output device* hotkey
  skips it meanwhile. The base station or dongle stays in Windows as a playback device, so this goes by the
  headset's own status, which is now asked for the moment the wireless link changes. Requested by [@Katan21](https://github.com/Katan21) in [#5](https://github.com/Swiftlyx/OpenChatMix/issues/5) and
  [@liquidshadowfoxx](https://github.com/liquidshadowfoxx) in [#6](https://github.com/Swiftlyx/OpenChatMix/issues/6).
- The Nova 7 Gen 2 protocol equalizer is confirmed on the Arctis Nova 7 (`22A1`), reported by [@zer0ed](https://github.com/zer0ed) in [#7](https://github.com/Swiftlyx/OpenChatMix/issues/7).

## 2.3.0 — 2026-10-21

- AI noise suppression as an optional plugin: [`DeepFilterNet-OpenChatMix.dll`](https://github.com/Swiftlyx/OpenChatMix/releases/latest/download/DeepFilterNet-OpenChatMix.dll) is OpenChatMix's build
  of [DeepFilterNet](https://github.com/Rikorose/DeepFilterNet) by Hendrik Schröter, with its DeepFilterNet3 model inside. Download it and add it
  in *Settings → Microphone → Plugins* like a VST plugin; *Max noise reduction* and *Post-filter* appear below
  the list. It adds about 20 ms and nothing is loaded until it is added. The plugin section takes any plugin
  now, and the chain's latency includes what plugins add.
- OpenChatMix could stop responding and be closed by Windows while the GameDAC visualizer was set to music
  and video: the check whether media plays asks other processes and ran on the app's main thread. It runs
  on a thread of its own now, so a stalled media app or Windows service can no longer freeze OpenChatMix.
- Plugins that crash in their own unload code, such as Supertone Clear, no longer take OpenChatMix down
  when the microphone chain stops or the app exits: plugin modules stay loaded until the app ends, after
  the app volumes are restored and the settings saved.
- `Log=1` set in `OpenChatMix.ini` while OpenChatMix runs is no longer overwritten when it exits, and a log
  file still held by an instance that is ending no longer leaves the new one without a log (it writes
  `OpenChatMix-<process ID>.log` beside it). Reported by [@liquidshadowfoxx](https://github.com/liquidshadowfoxx) in [#6](https://github.com/Swiftlyx/OpenChatMix/issues/6).
- With the log on, a hang of the app's window thread writes `OpenChatMix-hang-<time>.dmp` next to the log, so
  the cause can be found.
- The Arctis Nova Elite is confirmed: the ChatMix dial and the battery work, reported by [@liquidshadowfoxx](https://github.com/liquidshadowfoxx) in [#6](https://github.com/Swiftlyx/OpenChatMix/issues/6).

## 2.2.0 — 2026-10-20

- Experimental support for the Arctis Nova Elite (`2244`, `2270`): the ChatMix dial and the battery,
  built from published descriptions of its protocol ([HeadsetControl #481](https://github.com/Sapd/HeadsetControl/issues/481) and Linux-Arctis-Manager) and not yet
  tried on the headset. Requested in [#6](https://github.com/Swiftlyx/OpenChatMix/issues/6).
- With the log on and no supported headset connected, a SteelSeries headset OpenChatMix does not know
  is recognized by its audio devices: the log lists its HID collections and the reports it sends, read
  only, so a log is enough to add a new model.

## 2.1.0 — 2026-10-20

- The *Screensaver on/off* hotkey (formerly *Screensaver now*) keeps the GameDAC screensaver on until
  you press it again, also while sound plays. Turning the knob or a change made in OpenChatMix shows the
  GameDAC's own screen for three seconds, then the screensaver comes back. Requested by [@Katan21](https://github.com/Katan21) in [#5](https://github.com/Swiftlyx/OpenChatMix/issues/5).
- Nova Pro Wireless: the battery and 2.4 GHz link reports the base station sends on its own no longer
  count as using the GameDAC, so they no longer end the screensaver.
- The Bluetooth icon on the screensaver follows the headset's LED: steady with a device connected,
  blinking slowly while Bluetooth waits for one and fast while it pairs. Suggested by [@Katan21](https://github.com/Katan21) in [#5](https://github.com/Swiftlyx/OpenChatMix/issues/5).
- Bluetooth headphones show their battery beside the app's name, in the tray tooltip, as the tray icon
  at the even balance and in the low battery popup, when Windows gets the level from them.
- Experimental equalizer for the Nova 7 Gen 2 protocol: the Nova 7 Gen 2 (`227E`) and the Nova 7
  models on the Gen 2 firmware (`22A1`, 7X `229E` / `22A5` / `22AD`, Diablo IV `22A9`, WoW `22AB`)
  ignore the older 10-band command, so the curve is now sent as ten peak filters, as
  Linux-Arctis-Manager describes their protocol. These models also save their settings like the
  Nova 7 Gen 2. Reported by [@zer0ed](https://github.com/zer0ed) in [#7](https://github.com/Swiftlyx/OpenChatMix/issues/7); the *Headset* tab marks it as experimental until it is confirmed.
- After a SteelSeries headset disconnects, the panel no longer shows its last battery level.
- The Ukrainian translation reads more naturally: no repeated words, and the same terms as Windows
  (пристрій виводу, гаряча клавіша, самопрослуховування).

## 2.0.0 — 2026-10-19

- The tray icon animates: at the even balance the two bars turn into the equals sign, widen into the
  square or bend into the ring, and turn back into bars as soon as the balance moves; the bars also
  glide to their new level. Fast Material 3 springs drive it; with reduced motion it changes at once.
- Apps can be removed from a group: the trash icon next to an app in *Chat* or *Media* forgets its
  group, so it follows Game again (in virtual device mode it moves to the Game device), and a closed
  app leaves the list. Requested by [@zer0ed](https://github.com/zer0ed) in [#3](https://github.com/Swiftlyx/OpenChatMix/issues/3).
- The Xbox party chat (`XboxPartyChatHost.exe`) starts in the Chat group, as in SteelSeries GG. Settings
  from an earlier version get it once; removing it later sticks. Suggested by [@zer0ed](https://github.com/zer0ed) in [#4](https://github.com/Swiftlyx/OpenChatMix/issues/4).
- Discord and the Xbox party chat appear in the Chat group only once they are installed (Discord in
  its usual folder, the party chat with the Xbox app); until then nothing is shown, and they still
  go to Chat when they first run.
- Battery level on the Arctis Nova 7, 7 Gen 2, 7X, 7+ and Nova 5X: read from the dongle's status
  every 30 seconds, so the tray tooltip shows it and the low battery warning works on these headsets
  (before, only the Nova Pro Wireless reported it). Older Nova 7 / 7X firmware and the Arctis 7+
  report the level in five steps (0, 25, 50, 75, 100%).
- The Arctis Nova 7 (`22A1`) and Nova 7 Gen 2 (`227E`) are confirmed working: ChatMix dial and
  headset settings, reported by [@zer0ed](https://github.com/zer0ed) in [#1](https://github.com/Swiftlyx/OpenChatMix/issues/1) and [#2](https://github.com/Swiftlyx/OpenChatMix/issues/2).
- The Arctis Nova Pro Wireless is confirmed working, reported by [@Katan21](https://github.com/Katan21) in [#5](https://github.com/Swiftlyx/OpenChatMix/issues/5).
- The microphone level meter no longer stalls while it falls after you stop talking, and it moves at
  the display's frame rate between readings.
- The tray icon can show the headset battery: *Icon when balanced* has a *Battery* choice with a
  headset that reports one. At the even balance the bars close into a battery lying on its side, as in
  Material 3 Expressive: the charge in the theme's color or by the charge, smoothly from green (full)
  through yellow to red (empty), the rest of the body faded, and a bolt while it charges. Headsets that
  report no battery keep the bars.
- Virtual device mode: every channel chooses where it plays, so Game and Chat can stay on the headset
  while Media goes to speakers. Media can have a virtual device of its own (chosen by hand); its apps
  are sent there, ChatMix leaves it alone, its Windows volume applies, and the *Equalizer* tab gets a
  Media channel. *Settings → Audio* is arranged as Groups, Audio routing (a row per channel, the
  output, then the device switches) and Equalizer.
- The tray icon's tooltip is a Material 3 card with the device, its battery and the balance, and the
  right-click menu is a Material 3 menu with icons that also works from the keyboard.
- Icon buttons in the panel have Material 3 tooltips (after a short rest of the pointer, or at once
  with keyboard focus): *Never switch to* and its undo, removing an app from a group, the pin, the
  preset buttons, the tray marks and more.
- The pin fills while it is on.
- GameDAC line out (Nova Pro): *Speakers* or *Streaming* on the *Headset* tab, read from the device;
  in Streaming the stream mix of the main sound, the aux input and the microphone has a slider each.
- The GameDAC screensaver also runs while SteelSeries GG is open, and the new *Screensaver now*
  hotkey shows it at once (or hides it); it then stays until the headset or the app is used.
- The screensaver gives way to SteelSeries GG's own apps for the display (Discord, Now Playing,
  Spotify, Audio Visualizer) that are switched on in GG. It watches their helper processes: the moment
  one hands GG something to show, with or without sound (a message, your own voice or mute in a Discord
  voice channel, a track that starts, pauses or plays on muted), the screensaver stops drawing, even in
  the middle of a frame, and waits for as long as GG shows it: 6 seconds for Discord, 3.5 for a track,
  and as long as someone keeps talking in a voice channel. The heartbeat each helper sends GG is learned,
  kept in the settings and ignored. After something shown without sound the screensaver goes on by itself,
  taking the display back from the device as cleanly as when it starts; a sound still ends it as usual.
  Nothing is watched while GG is closed, its display apps are off or
  the screensaver is off; while the screensaver waits the helpers are checked once a second. Other chat
  apps no longer hold it, since GG does not show them. The *Screensaver* card lists the GG apps it gives
  way to.
- With the Arctis Nova Pro Wireless the screensaver can show the base station's Bluetooth: an icon in
  a corner of your choice (top right by default, or off) while Bluetooth is on, blinking while it pairs
  and moving by a pixel every half minute so it does not burn in. The state is read from the status
  reply the app already asks for, as [Linux-Arctis-Manager](https://github.com/elegos/Linux-Arctis-Manager)
  reads it; it is not tested on a Wireless base station yet.
- **Visualizer** (*Headset* → *Visualizer*, Nova Pro): bars that follow the sound fill the whole GameDAC
  screen while something plays, 32 of them with falling peak marks, at about 17 to 23 frames a second, as
  fast as the GameDAC takes them, across the whole range of hearing from 20 Hz to 20 kHz. It shows for music and video, while an app tells
  Windows it plays (the media controls Windows shows: players, YouTube in the browser and the like), or
  for any sound. OpenChatMix owns the screen meanwhile, so the GameDAC's own screen does not cut in;
  turning the knob shows it for three seconds, add-on screens go on top, and a screensaver started by
  hand goes first. It listens to the headset only while it shows.
- **3rd party add-ons** (*Settings* → *3rd party add-ons*): programs made for SteelSeries GameSense can
  show on the GameDAC screen through OpenChatMix while SteelSeries GG is closed. You add a program you
  already have by choosing its file; OpenChatMix includes none. It runs a GameSense receiver on 127.0.0.1
  only, puts its address where GameSense programs look for GG (`coreProps.json`, put back afterwards),
  starts the programs with itself and ends them with it, and stops them when GG starts. A program for
  Discord, such as GG's (`gamesense-discord-x64.exe` in GG's folder), known by "discord" in its file name,
  gets the same choices as in GG's Engine: for each
  of its eight events (a message, someone talking, you talking, your mic muted, someone joining or
  leaving your channel, you joining or leaving a channel) a screen of displays shown in turn, once, a
  number of times or while the event lasts. A display has one of 18 pixel icons, a bold heading and a
  text with the event's fields ({title}, {message}, {user}, {channel}), or an image or GIF, for a time
  of its own. *Show on the headset* tries a screen with sample text. Other GameSense programs show the
  screens they describe themselves. Add-on screens go over the screensaver, or over the GameDAC's own
  screen for as long as they last.
- The GameSense receiver for add-ons answers programs only: requests a web page in a browser could send
  to it are refused.
- A preview build (`tools\preview\build.bat`) shows the real panel with a simulated headset of each
  family, another headset, speakers or none, with its battery, Bluetooth and SteelSeries GG, so every
  page can be checked without the hardware.
- A *Next microphone* hotkey (*Settings* → *Hotkeys*, shown while the microphone is processed) feeds
  the microphone chain from the next connected microphone, the one the virtual microphone then plays,
  with a popup naming it. Virtual cables are skipped, as in the microphone picker; with a single
  microphone the popup says there is no other.
- **How the headset is connected** shows beside its name on the *Mixer* tab, as a small outlined chip:
  USB for the Nova Pro Wired, 2.4 GHz for the wireless models (2.4 GHz + BT while the Nova Pro Wireless
  also plays from a Bluetooth device), and for other headsets and speakers what Windows tells about the
  device: USB, Bluetooth, 3.5 mm (a jack of the sound card), HDMI or S/PDIF. Built-in speakers and
  virtual devices show none. When the line runs short, the dial mode gives way first, then the name is
  cut.
- **The battery takes the theme's color** by default, on the *Mixer* tab and in the tray: the
  accent you chose, or the Windows one. *Settings* → *Appearance* → *Battery color* colors it by its
  charge instead, from green to red, for the panel and for the tray each; the choice shows with a
  headset that reports its battery.
- **Export and import of all settings** (*Settings* → *General* → *Backup*): *Export* saves every
  setting, the presets and the microphone plugins' states in one ZIP file; *Import* takes them from such
  a file and restarts OpenChatMix. The current settings are kept first in
  `%APPDATA%\OpenChatMix\Backups`, and presets the file does not have stay.
- The screensaver card has a *Test* button that shows the animation for a few seconds; choosing an
  animation no longer shows it right away.
- Texts in the panel are no longer cut off: a subtitle that does not fit wraps onto another line, a
  value too long for its row (such as a long device name) moves below its label, and an app that is
  not running shows that before its file name.
- Hotkeys list *Mute game* before *Mute chat*, and *Mute media* mutes the Media group (shown while the
  group is in use).
- The tray card appears after the standard tooltip delay, and Windows no longer draws an empty
  tooltip next to it.
- Game profiles became *App profiles*: besides the balance and the headset equalizer, each app can set
  the Game, Chat and (with a Media device) Media equalizer presets while it runs.
- A *Mute game* hotkey, next to *Mute chat*.
- A wireless headset's battery shows at the top of the *Mixer* tab, right of the app's name, as an
  icon in the tray icon's style with the percent.
- The language is chosen from a list, ready for more languages.
- The groups on the *Mixer* tab list as many apps as fit beside the volume, then how many more, and
  their volume sliders have more room below.
- Virtual device mode only accepts virtual cables as the Game, Chat and Media devices. Choosing the
  headset as the Media device could switch the headset off in Windows when the mode stopped; now a
  device that is not a virtual cable is never switched off.
- The low battery warning is a popup like the ChatMix one, top right, held for 5 seconds, instead
  of a Windows notification. Batteries that report five steps (older Nova 7 / 7X firmware, Arctis
  7+) warn at 25%, their lowest step above empty.
- Output devices: *Never switch to* keeps a device out of switching after a disconnect and out of
  the *Next output device* hotkey; a click on it still uses it. The page says more clearly that
  disconnected and disabled devices are not listed and that they are turned on in the Windows sound
  settings.
- The ChatMix card on the *Mixer* page stays in place; the groups below it scroll when they don't
  fit.
- New installs start with the Windows accent color and with the Media group and the equalizer turned
  off. Settings from an earlier version keep their choices.
- A short tour on the first start: the balance and hotkeys, groups, the extras that are off, and
  where to make the app your own. *Settings → General → Show the tour* opens it again. Its steps keep
  one layout, so the tiles stay in place from step to step.
- With no game profile and no SteelSeries headset, the app no longer reads the process list every
  5 seconds.

- The whole panel works from the keyboard: `Tab` and `Shift + Tab` move between controls and scroll
  the page to them, `Enter` (or `Space`) presses, the arrows change sliders and values, `Esc` goes
  back. A Material 3 focus ring shows where you are; it hides again once the mouse is used.
- Contrast levels in *Settings → Appearance*: standard, medium or high. Higher levels darken text,
  outlines and containers in the light theme and brighten them in the dark one, and make the panel's
  surfaces less translucent (fully solid at high).
- In the light theme, slider tracks, the *Balanced* chip and the selected navigation item no longer
  fade into the cards over a bright desktop, and menus dim the page behind them as much as in the dark
  theme.
- Button group labels are never cut off: buttons take the width their labels need (*Bass Boost* on
  the headset equalizer), and only a group that is still too tight uses a smaller size.

- ChatMix no longer moves the app sliders of the Windows volume mixer: apps are lowered through the
  per-channel volume of their audio session, so a volume set for an app by hand stays and applies on
  top of the balance. An app that exits while ChatMix lowers it starts at its own volume next time.
- Virtual device mode follows the Windows volume and mute of the Game device (taskbar slider, volume
  keys) for both sides and of the Chat device for the Chat side. Before, the bridges ignored them.
  Levels above 0 dB are not reproduced, and recordings of the two devices keep the full level.
- Microphone chain: noise suppression (RNNoise, Standard or Strong with a voice gate), a compressor,
  a 10-band equalizer with voice presets and VST 2 / VST 3 plugins with their own windows, played
  into a virtual cable that apps use as their microphone. Microphone and cable levels can be held at
  100%, and the processed microphone opts out of Windows' ducking during calls.
- The equalizer page has a *Mic* channel; it also works without virtual device mode.
- Mouse wheel ChatMix: with a chosen key combination held (`Ctrl + Win`, `Alt + Win` or `Ctrl + Alt + Shift`, off by default)
  the wheel moves the balance and a wheel click centers it; other wheel shortcuts are left alone.
- Output devices (*Settings → Output devices*): an order of preference for the connected devices,
  optional switching to the next one when the output disconnects and back when the preferred one
  reconnects. A new hotkey sends the sound to the next device in that order. Disconnected and disabled
  devices and virtual cables are left out; in virtual device mode the bridges' output is switched.
- Lists whose order matters are reordered by dragging: output devices, the microphone's VST plugins
  and the new *Processing order* of the microphone chain (noise suppression, equalizer, compressor
  and plugins in any order). The up and down arrows are gone.
- *Listen to the microphone* plays the processed microphone on the current output device.
  While listening, a level meter shows the processed microphone against 0 dBFS, with a peak hold.
- Game and Chat have their own volume, on top of ChatMix, both with and without virtual device mode;
  app volumes in Windows stay at 100%. The slider is under each group on the mixer, and its track
  also shows what plays on that side right now.
- Saved headset presets are managed right in the equalizer card on the *Headset* tab: a click
  applies one, and its buttons rename it in place, copy it or delete it (to the Recycle Bin).
- Virtual device mode and the microphone no longer crackle. Before, the output was kept only about
  5 ms ahead while sound arrived in 10 ms packets, so it often ran dry, and the clock difference
  between devices was fixed by cutting audio. Now the output keeps a packet plus a period plus 6 ms
  ahead, and the clock difference is absorbed by playing a hair faster or slower (resampling, at most
  1%). The microphone also tops up its outputs every 5 ms, not only when the microphone delivers.
  When an output still runs dry, the log says so.
- After silence, virtual device mode no longer plays off pitch. While nothing played on a Game or
  Chat device, its bridge kept steering toward an empty queue, so the next sound ran about 1% slow for
  a few seconds and then fast; now the bridge holds while its source is silent and starts again at
  its target latency.
- The meters in the Game and Chat sliders follow the volume and ChatMix without virtual devices too.
  Windows reports an app's level before its volume, so the meters now apply it.
- The log no longer counts the first packet after a start or after silence as a capture gap, nor the
  end of a sound as an output that ran dry. The microphone report adds the slowest processing block
  and the longest pass of its thread, to tell a slow plugin from a late system.
- Chat has a color of its own: paired with the main color automatically, or a preset or any color
  from the picker. It always keeps at least 60° of hue from a colorful Game color and enough color
  next to a gray one, so both sides stay easy to tell apart.
- Softer colors: every theme uses pastel accents. A vivid color from the presets, the picker or the
  Windows accent is toned down instead of used as is.
- Menus and dragged rows no longer cast a shadow; they stand out by their raised tone.
- Numbers are set with − and + or typed: click the value (EQ frequency, level, Q, preamp and boosts,
  ducking, battery warning), type it (`1.5k` for 1500 Hz) and press Enter. The field selects the
  number on opening and has a real caret: arrows, Home and End, Shift selection, Ctrl+A, and a click
  to place it.
- *Settings → General* holds the language, startup and Quit; the update check stays at the bottom of
  *Settings*.
- A pin next to the page title keeps the panel open when it loses focus.
- Device lists say that only connected, enabled devices are shown, where to turn on the others, and
  that the output and microphone pickers leave out virtual cables.
- The tray tooltip shows the headset's battery level and charging.
- With reduced motion, scrolling with the wheel no longer jumps past the end and back; scrolling keeps
  a short glide while everything else changes at once.
- New look on Material 3 Expressive (docs/design-system.md): settings are groups of tiles with small
  gaps instead of boxes, choices of up to five options are connected button groups, longer lists are
  dropdowns, and every animation runs on springs with the Material motion values. Rows are denser
  (48 DIP) and spacing is the same on every page. Each setting has one control: Smart Volume, the
  compressor and the other short choices are button groups, and the arrows next to dropdowns are
  gone.
- Dragging a row no longer drifts when the page scrolls, and a dropped row no longer replays its move.
- The square and ring tray marks blend the Game and Chat colors smoothly where they meet.
- The latency shown for virtual device mode and the microphone is labeled as what OpenChatMix adds
  and includes the output stream's own latency.
- The tray icon can show its own mark at the even balance: a square, a ring or an equals sign.
- Without a SteelSeries headset the panel and the tray show the name of the output device instead of
  "No headset".
- The README and the app no longer present OpenChatMix as SteelSeries-only; a Ukrainian README was added.
- Hover highlights of switch rows no longer touch the hint below them, and the navigation bar labels
  have room below their indicator.
- Images and GIFs for the screensaver: oversized frames, images over 100 megapixels and running out
  of memory are refused instead of ending the app; decoding runs at low priority and very long GIFs
  are cut after a decoding budget.
- Removed: the *Headphones* screensaver animation (a saved choice of it becomes *Off*) and the *Next
  EQ preset* hotkey.

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
