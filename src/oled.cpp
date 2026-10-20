#include "oled.h"

#include <hidsdi.h>
#include <objbase.h>

#include <algorithm>
#include <exception>
#include <vector>

#include "log.h"

namespace {

constexpr uint8_t kDrawCommand = 0x93;
constexpr uint8_t kReturnCommand = 0x95;
constexpr int kHeaderSize = 6;          // [id] 93 [x] [y] [width] [height]
constexpr int kMaxRegionWidth = 64;     // Wider regions are split, as the device expects.
constexpr int kSendAttempts = 3;
constexpr ULONGLONG kFullRedrawMs = 5000;  // Also repairs anything the device drew in between.
// Right after taking over, the device may still finish drawing its own screen over the first frames,
// so whole frames are sent at short intervals for a while.
constexpr ULONGLONG kTakeoverMs = 2000;
constexpr DWORD kTakeoverFrameMs = 100;
constexpr int kFailuresBeforeBackoff = 5;
constexpr DWORD kBackoffMs = 2000;
constexpr DWORD kStopTimeoutMs = 5000;
// A frame in flight takes up to two reports of about 30 ms each before the hand-back.
constexpr DWORD kHandBackTimeoutMs = 250;
// The GameDAC redraws its interface after the hand-back; commands sooner may not show on it.
constexpr DWORD kHandBackSettleMs = 20;
constexpr uint8_t kBadgeShown = 0x01;
constexpr int kBadgeCornerShift = 2;
constexpr int kBadgeBlinkShift = 4;
constexpr int kBadgeMargin = 1;
constexpr ULONGLONG kBadgeShiftMs = 30000;
// Half periods, as the headset's LED blinks: slowly while Bluetooth waits for a device, fast while it pairs.
constexpr ULONGLONG kBadgeSlowBlinkMs = 1000;
constexpr ULONGLONG kBadgeFastBlinkMs = 250;
// The Bluetooth rune, 5 x 9 pixels.
constexpr const char* kBluetoothRune[] = {"..#..", "..##.", "#.#.#", ".###.", "..#..",
                                          ".###.", "#.#.#", "..##.", "..#.."};

}  // namespace

OledDisplay::~OledDisplay() {
    Close();
}

bool OledDisplay::Open(const DisplayCollection& collection) {
    Close();
    // One report must hold at least a 64x64 block, which also sets the frame rate (two reports per frame).
    constexpr int kMinFeatureLength = kHeaderSize + kMaxRegionWidth * kOledHeight / 8;
    if (collection.featureLength < kMinFeatureLength || collection.outputLength < 2) {
        Log("display: unsupported report sizes (feature %u, out %u)", collection.featureLength,
            collection.outputLength);
        return false;
    }
    handle_ = CreateFileW(collection.path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                          nullptr, OPEN_EXISTING, 0, nullptr);
    if (handle_ == INVALID_HANDLE_VALUE) {
        Log("display: open failed, error %lu", GetLastError());
        return false;
    }
    featureLength_ = collection.featureLength;
    outputLength_ = collection.outputLength;
    reportId_ = collection.reportId;
    stop_ = false;
    playing_ = false;
    wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    released_ = CreateEventW(nullptr, TRUE, TRUE, nullptr);
    thread_ = wake_ && released_ ? CreateThread(nullptr, 0, ThreadProc, this, 0, nullptr) : nullptr;
    if (!thread_) {
        Log("display: thread start failed, error %lu", GetLastError());
        Close();
        return false;
    }
    return true;
}

void OledDisplay::Close() {
    if (thread_) {
        stop_ = true;
        SetEvent(wake_);
        if (WaitForSingleObject(thread_, kStopTimeoutMs) != WAIT_OBJECT_0) {
            // The thread still uses the handles; leaking them is safer than closing them under it.
            Log("display: thread did not stop");
            thread_ = nullptr;
            wake_ = nullptr;
            released_ = nullptr;
            handle_ = INVALID_HANDLE_VALUE;
        } else {
            CloseHandle(thread_);
            thread_ = nullptr;
        }
    }
    if (wake_) {
        CloseHandle(wake_);
        wake_ = nullptr;
    }
    if (released_) {
        CloseHandle(released_);
        released_ = nullptr;
    }
    if (handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
    playing_ = false;
    overlaid_ = false;
    AcquireSRWLockExclusive(&lock_);
    requested_.reset();
    overlay_.reset();
    ReleaseSRWLockExclusive(&lock_);
}

void OledDisplay::Play(std::shared_ptr<IdleAnimation> animation) {
    if (!thread_ || !animation) {
        return;
    }
    AcquireSRWLockExclusive(&lock_);
    requested_ = std::move(animation);
    ReleaseSRWLockExclusive(&lock_);
    playing_ = true;
    SetEvent(wake_);
}

void OledDisplay::Hold(bool hold) {
    if (holding_.exchange(hold) != hold && wake_) {
        SetEvent(wake_);
    }
}

void OledDisplay::SetBluetoothIcon(bool shown, OledBlink blink, OledCorner corner) {
    const uint8_t badge = shown ? static_cast<uint8_t>(kBadgeShown | (static_cast<uint8_t>(corner) << kBadgeCornerShift) |
                                                       (static_cast<uint8_t>(blink) << kBadgeBlinkShift))
                                : 0;
    if (badge_.exchange(badge) != badge && wake_) {
        SetEvent(wake_);
    }
}

void OledDisplay::SetOverlay(std::shared_ptr<IdleAnimation> overlay) {
    AcquireSRWLockExclusive(&lock_);
    overlaid_ = overlay != nullptr;
    overlay_ = std::move(overlay);
    ReleaseSRWLockExclusive(&lock_);
    if (wake_) {
        SetEvent(wake_);
    }
}

void OledDisplay::Pause() {
    if (playing_.exchange(false) && wake_) {
        SetEvent(wake_);
        // An add-on's screen keeps the display until it ends.
        if (!overlaid_) {
            WaitForSingleObject(released_, kHandBackTimeoutMs);
        }
    }
}

DWORD WINAPI OledDisplay::ThreadProc(void* self) {
    static_cast<OledDisplay*>(self)->Run();
    return 0;
}

void OledDisplay::Run() {
    // WIC image decoding in IdleAnimation::Load needs COM.
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    std::shared_ptr<IdleAnimation> current;
    bool loaded = false;
    bool started = false;  // The current animation runs since `start`.
    bool drawn = false;    // The display shows our frames instead of the device interface.
    bool shownValid = false;
    bool failureLogged = false;
    int failures = 0;
    ULONGLONG start = 0;
    ULONGLONG resumed = 0;  // Drawing went on after a hold; the device may have its screen back by then.
    bool held = false;
    ULONGLONG lastFull = 0;
    // Frames live on the heap; together they would take 16 KB of the thread's stack.
    const auto shown = std::make_unique<OledBitmap>();
    const auto frame = std::make_unique<OledBitmap>();
    DWORD wait = INFINITE;

    for (;;) {
        WaitForSingleObject(wake_, wait);
        if (stop_) {
            break;
        }
        if (!playing_ && !overlaid_) {
            if (drawn) {
                ReturnToDevice();
                drawn = false;
                Sleep(kHandBackSettleMs);
            }
            SetEvent(released_);
            shownValid = false;
            started = false;
            wait = INFINITE;
            continue;
        }

        if (holding_ && !overlaid_) {
            // Whatever is on the display stays; the next frame is sent whole.
            held = true;
            shownValid = false;
            wait = INFINITE;
            continue;
        }

        AcquireSRWLockShared(&lock_);
        const bool overlay = overlay_ != nullptr;
        std::shared_ptr<IdleAnimation> requested = overlay ? overlay_ : requested_;
        ReleaseSRWLockShared(&lock_);
        if (requested != current) {
            current = std::move(requested);
            loaded = false;
            started = false;
        }
        if (!current) {
            wait = INFINITE;
            continue;
        }
        if (!loaded) {
            // Decoding a large GIF can take a while; it must not slow down the rest of the PC, and a
            // file that needs more memory than there is only fails to load.
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
            bool ok = false;
            try {
                ok = current->Load(stop_);
            } catch (const std::exception&) {
                Log("display: out of memory while loading the animation");
            }
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
            if (!ok) {
                if (!stop_) {
                    Log("display: animation could not be loaded");
                }
                // Stays idle until another animation is requested.
                wait = INFINITE;
                continue;
            }
            loaded = true;
        }
        if (!started) {
            start = GetTickCount64();
            started = true;
            failures = 0;
        }

        const ULONGLONG frameStart = GetTickCount64();
        frame->Clear();
        DWORD delay = current->Render(frameStart - start, *frame);
        if (const uint8_t badge = badge_.load(); badge != 0 && !overlay) {
            delay = std::min(delay, DrawBadge(*frame, badge, frameStart));
        }
        if (held) {
            // SteelSeries GG may have given the display back to the device meanwhile: a takeover again.
            held = false;
            resumed = frameStart;
        }
        const bool takeover = frameStart - start < kTakeoverMs || frameStart - resumed < kTakeoverMs;
        const bool full = !shownValid || takeover || frameStart - lastFull >= kFullRedrawMs;
        // Pause() may have been called while the frame was rendered.
        if (!playing_ && !overlaid_) {
            SetEvent(wake_);
            continue;
        }
        if (!drawn) {
            ResetEvent(released_);
        }
        drawn = true;
        bool interrupted = false;
        if (Draw(*frame, full ? nullptr : shown.get(), interrupted)) {
            if (interrupted) {
                // Part of the frame is on the display, so the next one is sent whole.
                shownValid = false;
                wait = 0;
                continue;
            }
            *shown = *frame;
            shownValid = true;
            failures = 0;
            if (full) {
                lastFull = frameStart;
            }
            if (failureLogged) {
                Log("display: drawing recovered");
                failureLogged = false;
            }
        } else {
            shownValid = false;
            if (++failures >= kFailuresBeforeBackoff) {
                if (!failureLogged) {
                    Log("display: drawing failed, error %lu", GetLastError());
                    failureLogged = true;
                }
                wait = kBackoffMs;
                continue;
            }
        }
        const ULONGLONG spent = GetTickCount64() - frameStart;
        const DWORD next = takeover ? std::min(delay, kTakeoverFrameMs) : delay;
        wait = next > spent ? static_cast<DWORD>(next - spent) : 0;
    }

    if (drawn) {
        ReturnToDevice();
    }
    SetEvent(released_);
    if (SUCCEEDED(com)) {
        CoUninitialize();
    }
}

bool OledDisplay::Draw(const OledBitmap& frame, const OledBitmap* shown, bool& interrupted) {
    // Only the bounding box of the changed pixels is sent: every report takes the device about
    // 30 ms regardless of its size.
    int left = 0;
    int top = 0;
    int right = kOledWidth;  // Exclusive.
    int bottom = kOledHeight;
    if (shown) {
        left = kOledWidth;
        top = kOledHeight;
        right = 0;
        bottom = 0;
        for (int y = 0; y < kOledHeight; ++y) {
            for (int x = 0; x < kOledWidth; ++x) {
                if (frame.pixels[y * kOledWidth + x] != shown->pixels[y * kOledWidth + x]) {
                    left = std::min(left, x);
                    right = std::max(right, x + 1);
                    top = std::min(top, y);
                    bottom = std::max(bottom, y + 1);
                }
            }
        }
        if (right == 0) {
            return true;
        }
    }
    // Columns are sent in whole bytes, so regions start and end on multiples of 8 rows.
    top &= ~7;
    bottom = (bottom + 7) & ~7;
    const int height = bottom - top;
    const int bitsPerReport = (featureLength_ - kHeaderSize) * 8;
    const int maxWidth = std::min(kMaxRegionWidth, bitsPerReport / height);
    for (int x = left; x < right; x += maxWidth) {
        if (holding_ && !overlaid_) {
            interrupted = true;
            return true;
        }
        if (!SendRegion(frame, x, top, std::min(maxWidth, right - x), height)) {
            return false;
        }
    }
    return true;
}

bool OledDisplay::SendRegion(const OledBitmap& frame, int left, int top, int width, int height) {
    std::vector<uint8_t> report(featureLength_, 0);
    report[0] = reportId_;
    report[1] = kDrawCommand;
    report[2] = static_cast<uint8_t>(left);
    report[3] = static_cast<uint8_t>(top);
    report[4] = static_cast<uint8_t>(width);
    report[5] = static_cast<uint8_t>(height);
    // Column-major: bit (x * height + y) of the payload, least significant bit first.
    for (int x = 0; x < width; ++x) {
        for (int y = 0; y < height; ++y) {
            if (frame.Get(left + x, top + y)) {
                const int bit = x * height + y;
                report[kHeaderSize + bit / 8] |= static_cast<uint8_t>(1u << (bit % 8));
            }
        }
    }
    for (int attempt = 1; attempt <= kSendAttempts; ++attempt) {
        if (HidD_SetFeature(handle_, report.data(), featureLength_)) {
            return true;
        }
        if (stop_) {
            break;
        }
        Sleep(10 * attempt * attempt);
    }
    return false;
}

DWORD OledDisplay::DrawBadge(OledBitmap& frame, uint8_t badge, ULONGLONG now) {
    constexpr int kRuneWidth = 5;
    constexpr int kRuneHeight = static_cast<int>(std::size(kBluetoothRune));
    // A black margin keeps the icon readable over the animation.
    constexpr int kBoxWidth = kRuneWidth + 2;
    constexpr int kBoxHeight = kRuneHeight + 2;
    static constexpr int kShifts[][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    const int* shift = kShifts[(now / kBadgeShiftMs) % std::size(kShifts)];
    const auto corner = static_cast<OledCorner>((badge >> kBadgeCornerShift) & 0x03);
    const bool right = corner == OledCorner::TopRight || corner == OledCorner::BottomRight;
    const bool bottom = corner == OledCorner::BottomLeft || corner == OledCorner::BottomRight;
    const int left = right ? kOledWidth - kBadgeMargin - kBoxWidth - shift[0] : kBadgeMargin + shift[0];
    const int top = bottom ? kOledHeight - kBadgeMargin - kBoxHeight - shift[1] : kBadgeMargin + shift[1];
    ULONGLONG next = kBadgeShiftMs - now % kBadgeShiftMs;
    const auto blink = static_cast<OledBlink>((badge >> kBadgeBlinkShift) & 0x03);
    if (blink != OledBlink::None) {
        const ULONGLONG half = blink == OledBlink::Fast ? kBadgeFastBlinkMs : kBadgeSlowBlinkMs;
        next = std::min(next, half - now % half);
        if ((now / half) % 2) {
            return static_cast<DWORD>(next);
        }
    }
    for (int y = 0; y < kBoxHeight; ++y) {
        for (int x = 0; x < kBoxWidth; ++x) {
            frame.Set(left + x, top + y, false);
        }
    }
    for (int y = 0; y < kRuneHeight; ++y) {
        for (int x = 0; x < kRuneWidth; ++x) {
            if (kBluetoothRune[y][x] == '#') {
                frame.Set(left + 1 + x, top + 1 + y);
            }
        }
    }
    return static_cast<DWORD>(next);
}

bool OledDisplay::ReturnToDevice() {
    std::vector<uint8_t> report(outputLength_, 0);
    report[0] = reportId_;
    report[1] = kReturnCommand;
    DWORD written = 0;
    if (!WriteFile(handle_, report.data(), outputLength_, &written, nullptr)) {
        Log("display: return to device failed, error %lu", GetLastError());
        return false;
    }
    return true;
}
