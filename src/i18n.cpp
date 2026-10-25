#include "i18n.h"

#include <array>

namespace {

constexpr size_t kCount = static_cast<size_t>(Text::Count);

constexpr std::array<const wchar_t*, kCount> kEnglish = {
    L"No headset",                                                         // NoHeadset
    L"No headset · hotkeys only",                                          // NoHeadsetStatus
    L"Dial: ChatMix",                                                      // DialChatMix
    L"Dial: volume",                                                       // DialVolume
    L"SteelSeries GG is running — close it",                              // GgRunning
    L"Game",                                                               // Game
    L"Chat",                                                               // Chat
    L"Media",                                                              // Media
    L"Add",                                                                // Add
    L"Apps show up here as soon as they play sound. Others can be added manually.",  // AppsEmpty
    L"not running",                                                        // NotRunning
    L"Add app",                                                            // AddApp
    L"Search apps",                                                        // SearchApps
    L"Add to",                                                             // AddTo
    L"Browse .exe",                                                        // BrowseExe
    L"Choose an app",                                                      // BrowseTitle
    L"Applications",                                                       // BrowseFilter
    L"No running apps",                                                    // NoRunningApps
    L"Nothing found",                                                      // NothingFound
    L"Settings",                                                           // Settings
    L"Hotkeys",                                                            // Hotkeys
    L"More game",                                                          // MoreGame
    L"More chat",                                                          // MoreChat
    L"Balanced",                                                           // Balanced
    L"balanced",                                                           // BalancedSuffix
    L"Not set",                                                            // NotAssigned
    L"Press keys",                                                         // PressKeys
    L"Click a shortcut to change it. A single key works too, such as a keyboard's volume knob; it then no longer reaches other apps.",  // HotkeyHintIdle
    L"Esc to cancel · Backspace to clear",                                 // HotkeyHintRecording
    L"Red shortcuts are taken by another app",                             // HotkeyTaken
    L"Set hotkeys in Settings",                                            // HotkeysInSettings
    L"General",                                                            // General
    L"Language",                                                           // LanguageLabel
    L"Start with Windows",                                                 // StartWithWindows
    L"For hotkeys",                                                        // ShowPopup
    L"Quit OpenChatMix",                                                   // Quit
    L"Works with any headset or speakers: the balance applies to the default output and is controlled with hotkeys "
    L"or the mouse wheel. The ChatMix dial and native settings work on Arctis Nova Pro Wired and Wireless, Nova 7, "
    L"7X, 7 Gen 2, Nova Elite, Arctis 7+ and Nova 5X.",  // SupportFooter
    L"Open",                                                               // TrayOpen
    L"Settings",                                                           // TraySettings
    L"Reconnect headset",                                                  // TrayReconnect
    L"Quit",                                                               // TrayExit
    L"Headset",  // HeadsetPage
    L"Equalizer",  // Equalizer
    L"Custom",  // EqCustom
    L"Reset",  // EqReset
    L"This preset is stored on the headset. Choose Custom to edit the curve.",  // EqPresetActive
    L"Microphone",  // Microphone
    L"Mic volume",  // MicVolume
    L"Sidetone",  // Sidetone
    L"Mute LED brightness",  // MicLed
    L"Sound",  // Sound
    L"Gain",  // Gain
    L"Volume limiter",  // VolumeLimiter
    L"Display",  // Display
    L"Brightness",  // Brightness
    L"Dim screen after",  // ScreenDim
    L"Power",  // Power
    L"Auto power-off",  // AutoOff
    L"Wireless",  // Wireless
    L"2.4 GHz mode",  // WirelessMode
    L"Bluetooth",  // Bluetooth
    L"Turn on with the headset",  // BluetoothPowerOn
    L"Lower game audio during calls",  // BluetoothAutoMute
    L"Off",  // OptOff
    L"Low",  // OptLow
    L"Medium",  // OptMedium
    L"High",  // OptHigh
    L"Never",  // OptNever
    L"Mute",  // OptMute
    L"Speed",  // OptSpeed
    L"Range",  // OptRange
    L"min",  // MinutesShort
    L"ms",  // MillisecondsShort
    L"Battery",  // Battery
    L"charging",  // Charging
    L"Headset is off",  // HeadsetOff
    L"Warn when SteelSeries GG is running",  // WarnGg
    L"SteelSeries GG is running",  // GgBalloonTitle
    L"GG takes over your headset while it runs. Quit GG to use OpenChatMix.",  // GgBalloonText
    L"Audio routing",  // Routing
    L"Virtual device mode",  // RoutingMode
    L"Mix Game and Chat from two virtual audio devices instead of changing app volumes.",  // RoutingModeHint
    L"Game device",  // RoutingGame
    L"Chat device",  // RoutingChat
    L"Output",  // RoutingOutput
    L"Not selected",  // NotSelected
    L"Headset (automatic)",  // AutomaticHeadset
    L"Running",  // RoutingRunning
    L"Choose a device for Game and Chat and an output",  // RoutingNeedsDevices
    L"Each channel needs a device of its own, and no channel may play into another's device",  // RoutingSameDevice
    L"A selected device is not available",  // RoutingDeviceMissing
    L"Hide",  // Dismiss
    L"No playback devices found",  // NoPlaybackDevices
    L"Save",  // EqSave
    L"Import",  // EqImport
    L"Folder",  // EqFolder
    L"My presets",  // MyPresets
    L"Equalizer presets",  // PresetFilter
    L"Save equalizer preset",  // SavePresetTitle
    L"Import equalizer preset",  // ImportPresetTitle
    L"My preset",  // DefaultPresetName
    L"Screensaver",  // IdleScreen
    L"Animation",  // IdleAnimation
    L"Stars",  // IdleStars
    L"Clock",  // IdleClock
    L"Game of Life",  // IdleLife
    L"Screen off",  // IdleBlank
    L"Image or GIF",  // IdleImage
    L"Start after",  // IdleDelay
    L"s",  // SecondsShort
    L"File",  // IdleFile
    L"Choose",  // IdleChoose
    L"Choose an image or GIF",  // IdleImageTitle
    L"Images",  // ImageFilter
    L"Plays on the GameDAC screen while no sound is playing, so the OLED does not burn in. Sound or the GameDAC knob bring the usual screen back.",  // IdleHint
    L"On screen now",  // IdleShowing
    L"Switch devices with the mode",  // RoutingManage
    L"Turns the Game and Chat devices on and makes them the Windows defaults while the mode runs, and off again when it stops or OpenChatMix exits.",  // RoutingManageHint
    L"Choose any two virtual audio cables below; common ones are picked automatically.",  // RoutingNoCables
    L"Windows did not switch the devices",  // RoutingManageFailed
    L"Mixer",  // Mixer
    L"Groups",  // Groups
    L"No apps yet",  // GroupEmpty
    L"Apps here follow the Game side. Apps that are in no group land here too; removed ones are left out of ChatMix until you add them again.",  // GroupGameHint
    L"Apps here follow the Chat side of the balance.",  // GroupChatHint
    L"Apps here ignore ChatMix; the Media slider on the mixer sets their volume.",  // GroupMediaHint
    L"Plays on",  // PlaysOn
    L"The equalizer works in virtual device mode, where Game and Chat pass through OpenChatMix, and on the processed microphone.",  // EqNeedsRouting
    L"Audio settings",  // OpenAudioSettings
    L"Presets",  // Presets
    L"Band",  // Band
    L"Peak",  // FilterPeak
    L"Low shelf",  // FilterLowShelf
    L"High shelf",  // FilterHighShelf
    L"Low pass",  // FilterLowPass
    L"High pass",  // FilterHighPass
    L"Notch",  // FilterNotch
    L"Frequency",  // Frequency
    L"Gain",  // Level
    L"Preamp",  // Preamp
    L"Type",  // FilterType
    L"OpenChatMix",  // PresetsBuiltin
    L"From SteelSeries GG",  // PresetsSonar
    L"Appearance",  // Appearance
    L"Theme, color, background",  // AppearanceSummary
    L"Audio",  // AudioSection
    L"Groups, virtual devices, ducking",  // AudioSummary
    L"Notifications",  // Notifications
    L"Popups, battery, SteelSeries GG",  // NotificationsSummary
    L"App profiles",  // Profiles
    L"Balance and equalizers that apply while an app runs",  // ProfilesSummary
    L"Theme",  // Theme
    L"Auto",  // ThemeAuto
    L"Light",  // ThemeLight
    L"Dark",  // ThemeDark
    L"Contrast",  // Contrast
    L"Standard",  // ContrastStandard
    L"Medium",  // ContrastMedium
    L"High",  // ContrastHigh
    L"Color",  // Color
    L"Windows accent color",  // WindowsColor
    L"Custom color",  // CustomColor
    L"Hue",  // Hue
    L"Saturation",  // Saturation
    L"Type a HEX code, e.g. #3B82F6",  // HexHint
    L"Game color",  // GameColor
    L"Chat color",  // ChatColor
    L"Paired with the Game color",  // ChatColorAuto
    L"Colors stay soft, and Chat always keeps a hue of its own next to Game, so both sides are easy to tell apart.",  // ColorsHint
    L"Background",  // Background
    L"Acrylic",  // Acrylic
    L"Mica",  // Mica
    L"Solid",  // Solid
    L"Reduce animations",  // ReduceMotion
    L"Tray icon",  // TrayIcon
    L"Color",  // TrayColor
    L"Monochrome",  // TrayMono
    L"Media group",  // MediaGroupToggle
    L"Apps in Media ignore ChatMix and have a volume of their own. In virtual device mode Media needs a virtual device of its own.",  // MediaGroupHint
    L"Send apps to their device",  // AutoRoute
    L"Chat apps play on the Chat device, all others on the Game device. Some apps switch only after a restart.",  // AutoRouteHint
    L"Lower game during chat",  // Ducking
    L"While there is sound in chat, the Game side gets quieter.",  // DuckingHint
    L"Lower by",  // DuckingAmount
    L"For the headset wheel",  // DialPopup
    L"Shows the balance when the ChatMix wheel on the headset turns.",  // DialPopupHint
    L"Low battery warning",  // BatteryAlert
    L"Warn at",  // BatteryThreshold
    L"Low battery",  // BatteryLowTitle
    L"charge it soon",  // BatteryLowText
    L"Mute chat",  // MuteChat
    L"Chat muted",  // ChatMuted
    L"Chat unmuted",  // ChatUnmuted
    L"Add game",  // AddGame
    L"Add an app or a game: its balance and equalizers apply while it runs and are undone when it closes.",  // NoProfiles
    L"Balance",  // ProfileBalance
    L"Game equalizer",  // ProfileGameEq
    L"Headset equalizer",  // ProfileHeadsetEq
    L"Unchanged",  // Unchanged
    L"Remove profile",  // RemoveProfile
    L"Equalizer",  // Equalizers
    L"Effects",  // EqEffects
    L"Bass boost",  // BassBoost
    L"Treble boost",  // TrebleBoost
    L"Voice clarity",  // VoiceClarity
    L"Smart volume",  // SmartVolume
    L"Evens out loud and quiet moments.",  // SmartVolumeHint
    L"Off",  // EffectOff
    L"Quiet",  // SmartQuiet
    L"Balanced",  // SmartBalanced
    L"Loud",  // SmartLoud
    L"Adds the Equalizer tab: Game, Chat and Media can be equalized in virtual device mode, the microphone always.",  // EqFeatureHint
    L"Manage presets…",  // ManagePresets
    L"Game presets",  // GamePresets
    L"Applied as the custom curve. An unsaved custom curve is kept in My presets first.",  // GamePresetsHint
    L"Custom curve backup",  // CurveBackup
    L"Updates",  // Updates
    L"Check for updates",  // CheckUpdates
    L"Once a day, from the project's GitHub releases. Nothing else is sent.",  // CheckUpdatesHint
    L"Check now",  // UpdateCheckNow
    L"Checking…",  // UpdateChecking
    L"Up to date",  // UpdateCurrent
    L"New version",  // UpdateAvailable
    L"Install",  // UpdateInstall
    L"Downloading…",  // UpdateDownloading
    L"Could not update",  // UpdateFailed
    L"Could not check",  // UpdateCheckFailed
    L"Open page",  // UpdateOpenPage
    L"A new version is ready to install in Settings.",  // UpdateBalloonText
    L"Version",  // VersionLabel
    L"Icon when balanced",  // TrayBalancedIcon
    L"hotkeys",  // HotkeysOnly
    L"Next output device",  // NextOutput
    L"Output device",  // OutputDevice
    L"No other output device",  // NoOtherOutput
    L"Mouse wheel",  // WheelControl
    L"Hold the keys and turn the wheel: up for more chat, down for more game. Click the wheel for an even balance. Only this exact combination is taken, so zooming with Ctrl or Ctrl + Alt and the wheel keeps working.",  // WheelControlHint
    L"Mic",  // MicShort
    L"Noise suppression, compressor, equalizer, plugins",  // MicSummary
    L"Process the microphone",  // MicProcessing
    L"The processed microphone plays into a virtual cable. Choose its other end, such as CABLE Output, as the microphone in Discord, OBS or the game.",  // MicProcessingHint
    L"Microphone",  // MicInput
    L"Virtual cable",  // MicOutput
    L"Windows default",  // MicDefaultInput
    L"Automatic (CABLE Input)",  // MicAutomaticCable
    L"No virtual cable for the microphone. Install VB-Audio Virtual Cable or choose a cable.",  // MicNoCable
    L"Keep levels at 100%",  // MicKeepLevels
    L"Windows and call apps lower microphone levels on their own. This keeps the microphone and both ends of the cable at 100%.",  // MicKeepLevelsHint
    L"Noise suppression",  // NoiseSuppressionLabel
    L"Standard",  // NoiseStandard
    L"Strong",  // NoiseStrong
    L"Standard removes fans, keyboards and room noise; Strong also silences the microphone while you do not speak.",  // NoiseHint
    L"Compressor",  // CompressorLabel
    L"Light",  // CompressorLight
    L"Medium",  // CompressorMedium
    L"Strong",  // CompressorStrong
    L"Makes quiet speech louder and loud speech quieter.",  // CompressorHint
    L"Equalizer",  // MicEqualizer
    L"Plugins",  // Plugins
    L"VST 2 and VST 3 effects run after the built-in steps, top to bottom; click a plugin to open its window. For AI noise suppression, add DeepFilterNet-OpenChatMix.dll from the OpenChatMix releases on GitHub, or, with an NVIDIA RTX card, NVAudioEffects.dll from NVIDIA's Audio Effects redistributable.",  // PluginsHint
    L"Add plugin",  // AddPlugin
    L"Choose a plugin",  // PluginTitle
    L"Plugins",  // PluginFilter
    L"could not be loaded",  // PluginNotLoaded
    L"Output devices",  // OutputDevices
    L"Priority, switching on disconnect",  // OutputDevicesSummary
    L"Switch when a device disconnects",  // SwitchOnLoss
    L"The sound goes to the highest device below that is still connected.",  // SwitchOnLossHint
    L"Return to a device that reconnects",  // ReturnToPreferred
    L"When the top device of the list connects again, the sound goes back to it.",  // ReturnToPreferredHint
    L"Priority",  // Priority
    L"Disconnected and disabled devices are not shown here. To manage one, first turn it on in the Windows sound settings (System → Sound → All sound devices). Virtual cables are left out. Click a device to use it now; the Next output device hotkey follows this order.",  // PriorityHint
    L"adds",  // AddsLatency
    L"Language, start with Windows, backup, log, quit",  // GeneralSummary
    L"Processing order",  // ProcessingOrder
    L"Drag the steps to change the order the microphone passes through them.",  // ProcessingOrderHint
    L"Listen to the microphone",  // MicMonitor
    L"Plays the processed microphone on your headphones, to try settings and plugins. Others still hear it only through the cable.",  // MicMonitorHint
    L"Only connected devices are shown; virtual cables are hidden. To use a disabled device, turn it on in the Windows sound settings.",  // HiddenDevicesHint
    L"Only connected devices are shown. To use a disabled device, turn it on in the Windows sound settings.",  // DisabledDevicesHint
    L"Colors",  // TrayIconColors
    L"Changes happen at once instead of moving; scrolling keeps a short glide.",  // ReduceMotionHint
    L"Level",  // MicLevel
    L"Speech peaking around −12 dB sounds right; keep the peaks below 0 dB or it distorts.",  // MicLevelHint
    L"copy",  // CopySuffix
    L"Never switch to",  // NeverSwitchTo
    L"Switching after a disconnect and the Next output device hotkey skip these devices. You can still use one by clicking it.",  // NeverSwitchToHint
    L"Every connected device is in Never switch to.",  // AllOutputsExcluded
    L"At the even balance the icon shows the headset's charge, with a bolt while it charges. Headsets that report no battery keep the bars.",  // TrayBatteryHint
    L"Welcome to OpenChatMix",  // TourWelcomeTitle
    L"Balance game and chat with one control, on any headset or speakers.",  // TourWelcomeText
    L"Ctrl + Alt + ← / → move the balance, Ctrl + Alt + ↓ evens it out. Change them in Settings → Hotkeys.",  // TourHotkeys
    L"On a SteelSeries headset with a ChatMix dial, the dial works too.",  // TourDial
    L"Click the icon in the tray to open this panel; the icon shows the balance.",  // TourTray
    L"Apps in groups",  // TourGroupsTitle
    L"ChatMix turns down one side: Game or Chat.",  // TourGroupsText
    L"Apps follow Game unless you put them in another group.",  // TourGroupsGame
    L"Discord and the Xbox party chat start in Chat.",  // TourGroupsChat
    L"Open a group on the Mixer page to add an app; the trash icon takes it out again.",  // TourGroupsAdd
    L"Extras when you need them",  // TourExtrasTitle
    L"OpenChatMix stays light: extras are off until you turn them on, and only then use memory.",  // TourExtrasText
    L"The Media group and the equalizer are off. Turn them on in Settings → Audio.",  // TourExtrasEq
    L"Virtual device mode gives Game and Chat devices of their own, for streams and recordings.",  // TourExtrasRouting
    L"The microphone chain adds noise suppression, an equalizer and VST plugins.",  // TourExtrasMic
    L"Make it yours",  // TourYoursTitle
    L"A few places worth a look.",  // TourYoursText
    L"Settings → Appearance: theme, colors and what the tray icon shows at the even balance, such as the battery.",  // TourYoursLook
    L"A SteelSeries headset gets a Headset tab with its own settings, from the equalizer to the microphone.",  // TourYoursHeadset
    L"OpenChatMix starts with Windows. Change that, or open this tour again, in Settings → General.",  // TourYoursStart
    L"Skip",  // TourSkip
    L"Back",  // TourBack
    L"Next",  // TourNext
    L"Get started",  // TourDone
    L"Show the tour",  // TourShow
    L"A short look at what OpenChatMix can do",  // TourShowHint
    L"Back",  // TipBack
    L"Keep the panel open",  // TipPin
    L"Let the panel hide again",  // TipUnpin
    L"Previous band",  // TipPreviousBand
    L"Next band",  // TipNextBand
    L"Remove the plugin",  // TipRemovePlugin
    L"Remove from the group; the app follows Game again",  // TipRemoveFromGroup
    L"Never switch to this device on its own",  // TipNeverSwitch
    L"Switch to this device again",  // TipSwitchAgain
    L"Rename",  // TipRename
    L"Make a copy",  // TipCopy
    L"Delete (to the Recycle Bin)",  // TipDelete
    L"Bars",  // TipMarkBars
    L"Square",  // TipMarkSquare
    L"Ring",  // TipMarkRing
    L"Equals sign",  // TipMarkEquals
    L"Headset battery",  // TipMarkBattery
    L"Same as the output",  // SameAsOutput
    L"None · Media plays with Game",  // RoutingMediaNone
    L"Virtual device",  // RoutingDevice
    L"Plays on",  // RoutingPlaysOn
    L"Media device",  // RoutingMediaDevice
    L"Apps that are not in Chat or Media play into this device. While the mode runs it is the default output, so the taskbar slider and the volume keys control it and the channels that play with it.",  // RoutingGameHint
    L"Apps in the Chat group play into this device. While the mode runs it is the default communications device, so calls land here too. ChatMix balances it against Game.",  // RoutingChatHint
    L"Media gets a virtual cable of its own, for example a third VB-Audio or Virtual Audio Cable; choosing one is up to you. ChatMix never changes it, its own Windows volume does. Don't pick the cable your microphone plays into. Without one, Media plays with Game.",  // RoutingMediaHint
    L"Line out",  // LineOut
    L"Line out plays",  // LineOutMode
    L"Speakers",  // OptSpeakers
    L"Streaming",  // OptStreaming
    L"Stream: main sound",  // StreamMain
    L"Stream: aux input",  // StreamAux
    L"Stream: microphone",  // StreamMic
    L"Screensaver on/off",  // StartScreensaver
    L"No screensaver",  // ScreensaverOffTitle
    L"Choose one on the Headset tab",  // ScreensaverOffText
    L"Mute game",  // MuteGame
    L"Game muted",  // GameMuted
    L"Game unmuted",  // GameUnmuted
    L"Only virtual audio cables are listed: a channel plays from a cable, never from a headset or speakers. To use a disabled cable, turn it on in the Windows sound settings.",  // ChannelDevicesHint
    L"Chat equalizer",  // ProfileChatEq
    L"Media equalizer",  // ProfileMediaEq
    L"Try it on the headset",  // IdleTest
    L"Shows it for a few seconds",  // IdleTestHint
    L"Test",  // IdleTestButton
    L"Mute media",  // MuteMedia
    L"Media muted",  // MediaMuted
    L"Media unmuted",  // MediaUnmuted
    L"Gives way while these SteelSeries GG apps show something on the screen: ",  // IdleGgApps
    L"Bluetooth icon",  // IdleBluetoothIcon
    L"Shows on the screensaver while the base station's Bluetooth is on: steady with a device connected, blinking slowly while it waits for one and fast while it pairs.",  // IdleBluetoothIconHint
    L"Top left",  // CornerTopLeft
    L"Top right",  // CornerTopRight
    L"Bottom left",  // CornerBottomLeft
    L"Bottom right",  // CornerBottomRight
    L"3rd party add-ons",  // AddOns
    L"GameSense programs on the GameDAC screen without GG",  // AddOnsSummary
    L"Programs made for SteelSeries GameSense can show messages and more on the GameDAC screen through OpenChatMix while SteelSeries GG is closed. OpenChatMix includes no such programs: you add one you already have by choosing its file, and OpenChatMix starts it and shows what it sends.",  // AddOnsHint
    L"SteelSeries GG is running, so it shows these programs itself. The add-ons start once GG is closed through its tray icon.",  // AddOnsGgHint
    L"Add-ons need a headset with a screen, such as the Arctis Nova Pro.",  // AddOnsNoDisplayHint
    L"File not found",  // AddOnMissing
    L"Running",  // AddOnRunning
    L"Waits until GG is closed",  // AddOnsWaitGg
    L"Needs a headset with a screen",  // AddOnsNoDisplay
    L"Not running",  // AddOnNotRunning
    L"Start with OpenChatMix",  // AddOnEnabled
    L"Runs while SteelSeries GG is closed and stops when GG starts.",  // AddOnEnabledHint
    L"Shows the screens the program describes for SteelSeries displays.",  // AddOnGenericHint
    L"Remove add-on",  // AddOnRemove
    L"Choose a GameSense program",  // AddOnBrowseTitle
    L"Mic muted",  // CardMuted
    L"You joined",  // CardSelfJoined
    L"You left",  // CardSelfLeft
    L"Joins the channel",  // CardJoins
    L"Leaves the channel",  // CardLeaves
    L"Events",  // AddOnEvents
    L"What each Discord event puts on the GameDAC screen, as in SteelSeries GG.",  // AddOnEventsHint
    L"Message",  // EventMessage
    L"Someone talking",  // EventVoiceActivity
    L"You talking",  // EventYourVoice
    L"Your mic muted",  // EventMuted
    L"Someone joins your channel",  // EventUserJoined
    L"Someone leaves your channel",  // EventUserLeft
    L"You join a channel",  // EventYouJoined
    L"You leave the channel",  // EventYouLeft
    L"Show on the screen",  // AddOnShowEvent
    L"Repeat",  // AddOnRepeat
    L"Once",  // RepeatOnce
    L"Several times",  // RepeatTimes
    L"While it lasts",  // RepeatWhileOn
    L"The displays below play in turn, then the usual screen comes back.",  // RepeatHint
    L"The displays below play in turn for as long as the event lasts.",  // RepeatWhileOnHint
    L"Times",  // AddOnTimes
    L"Displays",  // AddOnDisplays
    L"Shown one after another, each for its time. Drag one to move it.",  // AddOnDisplaysHint
    L"Display",  // AddOnDisplay
    L"Add a display",  // AddOnAddDisplay
    L"Show on the headset",  // AddOnPreview
    L"Shows while the add-ons run, with sample names and text.",  // AddOnPreviewHint
    L"Reset to default",  // AddOnReset
    L"Shows",  // AddOnDisplayKind
    L"Text",  // AddOnKindText
    L"Image",  // AddOnKindImage
    L"Time",  // AddOnLength
    L"Icon",  // AddOnIcon
    L"Words",  // AddOnWords
    L"The heading is bold beside the icon, the text below it. Add a field and the event fills it in.",  // AddOnFieldsHint
    L"Heading",  // AddOnHeading
    L"Text",  // AddOnText
    L"Title",  // FieldTitle
    L"Message",  // FieldMessage
    L"User",  // FieldUser
    L"Channel",  // FieldChannel
    L"Image file",  // AddOnImageFile
    L"A picture or GIF, in black and white across the whole screen.",  // AddOnImageHint
    L"Remove display",  // AddOnRemoveDisplay
    L"Alex (#general)",  // PreviewTitle
    L"This is how a message looks on the GameDAC.",  // PreviewMessage
    L"Alex",  // PreviewUser
    L"General",  // PreviewChannel
    L"Visualizer",  // Visualizer
    L"Show while something plays",  // VisualizerShow
    L"Bars that follow the sound fill the whole GameDAC screen, like an equalizer. Turning the knob shows the usual screen for a moment; add-on screens such as Discord messages show on top.",  // VisualizerHint
    L"Shows for",  // VisualizerFor
    L"Music and video",  // VisualizerMedia
    L"Any sound",  // VisualizerSound
    L"While an app tells Windows it plays, as the media controls in Windows show it: players, YouTube in the browser and the like.",  // VisualizerMediaHint
    L"Whenever the headset plays sound, games and calls too.",  // VisualizerSoundHint
    L"2.4 GHz",  // Link24Ghz
    L"3.5 mm",  // LinkJack
    L"Battery color",  // BatteryColor
    L"Theme",  // BatteryByTheme
    L"Charge",  // BatteryByCharge
    L"The battery takes the theme's accent color.",  // BatteryThemeHint
    L"From green when full to red when empty.",  // BatteryChargeHint
    L"Backup",  // SettingsBackup
    L"Export",  // SettingsExport
    L"Import",  // SettingsImport
    L"Settings, presets and plugins in one ZIP file. Import restarts OpenChatMix and keeps the current ones in Backups.",  // SettingsTransferHint
    L"Export settings",  // SettingsExportTitle
    L"Import settings",  // SettingsImportTitle
    L"OpenChatMix settings",  // SettingsFilter
    L"Saved:",  // SettingsExported
    L"The file could not be saved.",  // SettingsExportFailed
    L"This file holds no OpenChatMix settings.",  // SettingsImportFailed
    L"Next microphone",  // NextMicrophone
    L"No other microphone",  // NoOtherMicrophone
    L"Microphone processing is off",  // MicProcessingOff
    L"Experimental on this headset: if the sound does not change, please report it on GitHub.",  // EqExperimental
    L"Max noise reduction",  // DeepFilterReduction
    L"Lower values leave a little of the room in, which sounds more natural; Unlimited removes all the noise it can. DeepFilterNet adds about 20 ms.",  // DeepFilterReductionHint
    L"Unlimited",  // DeepFilterUnlimited
    L"Post-filter",  // DeepFilterPostFilter
    L"Removes a little more of the noise left between words; the voice may sound slightly thinner.",  // DeepFilterPostFilterHint
    L"Windows default",  // WindowsDefaultOutput
    L"Each group can play on a device of its own without virtual cables, for example Game and Chat on the headset and Media on the speakers. Windows sends the group's apps there, and ChatMix still balances Game and Chat.",  // GroupOutputsHint
    L"VST 2 and VST 3 effects run on this channel before its volume, top to bottom; click a plugin to open its window.",  // ChannelPluginsHint
    L"To remove noise from the voices of others, add DeepFilterNet-OpenChatMix.dll from the OpenChatMix releases on GitHub.",  // ChatPluginsHint
    L"Latency of the plugins",  // PluginsLatency
    L"Lower values leave a little of the room in, which sounds more natural; Unlimited removes all the noise it can. On a channel DeepFilterNet filters the mix of both sides and adds about 30 ms.",  // DeepFilterChannelHint
    L"Sidetone, boom mic",  // SidetoneBoomMic
    L"Sidetone, built-in mic",  // SidetoneOnEarMic
    L"Noise control",  // NoiseControl
    L"Noise cancelling level",  // NoiseCancellingLevel
    L"Transparency level",  // TransparencyLevel
    L"Home screen",  // HomeScreen
    L"Transparency",  // OptTransparency
    L"ANC",  // OptNoiseCancelling
    L"Detailed",  // OptDetailed
    L"Simple",  // OptSimple
    L"The settings of this headset are experimental: they follow a description of its protocol and are not yet confirmed on it. If one does not apply, please report it on GitHub.",  // HeadsetExperimental
    L"Mode",  // NoiseMode
    L"Diagnostics",  // LogSection
    L"Write a log",  // LogEnabled
    L"OpenChatMix.log records what the headset reports and any errors, for bug reports on GitHub. It may contain device and app names; turn it off when you are done.",  // LogHint
    L"Open the log folder",  // LogFolder
    L"For the knob in volume mode",  // VolumePopup
    L"Shows the headset volume when the knob on the GameDAC or base station turns.",  // VolumePopupHint
    L"Headset volume",  // HeadsetVolume
    L"spare",  // SpareBattery
    L"Effect",  // NvidiaAfxEffect
    L"Noise",  // NvidiaAfxNoise
    L"Room echo",  // NvidiaAfxEcho
    L"Both",  // NvidiaAfxBoth
    L"Strength",  // NvidiaAfxIntensity
    L"Lower values leave more of the original sound.",  // NvidiaAfxIntensityHint
    L"Loading the model on the graphics card…",  // NvidiaAfxLoading
    L"Runs on the NVIDIA graphics card.",  // NvidiaAfxReady
    L"The model did not load, so the sound passes unchanged. Install NVIDIA's Audio Effects redistributable for this graphics card; with the log on, it names the reason.",  // NvidiaAfxFailed
    L"Show popups",  // ShowPopups
    L"The balance, the volume and short notices in a corner of the screen.",  // ShowPopupsHint
    L"Corner of the screen",  // PopupCorner
    L"For the knob in ChatMix mode",  // KnobPopup
    L"Shows the balance when the knob on the GameDAC or base station turns.",  // KnobPopupHint
    L"Shows even while popups are off.",  // BatteryAlertHint
    L"For this game",  // PresetsForGame
    L"Type to search…",  // MenuSearchHint
    L"Nothing found",  // MenuNoMatches
    L"Saving in",  // EqSavingIn
    L"Done",  // EqSaved
    L"Noise cancelling on",  // NoiseCancellingOn
    L"Transparency on",  // TransparencyOn
    L"Noise control off",  // NoiseControlOff
    L"For noise control",  // NoisePopup
    L"Shows the mode when it changes with the headset's button.",  // NoisePopupHint
    L"Noise control on the base station screen",  // NoiseScreen
    L"Shows the new mode there for a moment when it changes with the headset's button, while SteelSeries GG is closed.",  // NoiseScreenHint
    L"For sound over Bluetooth, such as from a phone.",  // BluetoothEqHint
    L"Your voice on the headset's microphone, set in the headset itself: no virtual cable and no added latency.",  // MicEqHint
    L"Leave out of ChatMix: the app keeps its own volume",  // TipLeaveOutOfChatMix
    L"out of ChatMix",  // OutOfChatMix
};

constexpr std::array<const wchar_t*, kCount> kUkrainian = {
    L"Без гарнітури",
    L"Без гарнітури · гарячі клавіші",
    L"Коліщатко: ChatMix",
    L"Коліщатко: гучність",
    L"Запущено SteelSeries GG — закрийте його",
    L"Гра",
    L"Чат",
    L"Медіа",
    L"Додати",
    L"Програми з'являться тут, щойно відтворять звук. Інші можна додати вручну.",
    L"не запущена",
    L"Додати програму",
    L"Пошук програми",
    L"Додати до",
    L"Обрати .exe",
    L"Оберіть програму",
    L"Програми",
    L"Немає запущених програм",
    L"Нічого не знайдено",
    L"Налаштування",
    L"Гарячі клавіші",
    L"Більше гри",
    L"Більше чату",
    L"Порівну",
    L"порівну",
    L"Не призначено",
    L"Натисніть клавіші",
    L"Натисніть на комбінацію, щоб змінити її. Підійде й одна клавіша, наприклад регулятор гучності на клавіатурі; тоді інші програми її не отримуватимуть.",
    L"Esc — скасувати · Backspace — прибрати",
    L"Червоні комбінації вже зайняті іншою програмою",
    L"Гарячі клавіші — у налаштуваннях",
    L"Загальне",
    L"Мова",
    L"Запускати разом з Windows",
    L"Для гарячих клавіш",
    L"Вийти з OpenChatMix",
    L"Працює з будь-якими навушниками чи колонками: баланс застосовується до типового пристрою виводу й керується гарячими клавішами або колесом миші. Коліщатко ChatMix і налаштування гарнітури доступні для Arctis Nova Pro Wired і Wireless, Nova 7, 7X, 7 Gen 2, Nova Elite, Arctis 7+ і Nova 5X.",
    L"Відкрити",
    L"Налаштування",
    L"Перепідключити гарнітуру",
    L"Вихід",
    L"Гарнітура",
    L"Еквалайзер",
    L"Власний",
    L"Скинути",
    L"Цей пресет зберігається в гарнітурі. Щоб змінити криву, оберіть «Власний».",
    L"Мікрофон",
    L"Гучність мікрофона",
    L"Самопрослуховування",
    L"Яскравість індикатора мікрофона",
    L"Звук",
    L"Підсилення",
    L"Обмежувач гучності",
    L"Дисплей",
    L"Яскравість",
    L"Затемнення екрана через",
    L"Живлення",
    L"Автовимкнення",
    L"Бездротовий зв'язок",
    L"Режим 2,4 ГГц",
    L"Bluetooth",
    L"Вмикати разом з гарнітурою",
    L"Приглушувати гру під час дзвінків",
    L"Вимк.",
    L"Низький",
    L"Середній",
    L"Високий",
    L"Ніколи",
    L"Без звуку",
    L"Швидкість",
    L"Дальність",
    L"хв",
    L"мс",
    L"Батарея",
    L"заряджається",
    L"Гарнітура вимкнена",
    L"Попереджати про SteelSeries GG",
    L"Запущено SteelSeries GG",
    L"Поки GG працює, він керує гарнітурою. Закрийте GG, щоб користуватися OpenChatMix.",
    L"Маршрутизація звуку",
    L"Режим віртуальних пристроїв",
    L"Змішувати гру й чат із двох віртуальних аудіопристроїв замість зміни гучності програм.",
    L"Пристрій для гри",
    L"Пристрій для чату",
    L"Вихід",
    L"Не вибрано",
    L"Гарнітура (автоматично)",
    L"Працює",
    L"Оберіть пристрої для гри й чату та вихід",
    L"Кожному каналу потрібен окремий пристрій, і жоден канал не може виводити звук на пристрій іншого",
    L"Вибраний пристрій недоступний",
    L"Сховати",
    L"Пристроїв відтворення не знайдено",
    L"Зберегти",
    L"Імпорт",
    L"Папка",
    L"Мої пресети",
    L"Пресети еквалайзера",
    L"Зберегти пресет еквалайзера",
    L"Імпортувати пресет еквалайзера",
    L"Мій пресет",
    L"Заставка",
    L"Анімація",
    L"Зорі",
    L"Годинник",
    L"Гра «Життя»",
    L"Чорний екран",
    L"Зображення або GIF",
    L"Запускати через",
    L"с",
    L"Файл",
    L"Обрати",
    L"Оберіть зображення або GIF",
    L"Зображення",
    L"Відтворюється на дисплеї GameDAC, поки немає звуку, щоб OLED не вигорав. Звук або коліщатко GameDAC повертають звичайний екран.",
    L"Зараз на екрані",
    L"Вмикати пристрої разом з режимом",
    L"Вмикає пристрої для гри й чату та робить їх типовими у Windows, поки режим працює, і вимикає, коли режим зупинено або OpenChatMix закрито.",
    L"Оберіть нижче будь-які два віртуальні аудіокабелі; поширені обираються автоматично.",
    L"Windows не перемкнув пристрої",
    L"Мікшер",
    L"Групи",
    L"Поки немає програм",
    L"Гучністю цих програм керує сторона гри. Сюди ж потрапляють програми без групи; прибрані з неї залишаються поза ChatMix, доки ви не додасте їх знову.",
    L"Гучністю цих програм керує сторона чату.",
    L"Ці програми не залежать від ChatMix; їхню гучність задає повзунок «Медіа» в мікшері.",
    L"Відтворення на",
    L"Еквалайзер працює в режимі віртуальних пристроїв, де гра й чат проходять через OpenChatMix, а також на обробленому мікрофоні.",
    L"Налаштування звуку",
    L"Пресети",
    L"Смуга",
    L"Пік",
    L"Полиця НЧ",
    L"Полиця ВЧ",
    L"Зріз ВЧ",
    L"Зріз НЧ",
    L"Режекція",
    L"Частота",
    L"Рівень",
    L"Передпідсилення",
    L"Тип",
    L"OpenChatMix",
    L"Із SteelSeries GG",
    L"Вигляд",
    L"Тема, колір, фон",
    L"Звук",
    L"Групи, віртуальні пристрої, приглушення",
    L"Сповіщення",
    L"Спливаючі вікна, батарея, SteelSeries GG",
    L"Профілі програм",
    L"Баланс і еквалайзери, що діють, поки програма запущена",
    L"Тема",
    L"Авто",
    L"Світла",
    L"Темна",
    L"Контраст",
    L"Звичайний",
    L"Середній",
    L"Високий",
    L"Колір",
    L"Колір акценту Windows",
    L"Власний колір",
    L"Відтінок",
    L"Насиченість",
    L"Введіть HEX-код, наприклад #3B82F6",
    L"Колір гри",
    L"Колір чату",
    L"Підібраний до кольору гри",
    L"Кольори лишаються м'якими, а чат завжди має власний відтінок поруч із грою, тож обидві сторони легко розрізнити.",
    L"Фон",
    L"Акрил",
    L"Mica",
    L"Суцільний",
    L"Менше анімацій",
    L"Значок у треї",
    L"Кольоровий",
    L"Монохромний",
    L"Група «Медіа»",
    L"Програми з «Медіа» не залежать від ChatMix і мають власну гучність. У режимі віртуальних пристроїв «Медіа» потребує окремого віртуального пристрою.",
    L"Спрямовувати програми на їхні пристрої",
    L"Програми чату відтворюються на пристрої чату, решта — на пристрої гри. Деякі програми перемкнуться лише після перезапуску.",
    L"Приглушувати гру під час чату",
    L"Поки в чаті є звук, сторона гри стає тихішою.",
    L"Приглушувати на",
    L"Для коліщатка на гарнітурі",
    L"Показує баланс під час обертання коліщатка ChatMix на гарнітурі.",
    L"Попередження про низький заряд",
    L"Попереджати при",
    L"Низький заряд",
    L"скоро зарядіть",
    L"Вимкнути звук чату",
    L"Звук чату вимкнено",
    L"Звук чату увімкнено",
    L"Додати гру",
    L"Додайте програму чи гру: її баланс і еквалайзери діятимуть, поки вона запущена, і скасуються, коли вона закриється.",
    L"Баланс",
    L"Еквалайзер гри",
    L"Еквалайзер гарнітури",
    L"Без змін",
    L"Видалити профіль",
    L"Еквалайзер",
    L"Ефекти",
    L"Підсилення басів",
    L"Підсилення верхів",
    L"Чіткість голосу",
    L"Розумна гучність",
    L"Вирівнює гучні й тихі моменти.",
    L"Вимк.",
    L"Тихо",
    L"Помірно",
    L"Гучно",
    L"Додає вкладку «Еквалайзер»: у режимі віртуальних пристроїв — для гри, чату й медіа, для мікрофона — завжди.",
    L"Керувати пресетами…",
    L"Ігрові пресети",
    L"Пресет замінює власну криву; незбережену криву спершу буде збережено в «Мої пресети».",
    L"Резервна копія власної кривої",
    L"Оновлення",
    L"Перевіряти оновлення",
    L"Раз на день, із релізів проєкту на GitHub. Більше нічого не надсилається.",
    L"Перевірити",
    L"Перевірка…",
    L"Найновіша версія",
    L"Нова версія",
    L"Встановити",
    L"Завантаження…",
    L"Не вдалося оновити",
    L"Не вдалося перевірити",
    L"Відкрити сторінку",
    L"Нову версію можна встановити в налаштуваннях.",
    L"Версія",
    L"Значок при рівному балансі",
    L"гарячі клавіші",
    L"Наступний пристрій виводу",
    L"Пристрій виводу",
    L"Іншого пристрою виводу немає",
    L"Колесо миші",
    L"Затисніть клавіші й крутіть колесо: вгору — більше чату, вниз — більше гри. Натискання на колесо вирівнює баланс. Перехоплюється лише ця комбінація, тож масштабування через Ctrl чи Ctrl + Alt з колесом працює як раніше.",
    L"Мік",
    L"Шумодав, компресор, еквалайзер, плагіни",
    L"Обробляти мікрофон",
    L"Оброблений сигнал мікрофона подається у віртуальний кабель. Оберіть вихід цього кабелю, наприклад CABLE Output, як мікрофон у Discord, OBS чи грі.",
    L"Мікрофон",
    L"Віртуальний кабель",
    L"Типовий у Windows",
    L"Автоматично (CABLE Input)",
    L"Немає віртуального кабелю для мікрофона. Встановіть VB-Audio Virtual Cable або оберіть кабель.",
    L"Тримати рівні на 100%",
    L"Windows і програми для дзвінків самі знижують рівень мікрофона. Цей параметр утримує рівні мікрофона й обох кінців кабелю на 100%.",
    L"Шумодав",
    L"Звичайний",
    L"Сильний",
    L"«Звичайний» прибирає шум вентиляторів, клавіатури й кімнати; «Сильний» ще й приглушує мікрофон, поки ви мовчите.",
    L"Компресор",
    L"Легкий",
    L"Середній",
    L"Сильний",
    L"Робить тиху мову гучнішою, а гучну — тихішою.",
    L"Еквалайзер",
    L"Плагіни",
    L"Ефекти VST 2 і VST 3 працюють після вбудованих кроків, згори донизу; натисніть на плагін, щоб відкрити його вікно. Для шумодаву на основі ШІ додайте DeepFilterNet-OpenChatMix.dll з релізів OpenChatMix на GitHub або, з відеокартою NVIDIA RTX, NVAudioEffects.dll з пакета NVIDIA Audio Effects.",
    L"Додати плагін",
    L"Оберіть плагін",
    L"Плагіни",
    L"не вдалося завантажити",
    L"Пристрої виводу",
    L"Пріоритет, перемикання при відключенні",
    L"Перемикати, коли пристрій відключається",
    L"Звук перейде на перший підключений пристрій зі списку нижче.",
    L"Повертатися на пристрій, коли він підключається",
    L"Коли перший пристрій зі списку знову підключається, звук повертається на нього.",
    L"Пріоритет",
    L"Відключені й вимкнені пристрої тут не показуються. Щоб керувати таким пристроєм, спершу увімкніть його в параметрах звуку Windows (Система → Звук → Усі звукові пристрої). Віртуальні кабелі приховано. Натисніть на пристрій, щоб перемкнутися на нього; гаряча клавіша «Наступний пристрій виводу» перемикає в цьому порядку.",
    L"додає",
    L"Мова, запуск з Windows, резервна копія, лог, вихід",
    L"Порядок обробки",
    L"Перетягуйте кроки, щоб змінити порядок, у якому їх проходить мікрофон.",
    L"Слухати мікрофон",
    L"Відтворює оброблений мікрофон у навушниках, щоб перевіряти налаштування й плагіни. Інші й далі чують його лише через кабель.",
    L"Показано лише підключені пристрої; віртуальні кабелі приховано. Щоб використати вимкнений пристрій, увімкніть його в параметрах звуку Windows.",
    L"Показано лише підключені пристрої. Щоб використати вимкнений пристрій, увімкніть його в параметрах звуку Windows.",
    L"Кольори",
    L"Зміни відбуваються миттєво, без анімації; прокручування лишається плавним.",
    L"Рівень",
    L"Голос із піками близько −12 дБ звучить добре; тримайте піки нижче 0 дБ, інакше звук спотворюється.",
    L"копія",
    L"Не перемикатися на",
    L"Перемикання після відключення й гаряча клавіша «Наступний пристрій виводу» оминають ці пристрої. Перемкнутися на такий пристрій можна, натиснувши на нього.",
    L"Усі підключені пристрої в списку «Не перемикатися на».",
    L"При рівному балансі значок показує заряд гарнітури, а під час заряджання — ще й блискавку. Для гарнітур, що не передають рівень заряду, лишаються смужки.",
    L"Вітаємо в OpenChatMix",
    L"Баланс гри й чату одним рухом — на будь-яких навушниках чи колонках.",
    L"Ctrl + Alt + ← / → змінюють баланс, Ctrl + Alt + ↓ вирівнює його. Змінити їх можна в Налаштування → Гарячі клавіші.",
    L"На гарнітурах SteelSeries баланс регулює й коліщатко ChatMix.",
    L"Натисніть значок у треї, щоб відкрити цю панель; значок показує баланс.",
    L"Програми в групах",
    L"ChatMix приглушує одну зі сторін: гру або чат.",
    L"Програми належать до гри, доки ви не перенесете їх в іншу групу.",
    L"Discord і чат вечірок Xbox одразу потрапляють у «Чат».",
    L"Відкрийте групу на вкладці «Мікшер», щоб додати програму; значок кошика прибирає її з групи.",
    L"Більше — коли потрібно",
    L"OpenChatMix лишається легким: додаткові функції вимкнені, доки ви їх не ввімкнете, і лише тоді займають пам'ять.",
    L"Група «Медіа» й еквалайзер вимкнені. Увімкніть їх у Налаштування → Звук.",
    L"Режим віртуальних пристроїв дає грі й чату окремі пристрої — для стрімів і записів.",
    L"Обробка мікрофона додає шумодав, еквалайзер і VST-плагіни.",
    L"Під себе",
    L"Кілька місць, які варто переглянути.",
    L"Налаштування → Вигляд: тема, кольори й те, що значок у треї показує при рівному балансі, наприклад заряд батареї.",
    L"Для гарнітур SteelSeries з'являється окрема вкладка з їхніми налаштуваннями — від еквалайзера до мікрофона.",
    L"OpenChatMix запускається разом з Windows. Змінити це чи відкрити знайомство знову можна в Налаштування → Загальне.",
    L"Пропустити",
    L"Назад",
    L"Далі",
    L"Почати",
    L"Показати знайомство",
    L"Коротко про те, що вміє OpenChatMix",
    L"Назад",
    L"Не ховати панель",
    L"Знову ховати панель",
    L"Попередня смуга",
    L"Наступна смуга",
    L"Прибрати плагін",
    L"Прибрати з групи; програма знову належатиме до гри",
    L"Не перемикатися на цей пристрій автоматично",
    L"Знову перемикатися на цей пристрій",
    L"Перейменувати",
    L"Створити копію",
    L"Видалити (у Кошик)",
    L"Смужки",
    L"Квадрат",
    L"Кільце",
    L"Знак «=»",
    L"Батарея гарнітури",
    L"Як основний вихід",
    L"Немає · «Медіа» відтворюється разом із грою",
    L"Віртуальний пристрій",
    L"Відтворення на",
    L"Пристрій для медіа",
    L"Програми, що не входять у «Чат» чи «Медіа», виводять звук на цей пристрій. Поки режим працює, він є типовим пристроєм виводу, тож повзунок на панелі завдань і клавіші гучності керують ним і каналами, що відтворюються разом із ним.",
    L"Програми з групи «Чат» виводять звук на цей пристрій. Поки режим працює, він є типовим пристроєм зв'язку, тож дзвінки теж ідуть сюди. ChatMix урівноважує його з грою.",
    L"«Медіа» отримує власний віртуальний кабель, наприклад третій VB-Audio чи Virtual Audio Cable; який саме — обираєте ви. ChatMix його не змінює — лише власна гучність у Windows. Не обирайте кабель, на який виводиться мікрофон. Без нього «Медіа» відтворюється разом із грою.",
    L"Лінійний вихід",
    L"Режим лінійного виходу",
    L"Колонки",
    L"Стрімінг",
    L"Стрім: основний звук",
    L"Стрім: aux-вхід",
    L"Стрім: мікрофон",
    L"Заставка: увімк./вимк.",
    L"Заставку не вибрано",
    L"Оберіть її на вкладці «Гарнітура»",
    L"Вимкнути звук гри",
    L"Звук гри вимкнено",
    L"Звук гри увімкнено",
    L"Тут лише віртуальні аудіокабелі: канал отримує звук із кабелю, а не з навушників чи колонок. Щоб використати вимкнений кабель, увімкніть його в параметрах звуку Windows.",
    L"Еквалайзер чату",
    L"Еквалайзер медіа",
    L"Спробувати на гарнітурі",
    L"Показує її кілька секунд",
    L"Тест",
    L"Вимкнути звук медіа",
    L"Звук медіа вимкнено",
    L"Звук медіа увімкнено",
    L"Поступається, поки ці програми SteelSeries GG щось показують на екрані: ",
    L"Значок Bluetooth",
    L"Показується на заставці, поки Bluetooth базової станції увімкнено: горить постійно, коли пристрій підключено, повільно блимає, поки підключення немає, і швидко — під час сполучення.",
    L"Угорі ліворуч",
    L"Угорі праворуч",
    L"Унизу ліворуч",
    L"Унизу праворуч",
    L"Сторонні доповнення",
    L"Програми GameSense на екрані GameDAC без GG",
    L"Програми, зроблені для SteelSeries GameSense, можуть показувати повідомлення та інше на екрані GameDAC через OpenChatMix, поки SteelSeries GG закритий. OpenChatMix не містить таких програм: ви додаєте наявну, вибравши її файл, а OpenChatMix запускає її й показує те, що вона надсилає.",
    L"SteelSeries GG працює, тож показує ці програми сам. Доповнення запустяться, щойно GG закриють через його значок у треї.",
    L"Доповненням потрібна гарнітура з екраном, як-от Arctis Nova Pro.",
    L"Файл не знайдено",
    L"Працює",
    L"Очікує закриття GG",
    L"Потрібна гарнітура з екраном",
    L"Не працює",
    L"Запускати з OpenChatMix",
    L"Працює, поки SteelSeries GG закритий, і зупиняється, коли GG запускається.",
    L"Показує екрани, які програма описує для дисплеїв SteelSeries.",
    L"Прибрати доповнення",
    L"Виберіть програму GameSense",
    L"Мікрофон вимкнено",
    L"Ви приєдналися",
    L"Ви вийшли",
    L"Заходить у канал",
    L"Виходить з каналу",
    L"Події",
    L"Що кожна подія Discord показує на екрані GameDAC, як у SteelSeries GG.",
    L"Повідомлення",
    L"Хтось говорить",
    L"Ви говорите",
    L"Ваш мікрофон вимкнено",
    L"Хтось заходить у ваш канал",
    L"Хтось виходить з вашого каналу",
    L"Ви заходите в канал",
    L"Ви виходите з каналу",
    L"Показувати на екрані",
    L"Повтор",
    L"Один раз",
    L"Кілька разів",
    L"Поки триває",
    L"Кадри нижче показуються по черзі, потім повертається звичайний екран.",
    L"Кадри нижче показуються по черзі, поки триває подія.",
    L"Скільки разів",
    L"Кадри",
    L"Показуються один за одним, кожен свій час. Перетягніть кадр, щоб пересунути.",
    L"Кадр",
    L"Додати кадр",
    L"Показати на гарнітурі",
    L"Показує, поки працюють доповнення, з тестовими іменами й текстом.",
    L"Скинути до типових",
    L"Показує",
    L"Текст",
    L"Зображення",
    L"Час",
    L"Значок",
    L"Слова",
    L"Заголовок виділено жирним поруч зі значком, текст — під ним. Додайте поле, і подія підставить його значення.",
    L"Заголовок",
    L"Текст",
    L"Заголовок",
    L"Повідомлення",
    L"Користувач",
    L"Канал",
    L"Файл зображення",
    L"Зображення чи GIF, чорно-біле, на весь екран.",
    L"Прибрати кадр",
    L"Олекса (#загальний)",
    L"Ось так повідомлення виглядає на GameDAC.",
    L"Олекса",
    L"Загальний",
    L"Візуалізатор",
    L"Показувати під час відтворення",
    L"Стовпчики, що реагують на звук, заповнюють увесь дисплей GameDAC, як в еквалайзері. Поворот коліщатка на мить повертає звичайний екран; екрани доповнень, як-от повідомлення Discord, з'являються поверх.",
    L"Показувати для",
    L"Музики й відео",
    L"Будь-якого звуку",
    L"Поки програма повідомляє Windows про відтворення, як у медіаелементах керування Windows: плеєри, YouTube у браузері тощо.",
    L"Щоразу, коли гарнітура відтворює звук, зокрема в іграх і дзвінках.",
    L"2,4 ГГц",
    L"3,5 мм",
    L"Колір батареї",
    L"Тема",
    L"Заряд",
    L"Батарея має акцентний колір теми.",
    L"Від зеленого, коли повна, до червоного, коли порожня.",
    L"Резервна копія",
    L"Експорт",
    L"Імпорт",
    L"Налаштування, пресети й плагіни в одному ZIP-файлі. Імпорт перезапускає OpenChatMix, а поточні налаштування зберігає в Backups.",
    L"Експорт налаштувань",
    L"Імпорт налаштувань",
    L"Налаштування OpenChatMix",
    L"Збережено:",
    L"Не вдалося зберегти файл.",
    L"У цьому файлі немає налаштувань OpenChatMix.",
    L"Наступний мікрофон",
    L"Немає іншого мікрофона",
    L"Обробку мікрофона вимкнено",
    L"Експериментально для цієї гарнітури: якщо звук не змінюється, повідомте про це на GitHub.",
    L"Найбільше приглушення шуму",
    L"Менші значення лишають трохи звуку кімнати, і голос звучить природніше; «Без обмеження» прибирає весь шум, який може. DeepFilterNet додає близько 20 мс.",
    L"Без обмеження",
    L"Постфільтр",
    L"Прибирає ще трохи шуму, що лишився між словами; голос може звучати трохи тонше.",
    L"Типовий у Windows",
    L"Кожна група може відтворюватися на окремому пристрої без віртуальних кабелів, наприклад гра й чат у гарнітурі, а медіа на колонках. Windows спрямовує туди програми групи, а ChatMix і далі врівноважує гру й чат.",
    L"Ефекти VST 2 і VST 3 працюють на цьому каналі до його гучності, згори донизу; натисніть на плагін, щоб відкрити його вікно.",
    L"Щоб прибрати шум із голосів співрозмовників, додайте DeepFilterNet-OpenChatMix.dll з релізів OpenChatMix на GitHub.",
    L"Затримка плагінів",
    L"Менші значення лишають трохи звуку кімнати, і голос звучить природніше; «Без обмеження» прибирає весь шум, який може. На каналі DeepFilterNet обробляє суміш обох боків і додає близько 30 мс.",
    L"Самопрослуховування, знімний мікрофон",
    L"Самопрослуховування, вбудований мікрофон",
    L"Керування шумом",
    L"Рівень шумозаглушення",
    L"Рівень прозорості",
    L"Головний екран",
    L"Прозорість",
    L"ANC",
    L"Детальний",
    L"Простий",
    L"Налаштування цієї гарнітури експериментальні: вони працюють за описом її протоколу й ще не підтверджені на ній. Якщо якесь не застосовується, повідомте про це на GitHub.",
    L"Режим",
    L"Діагностика",
    L"Вести лог",
    L"OpenChatMix.log записує, що повідомляє гарнітура, і помилки, для звітів про баги на GitHub. Він може містити назви пристроїв і програм; вимкніть його, коли закінчите.",
    L"Відкрити папку з логом",
    L"Для коліщатка в режимі гучності",
    L"Показує гучність навушників під час обертання коліщатка на GameDAC або базовій станції.",
    L"Гучність навушників",
    L"запасна",
    L"Ефект",
    L"Шум",
    L"Луна кімнати",
    L"Обидва",
    L"Сила",
    L"Менші значення лишають більше початкового звуку.",
    L"Модель завантажується на відеокарту…",
    L"Працює на відеокарті NVIDIA.",
    L"Модель не завантажилась, тож звук проходить без змін. Установіть пакет NVIDIA Audio Effects для цієї відеокарти; з увімкненим логом у ньому буде причина.",
    L"Показувати спливаючі вікна",
    L"Баланс, гучність і короткі повідомлення в куті екрана.",
    L"Кут екрана",
    L"Для коліщатка в режимі ChatMix",
    L"Показує баланс під час обертання коліщатка на GameDAC або базовій станції.",
    L"З'являється, навіть коли спливаючі вікна вимкнено.",
    L"Для цієї гри",
    L"Друкуйте, щоб шукати…",
    L"Нічого не знайдено",
    L"Збереження за",
    L"Готово",
    L"Шумозаглушення ввімкнено",
    L"Прозорість увімкнено",
    L"Керування шумом вимкнено",
    L"Для керування шумом",
    L"Показує режим, коли його змінюють кнопкою на гарнітурі.",
    L"Керування шумом на екрані базової станції",
    L"Ненадовго показує там новий режим, коли його змінюють кнопкою на гарнітурі, поки SteelSeries GG закрито.",
    L"Для звуку через Bluetooth, наприклад із телефона.",
    L"Ваш голос у мікрофоні гарнітури, налаштовується в самій гарнітурі: без віртуального кабелю й без додаткової затримки.",
    L"Прибрати з ChatMix: програма матиме власну гучність",
    L"поза ChatMix",
};

constexpr bool IsComplete(const std::array<const wchar_t*, kCount>& table) {
    for (const wchar_t* text : table) {
        if (!text) {
            return false;
        }
    }
    return true;
}
static_assert(IsComplete(kEnglish) && IsComplete(kUkrainian), "every Text needs a translation");

Language g_language = Language::English;

}  // namespace

void SetLanguage(Language language) {
    g_language = language;
}

Language CurrentLanguage() {
    return g_language;
}

const wchar_t* T(Text text) {
    const auto& table = g_language == Language::Ukrainian ? kUkrainian : kEnglish;
    return table[static_cast<size_t>(text)];
}
