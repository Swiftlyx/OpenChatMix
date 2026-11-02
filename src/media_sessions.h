#pragma once

#include <windows.h>

#include <memory>
#include <vector>

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
    // The processes of the apps that play, or that played last while none plays: for every session that plays,
    // the roots of its app's processes, e.g. a browser's main process, whose helpers play the sound. An app whose
    // process is not found adds none; a process that exits drops out. Set by the same checks, before AnyPlaying
    // turns true.
    std::vector<DWORD> PlayingProcesses() const;

private:
    struct State;
    static DWORD WINAPI CheckLoop(void* parameter);

    std::shared_ptr<State> state_;
    HANDLE thread_ = nullptr;
};
