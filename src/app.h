#pragma once

#include <windows.h>

#include <string>
#include <string_view>

// Private window messages shared between modules.
constexpr UINT WM_APP_TRAY = WM_APP + 1;              // Tray icon callback.
constexpr UINT WM_APP_CHATMIX = WM_APP + 2;           // Headset reported new ChatMix levels.
constexpr UINT WM_APP_DEVICE_LOST = WM_APP + 3;       // Headset reader stopped on an I/O error.
constexpr UINT WM_APP_SESSIONS_CHANGED = WM_APP + 4;  // Audio session created on a target endpoint.
constexpr UINT WM_APP_ENDPOINTS_CHANGED = WM_APP + 5; // Audio endpoint added, removed or default changed.
constexpr UINT WM_APP_SHOW_PANEL = WM_APP + 6;        // Second instance asks to open the panel.
constexpr UINT WM_APP_BROWSE_DONE = WM_APP + 7;       // File dialog finished; lParam owns a std::wstring* or null.
constexpr UINT WM_APP_DIAL_MODE = WM_APP + 8;         // GameDAC knob switched between volume and ChatMix.
constexpr UINT WM_APP_HEADSET = WM_APP + 9;           // Headset reported settings or battery.
constexpr UINT WM_APP_ROUTING = WM_APP + 10;          // A virtual device bridge stopped on an audio error.
constexpr UINT WM_APP_FRAME = WM_APP + 11;            // FrameClock: time to draw the next animation frame.
constexpr UINT WM_APP_UPDATE_CHECKED = WM_APP + 12;   // Update check finished; lParam owns an UpdateCheckResult*.
constexpr UINT WM_APP_UPDATE_INSTALLED = WM_APP + 13; // Update download finished; wParam 1 when it is in place.
constexpr UINT WM_APP_DEVICE_VOLUME = WM_APP + 14;    // Windows volume or mute of a virtual Game or Chat device changed.
constexpr UINT WM_APP_WHEEL = WM_APP + 15;            // Mouse wheel with the ChatMix keys held; lParam is the delta, 0 a click.
constexpr UINT WM_APP_MIC_STOPPED = WM_APP + 16;      // The microphone chain stopped on an audio error.
constexpr UINT WM_APP_MIC_LEVEL = WM_APP + 17;        // The level of a microphone or cable changed.
constexpr UINT WM_APP_GG_EVENT = WM_APP + 18;         // A SteelSeries GG helper handed GG something to show; wParam: GgScreenApp.
constexpr UINT WM_APP_GG_HEARTBEAT = WM_APP + 19;     // The GG watch learned a helper's heartbeat.
constexpr UINT WM_APP_GAMESENSE = WM_APP + 20;        // An add-on sent GameSense requests.

inline constexpr wchar_t kAppName[] = L"OpenChatMix";
inline constexpr wchar_t kMainWindowClass[] = L"OpenChatMix.Main";
// Window class of the NovaChatMix prototype, closed on start so both never drive the headset.
inline constexpr wchar_t kLegacyWindowClass[] = L"NovaChatMix.Main";

inline std::wstring ToLower(std::wstring_view text) {
    std::wstring result(text);
    if (!result.empty()) {
        CharLowerBuffW(result.data(), static_cast<DWORD>(result.size()));
    }
    return result;
}

inline std::wstring FromUtf8(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
    return result;
}

inline std::string Utf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0,
                                         nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr,
                        nullptr);
    return result;
}
