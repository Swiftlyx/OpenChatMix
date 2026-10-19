#include "media_sessions.h"

#include <windows.h>
// windows.h defines GetCurrentTime, which clashes with a WinRT method of that name.
#undef GetCurrentTime

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>

#include "log.h"

namespace control = winrt::Windows::Media::Control;

struct MediaSessions::Impl {
    control::GlobalSystemMediaTransportControlsSessionManager manager{nullptr};
    bool failed = false;  // The manager is not available on this system.
};

MediaSessions::MediaSessions() : impl_(std::make_unique<Impl>()) {}

MediaSessions::~MediaSessions() = default;

bool MediaSessions::AnyPlaying() {
    if (impl_->failed) {
        return false;
    }
    try {
        if (!impl_->manager) {
            impl_->manager = control::GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
        }
        for (const auto& session : impl_->manager.GetSessions()) {
            if (session.GetPlaybackInfo().PlaybackStatus() ==
                control::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing) {
                return true;
            }
        }
    } catch (const winrt::hresult_error& error) {
        if (!impl_->manager) {
            impl_->failed = true;
            Log("media sessions unavailable: 0x%08X", static_cast<unsigned>(error.code()));
        }
    }
    return false;
}
