#include "media_sessions.h"

// windows.h defines GetCurrentTime, which clashes with a WinRT method of that name.
#undef GetCurrentTime

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>

#include <atomic>

#include "log.h"

namespace control = winrt::Windows::Media::Control;

namespace {

constexpr DWORD kCheckMs = 1000;
constexpr ULONGLONG kIdleAfterMs = 3000;  // Checks pause when nobody asked for this long.
constexpr DWORD kStopWaitMs = 1000;

}  // namespace

struct MediaSessions::State {
    std::atomic<bool> playing{false};
    std::atomic<ULONGLONG> lastAsked{0};
    std::atomic<bool> stop{false};
    HANDLE wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    State() = default;
    State(const State&) = delete;
    State& operator=(const State&) = delete;
    ~State() {
        if (wake) {
            CloseHandle(wake);
        }
    }
};

MediaSessions::MediaSessions() : state_(std::make_shared<State>()) {}

MediaSessions::~MediaSessions() {
    if (thread_) {
        state_->stop = true;
        SetEvent(state_->wake);
        // A check stuck in another process is left behind; the thread keeps the state it uses alive.
        WaitForSingleObject(thread_, kStopWaitMs);
        CloseHandle(thread_);
    }
}

bool MediaSessions::AnyPlaying() {
    const ULONGLONG now = GetTickCount64();
    const ULONGLONG previous = state_->lastAsked.exchange(now);
    if (!thread_) {
        auto* holder = new std::shared_ptr<State>(state_);
        thread_ = CreateThread(nullptr, 0, CheckLoop, holder, 0, nullptr);
        if (!thread_) {
            delete holder;
        }
    } else if (now - previous >= kIdleAfterMs) {
        // The thread waits without a timeout while nobody asks.
        SetEvent(state_->wake);
    }
    return state_->playing;
}

DWORD WINAPI MediaSessions::CheckLoop(void* parameter) {
    const std::unique_ptr<std::shared_ptr<State>> holder(static_cast<std::shared_ptr<State>*>(parameter));
    State& state = **holder;
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    {
        control::GlobalSystemMediaTransportControlsSessionManager manager{nullptr};
        while (!state.stop) {
            const bool asked = GetTickCount64() - state.lastAsked < kIdleAfterMs;
            if (asked) {
                try {
                    if (!manager) {
                        manager = control::GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
                    }
                    bool playing = false;
                    for (const auto& session : manager.GetSessions()) {
                        if (session.GetPlaybackInfo().PlaybackStatus() ==
                            control::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing) {
                            playing = true;
                            break;
                        }
                    }
                    state.playing = playing;
                } catch (const winrt::hresult_error& error) {
                    state.playing = false;
                    if (!manager) {
                        Log("media sessions unavailable: 0x%08X", static_cast<unsigned>(error.code()));
                        break;
                    }
                }
            }
            WaitForSingleObject(state.wake, asked ? kCheckMs : INFINITE);
        }
    }
    winrt::uninit_apartment();
    return 0;
}
