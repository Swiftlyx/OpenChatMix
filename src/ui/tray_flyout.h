#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "canvas.h"
#include "frame_clock.h"

namespace ui {

// Material 3 surfaces at the tray icon in place of the shell's plain tooltip and Win32 menu: a hover
// card with the device and the balance, and the right-click menu. Each is a layered window drawn
// with Direct2D next to the taskbar, and exists only while it is shown. The card stays above the
// popups the taskbar shows meanwhile, such as the empty tooltip Windows 11 sometimes draws over it.
class TrayFlyout {
public:
    struct Card {
        std::wstring device;     // With the battery when the headset reports one.
        std::wstring companion;  // A second pair of headphones on a line of its own, likewise; may be empty.
        int game = 0;
        int chat = 0;
        bool chatMuted = false;
        bool gameMuted = false;
    };
    struct MenuEntry {
        wchar_t glyph = 0;
        std::wstring text;
        UINT command = 0;      // 0 draws a divider.
        bool checked = false;  // A check at the end, e.g. Start with Windows.
        bool danger = false;   // In the error color, e.g. Quit.
    };

    TrayFlyout(HINSTANCE instance, HWND owner);
    ~TrayFlyout();
    TrayFlyout(const TrayFlyout&) = delete;
    TrayFlyout& operator=(const TrayFlyout&) = delete;

    // Shows or refreshes the hover card; it ignores the mouse and never takes focus.
    void ShowCard(const RECT& icon, const Card& card);
    void HideCard();
    bool CardVisible() const { return window_ && kind_ == Kind::Card; }
    // Opens the menu at the cursor; a chosen entry reaches the owner as WM_COMMAND.
    void ShowMenu(const RECT& icon, POINT cursor, const std::wstring& header, std::vector<MenuEntry> entries);
    bool MenuVisible() const { return window_ && kind_ == Kind::Menu; }
    void SetReduceMotion(bool reduce) { reduceMotion_ = reduce; }

private:
    enum class Kind { None, Card, Menu };

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static void CALLBACK OnShellWindowShown(HWINEVENTHOOK hook, DWORD event, HWND window, LONG object, LONG child,
                                            DWORD thread, DWORD time);
    LRESULT Handle(UINT message, WPARAM wParam, LPARAM lParam);
    // Creates the window for a surface of width x height DIP beside the taskbar near anchor.
    bool Open(Kind kind, const RECT& icon, POINT anchor, float width, float height);
    void Close();
    void Tick();
    void Render();
    void DrawCard();
    void DrawMenu();
    float MenuWidth();
    float MenuHeight() const;
    int EntryAt(POINT pixel) const;
    void Choose(int index);
    void Step(int direction);

    HINSTANCE instance_;
    HWND owner_;
    HWND window_ = nullptr;
    Kind kind_ = Kind::None;
    HWINEVENTHOOK shellHook_ = nullptr;  // Windows the taskbar shows while the card is up.
    Canvas canvas_;
    HDC memoryDc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ previousBitmap_ = nullptr;
    int width_ = 0;  // Window size in pixels, shadow included.
    int height_ = 0;
    POINT origin_{};
    float dpi_ = 96.0f;
    float surfaceWidth_ = 0.0f;  // The card or menu in DIP, without the shadow.
    float surfaceHeight_ = 0.0f;
    POINT slide_{};  // Pixels the surface travels while it appears, from the taskbar.
    FrameClock frames_;
    double shownAt_ = 0.0;
    float appear_ = 0.0f;
    bool reduceMotion_ = false;

    Card card_;
    std::wstring header_;
    std::vector<MenuEntry> entries_;
    int hot_ = -1;  // Entry under the pointer or chosen with the keyboard.
    bool trackingMouse_ = false;
};

}  // namespace ui
