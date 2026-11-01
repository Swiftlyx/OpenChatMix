// What plays now, from the media sessions Windows lists in its media controls (Global System Media Transport
// Controls): players, browsers and other apps that tell Windows their track.

#include "infoscreen.h"

// windows.h defines GetCurrentTime, which clashes with a WinRT method of that name.
#undef GetCurrentTime

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>

namespace control = winrt::Windows::Media::Control;

std::optional<Track> ReadPlayingTrack() {
    static control::GlobalSystemMediaTransportControlsSessionManager manager{nullptr};
    try {
        if (!manager) {
            manager = control::GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
        }
        // The session Windows shows first when it plays, else any that plays.
        std::vector<control::GlobalSystemMediaTransportControlsSession> sessions;
        if (auto current = manager.GetCurrentSession()) {
            sessions.push_back(current);
        }
        for (const auto& session : manager.GetSessions()) {
            sessions.push_back(session);
        }
        for (const auto& session : sessions) {
            if (session.GetPlaybackInfo().PlaybackStatus() !=
                control::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing) {
                continue;
            }
            const auto properties = session.TryGetMediaPropertiesAsync().get();
            Track track{std::wstring(properties.Title()), std::wstring(properties.Artist())};
            if (track.title.empty() && track.artist.empty()) {
                continue;
            }
            return track;
        }
    } catch (const winrt::hresult_error&) {
        manager = nullptr;
    }
    return std::nullopt;
}
