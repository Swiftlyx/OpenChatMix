#include "oled.h"

#include <hidsdi.h>
#include <objbase.h>

#include <algorithm>
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
    AcquireSRWLockExclusive(&lock_);
    requested_.reset();
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

void OledDisplay::Pause() {
    if (playing_.exchange(false) && wake_) {
        SetEvent(wake_);
        WaitForSingleObject(released_, kHandBackTimeoutMs);
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
        if (!playing_) {
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

        AcquireSRWLockShared(&lock_);
        std::shared_ptr<IdleAnimation> requested = requested_;
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
            if (!current->Load(stop_)) {
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
        const DWORD delay = current->Render(frameStart - start, *frame);
        const bool takeover = frameStart - start < kTakeoverMs;
        const bool full = !shownValid || takeover || frameStart - lastFull >= kFullRedrawMs;
        // Pause() may have been called while the frame was rendered.
        if (!playing_) {
            SetEvent(wake_);
            continue;
        }
        if (!drawn) {
            ResetEvent(released_);
        }
        drawn = true;
        if (Draw(*frame, full ? nullptr : shown.get())) {
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

bool OledDisplay::Draw(const OledBitmap& frame, const OledBitmap* shown) {
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
