#pragma once

// Layout constants and helpers shared by the panel implementation files.

#include <d2d1.h>

#include <cstdint>
#include <cwctype>
#include <string>
#include <string_view>

#include "../i18n.h"
#include "../settings.h"
#include "design.h"

namespace ui::detail {

inline constexpr wchar_t kWindowClass[] = L"OpenChatMix.Panel";
// Short names for the design tokens used everywhere in the panel (see design.h).
inline constexpr float kWidth = design::kPanelWidth;
inline constexpr float kHeight = design::kPanelHeight;
inline constexpr float kHeaderHeight = design::kHeaderHeight;
inline constexpr float kNavHeight = design::kNavHeight;
inline constexpr float kScreenMargin = design::kScreenMargin;
inline constexpr float kRowHeight = design::kRowHeight;
inline constexpr float kRowHintTop = design::kHintTopInRow;
inline constexpr float kGutter = design::kPageGutter;
inline constexpr float kInset = design::kContentInset;
inline constexpr ULONGLONG kReopenGuardMs = 400;
inline constexpr UINT_PTR kMeterTimer = 1;     // Panel window timer that reads the level meters.
inline constexpr UINT_PTR kTooltipTimer = 2;   // Panel window timer for the tooltip delay.
inline constexpr ULONGLONG kSaveDoneMs = 2000;  // How long Done shows after a headset save.
inline constexpr UINT kTooltipDelayMs = 500;
inline constexpr UINT kMeterIntervalMs = 33;  // A tray click that closed the panel must not reopen it.

// Segoe Fluent Icons / Segoe MDL2 Assets code points.
inline constexpr wchar_t kGlyphSettings = 0xE713;
inline constexpr wchar_t kGlyphBack = 0xE72B;
inline constexpr wchar_t kGlyphAdd = 0xE710;
inline constexpr wchar_t kGlyphSearch = 0xE721;
inline constexpr wchar_t kGlyphGame = 0xE7FC;
inline constexpr wchar_t kGlyphChat = 0xE8BD;
inline constexpr wchar_t kGlyphFolder = 0xE838;
inline constexpr wchar_t kGlyphPower = 0xE7E8;
inline constexpr wchar_t kGlyphCheck = 0xE73E;
inline constexpr wchar_t kGlyphHeadphones = 0xE7F6;
inline constexpr wchar_t kGlyphSpeakers = 0xE7F5;
inline constexpr wchar_t kGlyphChevronLeft = 0xE76B;
inline constexpr wchar_t kGlyphChevronRight = 0xE76C;
inline constexpr wchar_t kGlyphReset = 0xE72C;
inline constexpr wchar_t kGlyphSave = 0xE74E;
inline constexpr wchar_t kGlyphImport = 0xE8E5;
inline constexpr wchar_t kGlyphBattery0 = 0xEBA0;          // EBA0..EBAA: battery 0-100%.
inline constexpr wchar_t kGlyphBatteryCharging0 = 0xEBAB;  // EBAB..EBB5: charging 0-100%.
inline constexpr wchar_t kGlyphMixer = 0xF4C3;
inline constexpr wchar_t kGlyphEqualizer = 0xE9E9;
inline constexpr wchar_t kGlyphPalette = 0xE790;
inline constexpr wchar_t kGlyphVolume = 0xE767;
inline constexpr wchar_t kGlyphKeyboard = 0xE765;
inline constexpr wchar_t kGlyphBell = 0xEA8F;
inline constexpr wchar_t kGlyphDelete = 0xE74D;
inline constexpr wchar_t kGlyphMute = 0xE74F;
inline constexpr wchar_t kGlyphEdit = 0xE70F;
inline constexpr wchar_t kGlyphCopy = 0xE8C8;
inline constexpr wchar_t kGlyphContrast = 0xE793;
inline constexpr wchar_t kGlyphChevronDown = 0xE70D;
inline constexpr wchar_t kGlyphChevronUp = 0xE70E;
inline constexpr wchar_t kGlyphRemove = 0xE738;  // Minus.
inline constexpr wchar_t kGlyphMicrophone = 0xE720;
inline constexpr wchar_t kGlyphOpenWindow = 0xE8A7;
inline constexpr wchar_t kGlyphPlugin = 0xEA86;
inline constexpr wchar_t kGlyphDragHandle = 0xE76F;  // GripperBarHorizontal.
inline constexpr wchar_t kGlyphPin = 0xE718;
inline constexpr wchar_t kGlyphPinFill = 0xE842;  // The pin's filled head without its needle; drawn over kGlyphPin.
inline constexpr wchar_t kGlyphBlocked = 0xE733;
inline constexpr wchar_t kGlyphUndo = 0xE7A7;
inline constexpr wchar_t kGlyphInfo = 0xE946;

// Parts of a widget (segments, stepper arrows) get IDs above all fixed ones, so they never collide.
inline constexpr uint32_t PartId(uint32_t id, uint32_t part) {
    return id ^ (part << 24);
}

// Fixed widget IDs; per-item widgets use HashId() with the high bit set.
enum : uint32_t {
    kIdBack = 1,
    kIdMark,
    kIdMixCard,
    kIdSlider,
    kIdRunningList,
    kIdSettingsPage,
    kIdBrowse,
    kIdAutostart,
    kIdShowPopup,
    kIdQuit,
    kIdWarnGg,
    kIdEqReset,
    kIdSnackbar,
    kIdRoutingToggle,
    kIdPickList,
    kIdEqSave,
    kIdEqImport,
    kIdEqFolder,
    kIdRoutingManage,
    kIdIdleChoose,
    kIdSnackbarAction,
    kIdNavigation,    // + destination
    kIdEqGraph = 140,
    kIdEqChannel,
    kIdEqToggle,
    kIdEqPresetButton,
    kIdEqReset2,
    kIdEqPreamp,
    kIdEqBandSelect,
    kIdEqType,
    kIdEqFrequency,
    kIdEqGain,
    kIdEqQ,
    kIdEqBandToggle,
    kIdEqSaveFile,
    kIdEqImportFile,
    kIdEqFolder2,
    kIdEqOpenAudio,
    kIdTheme = 170,
    kIdBackdrop,
    kIdTray,
    kIdReduceMotion,
    kIdContrast,
    kIdSwatch = 180,      // + swatch
    kIdChatSwatch = 190,  // + swatch
    kIdHueSlider = 200,
    kIdChromaSlider,
    kIdMediaGroup,
    kIdAutoRoute,
    kIdDucking,
    kIdDuckingAmount,
    kIdDialPopup,
    kIdBatteryAlert,
    kIdBatteryThreshold,
    kIdAddProfile,
    kIdRemoveProfile,
    kIdProfileBalance,
    kIdProfileGameEq,
    kIdProfileHeadsetEq,
    kIdGroupAdd,
    kIdGroupList,
    kIdRoutingStatus,
    kIdMenu,
    kIdMenuList,
    kIdEqBass,
    kIdEqTreble,
    kIdEqVoice,
    kIdEqSmart,
    kIdEqFeature,
    kIdGamePresets,
    kIdCheckUpdates,
    kIdUpdateAction,
    kIdTrayBalanced,
    kIdWheelControl,
    kIdMicToggle,
    kIdMicKeepLevels,
    kIdMicNoise,
    kIdMicCompressor,
    kIdMicEqEdit,
    kIdAddPlugin,
    kIdMicInputRow,
    kIdMicOutputRow,
    kIdSwitchOnLoss,
    kIdReturnToPreferred,
    kIdPin,
    kIdMicMonitor,
    kIdChainList,
    kIdPluginList,
    kIdOutputList,
    kIdGameVolume,
    kIdChatVolume,
    kIdMicLevel,
    kIdTourNext,
    kIdTourBack,
    kIdTourSkip,
    kIdTourStep,
    kIdTourShow,
    kIdTooltip,
    kIdProfileChatEq,
    kIdProfileMediaEq,
    kIdIdleTest,
    kIdAddAddOn,
    kIdAddOnEnabled,
    kIdRemoveAddOn,
    kIdAddOnEventEnabled,
    kIdAddOnTimes,
    kIdAddOnDisplayList,
    kIdAddOnAddDisplay,
    kIdAddOnPreview,
    kIdAddOnReset,
    kIdAddOnDisplayKind,
    kIdAddOnLength,
    kIdAddOnImage,
    kIdAddOnIcons,
    kIdAddOnHeading,
    kIdAddOnText,
    kIdAddOnRemoveDisplay,
    kIdVisualizer,
    kIdVisualizerFor,
    kIdBatteryTint,
    kIdTrayBatteryTint,
    kIdSettingsExport,
    kIdSettingsImport,
    kIdLog,
    kIdLogFolder,
    kIdVolumePopup,
    kIdPopups,
    kIdPopupCorner,
    kIdKnobPopup,
    kIdMenuScrollbar,
    kIdNoisePopup,
    kIdNoiseScreen,
    kIdPageScrollbar,
    kIdHotkeyRow = 40,
    kIdAddGroup = 60,
    kIdLanguage = 70,
    kIdEqBand = 80,     // + band
    kIdEqPreset = 100,  // + segment
    kIdRoutingRow = 120,  // + slot
};

// The headset's filter editor uses the equalizer's widget IDs plus this, above the fixed ones and below parts and
// hashed IDs; the Bluetooth equalizer and the switch between the two use them plus the second base.
inline constexpr uint32_t kIdHeadsetEqBase = 0x10000;
inline constexpr uint32_t kIdBluetoothEqBase = 0x20000;

enum : uint32_t {
    kKindGroup = 1,
    kKindRunning = 2,
    kKindSetting = 3,
    kKindEndpoint = 4,
    kKindPreset = 5,
    kKindIdle = 6,
    kKindNav = 7,
    kKindProfile = 8,
    kKindEqPreset = 9,
    kKindPlugin = 10,
    kKindOutput = 11,
    kKindStage = 12,
    kKindAppRemove = 13,
    kKindOutputExclude = 14,
    kKindAddOn = 15,
};

inline constexpr uint32_t kHoverChannel = 0xF0;  // Animation channels shared by the components.
inline constexpr uint32_t kShapeChannel = 0xF1;
inline constexpr uint32_t kPressChannel = 0xF2;
inline constexpr uint32_t kSelectChannel = 0xF3;

inline constexpr AppGroup kGroups[] = {AppGroup::Game, AppGroup::Chat, AppGroup::Media};
inline constexpr AppGroup kAddGroups[] = {AppGroup::Chat, AppGroup::Media};
inline constexpr Language kLanguages[] = {Language::English, Language::Ukrainian};
// Language names are shown in their own language regardless of the current UI language.
inline constexpr const wchar_t* kLanguageLabels[] = {L"English", L"Українська"};

inline uint32_t HashId(uint32_t kind, std::wstring_view key) {
    uint32_t hash = 2166136261u ^ kind;
    for (const wchar_t c : key) {
        hash ^= static_cast<uint32_t>(std::towlower(c));
        hash *= 16777619u;
    }
    return hash | 0x80000000u;
}

inline uint64_t AnimKey(uint32_t id, uint32_t channel) {
    return (static_cast<uint64_t>(id) << 8) | channel;
}

inline D2D1_RECT_F Inset(const D2D1_RECT_F& rect, float dx, float dy) {
    return {rect.left + dx, rect.top + dy, rect.right - dx, rect.bottom - dy};
}

inline bool Contains(const D2D1_RECT_F& rect, float x, float y) {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

// Text on the Windows clipboard, one line of it; empty when it holds none.
std::wstring ClipboardText(HWND owner);
void SetClipboardText(HWND owner, const std::wstring& text);

inline std::wstring Percent(int value) {
    return std::to_wstring(value) + L"%";
}

inline std::wstring HexColor(uint32_t rgb) {
    wchar_t text[16];
    swprintf_s(text, L"#%06X", rgb & 0xFFFFFF);
    return text;
}

}  // namespace ui::detail
