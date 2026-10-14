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

private:
    static DWORD WINAPI ThreadProc(void* self);
    void Run();
    bool Draw(const OledBitmap& frame, const OledBitmap* shown);
    bool SendRegion(const OledBitmap& frame, int x, int y, int width, int height);
    bool ReturnToDevice();

    HANDLE handle_ = INVALID_HANDLE_VALUE;
    USHORT featureLength_ = 0;
    USHORT outputLength_ = 0;
    uint8_t reportId_ = 0;
    HANDLE thread_ = nullptr;
    HANDLE wake_ = nullptr;      // Auto-reset: a request changed.
    HANDLE released_ = nullptr;  // Manual-reset: the device shows its own interface.
    std::atomic<bool> stop_{false};
    std::atomic<bool> playing_{false};
    SRWLOCK lock_ = SRWLOCK_INIT;
    std::shared_ptr<IdleAnimation> requested_;  // Guarded by lock_.
};
