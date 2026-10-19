#pragma once

#include <windows.h>

#include <array>
#include <atomic>
#include <map>
#include <string>
#include <vector>

// SteelSeries GG's Engine apps that draw on the headset display. GG runs an app's helper process only while
// the app is switched on, and the helper hands every message, track or frame to GG over GG's local GameSense
// connection, so its I/O shows the moment GG gets something to show.
enum class GgScreenApp { Discord, Track, Visualizer };
inline constexpr int kGgScreenAppCount = 3;

struct GgScreenHelper {
    const wchar_t* exe;
    GgScreenApp app;
    const wchar_t* name;  // As GG names the app.
};

inline constexpr GgScreenHelper kGgScreenHelpers[] = {
    {L"gamesense-discord-x64.exe", GgScreenApp::Discord, L"Discord"},
    {L"now-playing.exe", GgScreenApp::Track, L"Now Playing"},
    {L"gamesense-spotify-x64.exe", GgScreenApp::Track, L"Spotify"},
    {L"AudioSync.exe", GgScreenApp::Visualizer, L"Audio Visualizer"},
};

// Heartbeat sizes per helper executable. One helper can have several, e.g. with another GameSense server.
using GgHeartbeats = std::map<std::wstring, std::vector<ULONGLONG>>;

// A running helper process.
struct GgHelperProcess {
    DWORD pid = 0;
    const GgScreenHelper* spec = nullptr;

    bool operator==(const GgHelperProcess&) const = default;
};

// Watches the I/O of GG's running helpers from a thread of its own, which exists only while there is something
// to watch. Whatever a helper sends besides the heartbeat that keeps its app alive in GG counts as something GG
// is about to show. The heartbeat is learned as the same lone request four times at an interval steady to a few
// hundredths of a second, a rhythm a timer keeps and people do not.
class GgScreenWatch {
public:
    GgScreenWatch() = default;
    ~GgScreenWatch();
    GgScreenWatch(const GgScreenWatch&) = delete;
    GgScreenWatch& operator=(const GgScreenWatch&) = delete;

    // onEvent runs on the watch thread as soon as a helper hands GG something; then notify gets eventMessage
    // with the GgScreenApp as wParam. heartbeatMessage follows a newly learned heartbeat.
    void Init(HWND notify, UINT eventMessage, UINT heartbeatMessage, void (*onEvent)());
    // Heartbeat sizes learned earlier, by helper executable, and those known now.
    void SetHeartbeats(const GgHeartbeats& heartbeats);
    GgHeartbeats Heartbeats() const;
    // Watches these helpers from now on; with none, the thread stops and nothing is checked.
    void Watch(const std::vector<GgHelperProcess>& helpers);
    void Stop() { Watch({}); }
    // Fast: checks every few milliseconds, e.g. while the screensaver plays; otherwise about once a second.
    void SetFast(bool fast);
    // Tick of the last thing a helper of the app handed GG; 0 for none yet.
    ULONGLONG LastEvent(GgScreenApp app) const { return events_[static_cast<int>(app)].load(); }

private:
    struct Helper {
        HANDLE process = nullptr;
        const GgScreenHelper* spec = nullptr;
        IO_COUNTERS last{};
        ULONGLONG logged = 0;  // Tick of the last event written to the log.
    };
    struct Request {
        ULONGLONG bytes;
        ULONGLONG tick;
    };

    static DWORD WINAPI ThreadProc(void* self);
    void Run();
    void StopThread();
    // precise: the check before this one was a few milliseconds ago, so tick is exact enough to learn from.
    bool IsEvent(const Helper& helper, const IO_COUNTERS& counters, ULONGLONG tick, bool precise);
    bool KnowsHeartbeat(const std::wstring& exe) const;

    HWND notify_ = nullptr;
    UINT eventMessage_ = 0;
    UINT heartbeatMessage_ = 0;
    void (*onEvent_)() = nullptr;
    std::vector<GgHelperProcess> watched_;
    HANDLE thread_ = nullptr;
    HANDLE wake_ = nullptr;  // Auto-reset: stop or a change of pace.
    std::atomic<bool> stop_{false};
    std::atomic<bool> fast_{false};
    std::array<std::atomic<ULONGLONG>, kGgScreenAppCount> events_{};

    mutable SRWLOCK lock_ = SRWLOCK_INIT;
    GgHeartbeats heartbeats_;  // Under lock_.
    // Used by the watch thread while it runs, and kept for the next one.
    std::vector<Helper> helpers_;
    std::map<std::wstring, std::vector<Request>> requests_;  // Recent lone requests per helper executable.
};
