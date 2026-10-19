#pragma once

#include <memory>

// The media sessions Windows lists in its media controls: players, browsers playing a video, and other apps that
// tell Windows what they play (Global System Media Transport Controls). Must be used from an MTA thread.
class MediaSessions {
public:
    MediaSessions();
    ~MediaSessions();
    MediaSessions(const MediaSessions&) = delete;
    MediaSessions& operator=(const MediaSessions&) = delete;

    // Whether any session says it is playing; false when there is none or Windows cannot tell.
    bool AnyPlaying();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
