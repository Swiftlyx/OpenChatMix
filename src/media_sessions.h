#pragma once

#include <windows.h>

#include <memory>

// The media sessions Windows lists in its media controls: players, browsers playing a video, and other apps that
// tell Windows what they play (Global System Media Transport Controls). Asking crosses into other processes and
// stalls while one of them hangs, so the checks run on a thread of their own and AnyPlaying() never waits.
class MediaSessions {
public:
    MediaSessions();
    ~MediaSessions();
    MediaSessions(const MediaSessions&) = delete;
    MediaSessions& operator=(const MediaSessions&) = delete;

    // Whether any session played at the last check: false at first, when none plays or when Windows cannot tell.
    // Checks run about once a second while this keeps being asked, and pause when it is not.
    bool AnyPlaying();

private:
    struct State;
    static DWORD WINAPI CheckLoop(void* parameter);

    std::shared_ptr<State> state_;
    HANDLE thread_ = nullptr;
};
