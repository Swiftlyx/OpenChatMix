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
    L"Apps",                                                               // Apps
    L"Media apps ignore ChatMix",                                          // MediaHint
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
    L"Show popup for hotkeys",                                             // ShowPopup
    L"Translucent background",                                             // Translucent
    L"Quit OpenChatMix",                                                   // Quit
    L"Supports Arctis Nova Pro Wired and Wireless, Nova 7, 7X, 7 Gen 2, Arctis 7+ and Nova 5X. "
    L"Without a headset the balance applies to the default output and is controlled with hotkeys.",  // SupportFooter
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
    L"On",  // OptOn
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
    L"Choose a Game device, a Chat device and an output",  // RoutingNeedsDevices
    L"Game, Chat and Output must be different devices",  // RoutingSameDevice
    L"A selected device is not available",  // RoutingDeviceMissing
    L"Send games to the Game device and voice chat to the Chat device. Apps that play straight to the output are not affected.",  // RoutingCardHint
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
    L"Headphones",  // IdleBounce
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
    L"Apps here follow the Game side. Apps that are in no group land here too.",  // GroupGameHint
    L"Apps here follow the Chat side of the balance.",  // GroupChatHint
    L"Apps here keep their full volume and ignore ChatMix.",  // GroupMediaHint
    L"Plays on",  // PlaysOn
    L"Add app",  // AddToGroup
    L"The equalizer works in virtual device mode: Game and Chat pass through OpenChatMix there, so they can be equalized for any headset.",  // EqNeedsRouting
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
    L"From SteelSeries Sonar",  // PresetsSonar
    L"Appearance",  // Appearance
    L"Theme, color, background",  // AppearanceSummary
    L"Audio",  // AudioSection
    L"Groups, virtual devices, ducking",  // AudioSummary
    L"Notifications",  // Notifications
    L"Popups, battery, SteelSeries GG",  // NotificationsSummary
    L"Game profiles",  // Profiles
    L"Settings that apply while a game runs",  // ProfilesSummary
    L"Theme",  // Theme
    L"Auto",  // ThemeAuto
    L"Light",  // ThemeLight
    L"Dark",  // ThemeDark
    L"Color",  // Color
    L"Windows accent color",  // WindowsColor
    L"Custom color",  // CustomColor
    L"Hue",  // Hue
    L"Saturation",  // Saturation
    L"Type a HEX code, e.g. #3B82F6",  // HexHint
    L"Background",  // Background
    L"Acrylic",  // Acrylic
    L"Mica",  // Mica
    L"Solid",  // Solid
    L"Reduce animations",  // ReduceMotion
    L"Tray icon",  // TrayIcon
    L"Color",  // TrayColor
    L"Monochrome",  // TrayMono
    L"Media group",  // MediaGroupToggle
    L"Apps in Media keep full volume and ignore ChatMix. Not used in virtual device mode.",  // MediaGroupHint
    L"Send apps to their device",  // AutoRoute
    L"Chat apps play on the Chat device, all others on the Game device. Some apps switch only after a restart.",  // AutoRouteHint
    L"Lower game during chat",  // Ducking
    L"While there is sound in chat, the Game side gets quieter.",  // DuckingHint
    L"Lower by",  // DuckingAmount
    L"Popup for the headset wheel",  // DialPopup
    L"Shows the balance when the ChatMix wheel turns, on headsets without a display.",  // DialPopupHint
    L"Low battery warning",  // BatteryAlert
    L"Warn at",  // BatteryThreshold
    L"Headset battery is low",  // BatteryLowTitle
    L"Battery at",  // BatteryLowText
    L"Mute chat",  // MuteChat
    L"Next EQ preset",  // NextPreset
    L"Chat muted",  // ChatMuted
    L"Chat unmuted",  // ChatUnmuted
    L"Add game",  // AddGame
    L"Add a game: its settings apply while it runs and are undone when it closes.",  // NoProfiles
    L"Balance",  // ProfileBalance
    L"Game equalizer",  // ProfileGameEq
    L"Headset equalizer",  // ProfileHeadsetEq
    L"Unchanged",  // Unchanged
    L"Remove profile",  // RemoveProfile
    L"Running now",  // ProfileRunning
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
    L"Adds the Equalizer tab: Game and Chat can be equalized in virtual device mode.",  // EqFeatureHint
    L"Manage presets…",  // ManagePresets
    L"Preset",  // Preset
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
    L"Програми",
    L"Медіа не залежить від ChatMix",
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
    L"Натисніть на комбінацію, щоб змінити її. Підійде й одна клавіша, наприклад крутилка гучності на клавіатурі; інші програми її тоді не отримуватимуть.",
    L"Esc — скасувати · Backspace — прибрати",
    L"Червоні комбінації вже зайняті іншою програмою",
    L"Гарячі клавіші — у налаштуваннях",
    L"Загальне",
    L"Мова",
    L"Запускати разом з Windows",
    L"Спливаюче вікно для гарячих клавіш",
    L"Напівпрозорий фон",
    L"Вийти з OpenChatMix",
    L"Підтримка: Arctis Nova Pro Wired і Wireless, Nova 7, 7X, 7 Gen 2, Arctis 7+, Nova 5X. "
    L"Без гарнітури баланс діє на вихід за замовчуванням і керується клавішами.",
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
    L"Чутність свого голосу",
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
    L"Увімк.",
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
    L"Оберіть пристрої для гри, чату й вихід",
    L"Гра, чат і вихід мають бути різними пристроями",
    L"Вибраний пристрій недоступний",
    L"Направте ігри на пристрій для гри, а голосовий чат — на пристрій для чату. Програми, що грають прямо на вихід, не змінюються.",
    L"Сховати",
    L"Аудіопристроїв не знайдено",
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
    L"Навушники",
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
    L"Грає на екрані GameDAC, поки немає звуку, щоб OLED не вигорав. Звук або коліщатко GameDAC повертають звичайний екран.",
    L"Зараз на екрані",
    L"Вмикати пристрої разом з режимом",
    L"Вмикає пристрої для гри й чату та робить їх типовими у Windows, поки режим працює, і вимикає, коли режим зупинено або OpenChatMix закрито.",
    L"Оберіть нижче будь-які два віртуальні аудіокабелі; поширені підхоплюються автоматично.",
    L"Windows не перемкнув пристрої",
    L"Мікшер",
    L"Групи",
    L"Поки немає програм",
    L"Програми тут ідуть за стороною гри. Сюди ж потрапляють програми без групи.",
    L"Програми тут ідуть за стороною чату.",
    L"Програми тут зберігають повну гучність і не залежать від ChatMix.",
    L"Грає на",
    L"Додати програму",
    L"Еквалайзер працює в режимі віртуальних пристроїв: там гра й чат проходять через OpenChatMix, тож їх можна налаштувати для будь-яких навушників.",
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
    L"Із SteelSeries Sonar",
    L"Вигляд",
    L"Тема, колір, фон",
    L"Звук",
    L"Групи, віртуальні пристрої, приглушення",
    L"Сповіщення",
    L"Спливаючі вікна, батарея, SteelSeries GG",
    L"Ігрові профілі",
    L"Налаштування, що діють під час гри",
    L"Тема",
    L"Авто",
    L"Світла",
    L"Темна",
    L"Колір",
    L"Колір акценту Windows",
    L"Власний колір",
    L"Відтінок",
    L"Насиченість",
    L"Введіть HEX-код, наприклад #3B82F6",
    L"Фон",
    L"Акрил",
    L"Mica",
    L"Суцільний",
    L"Менше анімацій",
    L"Значок у треї",
    L"Кольоровий",
    L"Монохромний",
    L"Група «Медіа»",
    L"Програми з «Медіа» мають повну гучність і не залежать від ChatMix. У режимі віртуальних пристроїв не діє.",
    L"Направляти програми на їхній пристрій",
    L"Програми чату грають на пристрої чату, решта — на пристрої гри. Деякі програми перемкнуться лише після перезапуску.",
    L"Приглушувати гру під час чату",
    L"Поки в чаті є звук, сторона гри стає тихішою.",
    L"Приглушувати на",
    L"Спливаюче вікно для коліщатка",
    L"Показує баланс, коли крутиться коліщатко ChatMix, на гарнітурах без екрана.",
    L"Попередження про низький заряд",
    L"Попереджати при",
    L"Низький заряд гарнітури",
    L"Заряд",
    L"Вимкнути чат",
    L"Наступний пресет EQ",
    L"Чат вимкнено",
    L"Чат увімкнено",
    L"Додати гру",
    L"Додайте гру: її налаштування діятимуть, поки вона запущена, і скасуються, коли вона закриється.",
    L"Баланс",
    L"Еквалайзер гри",
    L"Еквалайзер гарнітури",
    L"Без змін",
    L"Видалити профіль",
    L"Зараз запущена",
    L"Еквалайзер",
    L"Ефекти",
    L"Підсилення басів",
    L"Підсилення верхів",
    L"Чіткість голосу",
    L"Розумна гучність",
    L"Вирівнює гучні й тихі моменти.",
    L"Вимк.",
    L"Тихо",
    L"Збалансовано",
    L"Гучно",
    L"Додає вкладку «Еквалайзер»: у режимі віртуальних пристроїв гру й чат можна налаштувати еквалайзером.",
    L"Керувати пресетами…",
    L"Пресет",
    L"Ігрові пресети",
    L"Застосовується як власна крива. Незбережена власна крива спершу зберігається в «Мої пресети».",
    L"Резервна власна крива",
    L"Оновлення",
    L"Перевіряти оновлення",
    L"Раз на день, із релізів проєкту на GitHub. Більше нічого не надсилається.",
    L"Перевірити",
    L"Перевірка…",
    L"Встановлено найновішу",
    L"Нова версія",
    L"Встановити",
    L"Завантаження…",
    L"Не вдалося оновити",
    L"Не вдалося перевірити",
    L"Сторінка",
    L"Нову версію можна встановити в налаштуваннях.",
    L"Версія",
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
