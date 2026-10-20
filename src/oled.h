#pragma once

#include <windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include "chatmix_device.h"

// GameDAC / base station OLED. Drawing protocol as documented by ggoled (github.com/JerwuQu/ggoled):
// feature report [id] 93 [x] [y] [width <= 64] [height, multiple of 8] followed by column-major pixels;
// output report [id] 95 hands the display back to the device's own interface.

constexpr int kOledWidth = 128;
constexpr int kOledHeight = 64;

// Where an icon sits over the animation.
enum class OledCorner : uint8_t { TopLeft, TopRight, BottomLeft, BottomRight };
// How an icon over the animation blinks.
enum class OledBlink : uint8_t { None, Slow, Fast };

// One frame, one byte per pixel (0 off, 1 on), row-major.
struct OledBitmap {
    std::array<uint8_t, kOledWidth * kOledHeight> pixels{};

    void Clear() { pixels.fill(0); }
    bool Get(int x, int y) const { return pixels[y * kOledWidth + x] != 0; }
    // Ignores coordinates outside the screen.
    void Set(int x, int y, bool on = true) {
        if (x >= 0 && x < kOledWidth && y >= 0 && y < kOledHeight) {
            pixels[y * kOledWidth + x] = on ? 1 : 0;
        }
    }
    bool operator==(const OledBitmap&) const = default;
};

class IdleAnimation {
public:
    virtual ~IdleAnimation() = default;
    // Runs on the display thread (COM MTA) before the first frame; may take a while, e.g. to decode
    // a GIF, and should return early once cancel is set.
    virtual bool Load(const std::atomic<bool>& cancel) {
        (void)cancel;
        return true;
    }
    // Draws the frame elapsedMs after the animation started; returns the milliseconds until the
    // next frame is due.
    virtual DWORD Render(ULONGLONG elapsedMs, OledBitmap& frame) = 0;
};

// Plays an animation on the display from its own thread and its own HID handle, so drawing never
// blocks the ChatMix commands of the main thread.
class OledDisplay {
public:
    OledDisplay() = default;
    ~OledDisplay();
    OledDisplay(const OledDisplay&) = delete;
    OledDisplay& operator=(const OledDisplay&) = delete;

    bool Open(const DisplayCollection& collection);
    // Returns the display to the device and stops the thread.
    void Close();
    bool IsOpen() const { return thread_ != nullptr; }

    // Starts drawing the animation, replacing the current one. After Pause() or with another
    // animation it starts again at elapsed time 0.
    void Play(std::shared_ptr<IdleAnimation> animation);
    // Returns the display to the device and waits until it shows its own interface again, so commands
    // sent right after appear on it; Play() resumes.
    void Pause();
    bool IsPlaying() const { return playing_.load(); }
    // Stops drawing but keeps the display, so what another app (SteelSeries GG) draws meanwhile stays
    // whole; drawing resumes with a complete frame. Pause() still gives the display back.
    void Hold(bool hold);
    // Starts holding from any thread: drawing stops before its next report, even within a frame. Hold()
    // decides afterwards when it ends.
    void HoldNow() { holding_ = true; }
    // Shows the Bluetooth icon over the animation in a corner, steady or blinking. Like a still image it moves by a
    // pixel every half minute, so it does not burn in.
    void SetBluetoothIcon(bool shown, OledBlink blink, OledCorner corner);
    // Shows an add-on's screen over the screensaver, or instead of the device's own screen, until it is replaced
    // or cleared with null; what was there before comes back after it. GG holds do not apply to it.
    void SetOverlay(std::shared_ptr<IdleAnimation> overlay);

private:
    static DWORD WINAPI ThreadProc(void* self);
    void Run();
    // interrupted: a hold began before the frame was sent whole.
    bool Draw(const OledBitmap& frame, const OledBitmap* shown, bool& interrupted);
    bool SendRegion(const OledBitmap& frame, int x, int y, int width, int height);
    bool ReturnToDevice();
    // Draws the icon of badge_ into the frame; returns the milliseconds until it changes.
    static DWORD DrawBadge(OledBitmap& frame, uint8_t badge, ULONGLONG now);

    HANDLE handle_ = INVALID_HANDLE_VALUE;
    USHORT featureLength_ = 0;
    USHORT outputLength_ = 0;
    uint8_t reportId_ = 0;
    HANDLE thread_ = nullptr;
    HANDLE wake_ = nullptr;      // Auto-reset: a request changed.
    HANDLE released_ = nullptr;  // Manual-reset: the device shows its own interface.
    std::atomic<bool> stop_{false};
    std::atomic<bool> playing_{false};
    std::atomic<bool> holding_{false};
    std::atomic<uint8_t> badge_{0};  // 0 none; otherwise bit 0 shown, bits 2-3 the corner, bits 4-5 the blink.
    std::atomic<bool> overlaid_{false};
    SRWLOCK lock_ = SRWLOCK_INIT;
    std::shared_ptr<IdleAnimation> requested_;
    std::shared_ptr<IdleAnimation> overlay_;  // Under lock_.
};
