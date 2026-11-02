#include "media_sessions.h"

// windows.h defines GetCurrentTime, which clashes with a WinRT method of that name.
#undef GetCurrentTime

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>

#include <appmodel.h>
#include <shlobj.h>
#include <tlhelp32.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <string>
#include <vector>

#include "app.h"
#include "app_info.h"
#include "log.h"

namespace control = winrt::Windows::Media::Control;

namespace {

constexpr DWORD kCheckMs = 1000;
constexpr ULONGLONG kIdleAfterMs = 3000;  // Checks pause when nobody asked for this long.
constexpr DWORD kStopWaitMs = 1000;
constexpr size_t kMaxProcesses = 8;  // Each process heard takes a capture of its own.

// PKEY_Link_TargetParsingPath, defined locally to avoid pulling initguid.h into this unit.
constexpr PROPERTYKEY kTargetPathKey = {
    {0xb9b4b3fc, 0x2b51, 0x4a42, {0xb5, 0xd8, 0x32, 0x41, 0x46, 0xaf, 0xcf, 0x25}}, 2};

bool Plays(const control::GlobalSystemMediaTransportControlsSession& session) {
    return session.GetPlaybackInfo().PlaybackStatus() ==
           control::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
}

// The file name of the program an app ID starts. A desktop app's ID leads to its Start menu entry, e.g. "Chrome" to
// chrome.exe; an app without one may use its program's name or path as the ID. Empty for a packaged app.
std::wstring ProgramOf(const std::wstring& app) {
    winrt::com_ptr<IShellItem2> item;
    PWSTR target = nullptr;
    std::wstring program;
    if (SUCCEEDED(SHCreateItemInKnownFolder(FOLDERID_AppsFolder, KF_FLAG_DONT_VERIFY, app.c_str(),
                                            __uuidof(IShellItem2), item.put_void())) &&
        SUCCEEDED(item->GetString(kTargetPathKey, &target))) {
        program = FileNameOf(target);
        CoTaskMemFree(target);
    }
    if (program.empty() && !app.empty() && app.find(L'!') == std::wstring::npos) {
        program = FileNameOf(app);
        if (program.size() < 4 || _wcsicmp(program.c_str() + program.size() - 4, L".exe") != 0) {
            program += L".exe";
        }
    }
    return program;
}

bool IsPackagedApp(DWORD pid, const std::wstring& app) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) {
        return false;
    }
    wchar_t id[APPLICATION_USER_MODEL_ID_MAX_LENGTH];
    UINT32 length = APPLICATION_USER_MODEL_ID_MAX_LENGTH;
    const bool match =
        GetApplicationUserModelId(process, &length, id) == ERROR_SUCCESS && _wcsicmp(id, app.c_str()) == 0;
    CloseHandle(process);
    return match;
}

// The roots of the processes that run an app: a process started by another one of the same app, such as a browser's
// helper or the copy a portable app unpacks, belongs to its root's tree. More than one root runs when the app runs
// more than once, or when its program hosts other things too, e.g. powershell.exe.
std::vector<DWORD> FindAppProcesses(const std::wstring& app) {
    struct Entry {
        DWORD pid;
        DWORD parent;
        std::wstring exe;
    };
    std::vector<Entry> entries;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return {};
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry)) {
        entries.push_back({entry.th32ProcessID, entry.th32ParentProcessID, entry.szExeFile});
    }
    CloseHandle(snapshot);
    const std::wstring program = ProgramOf(app);
    std::map<DWORD, DWORD> members;  // Process to parent.
    for (const Entry& process : entries) {
        if (!program.empty() && _wcsicmp(process.exe.c_str(), program.c_str()) == 0) {
            members[process.pid] = process.parent;
        }
    }
    if (members.empty()) {
        for (const Entry& process : entries) {
            if (process.pid && IsPackagedApp(process.pid, app)) {
                members[process.pid] = process.parent;
            }
        }
    }
    std::vector<DWORD> roots;
    for (const auto& [pid, parent] : members) {
        if (!members.count(parent)) {
            roots.push_back(pid);
        }
    }
    return roots;
}

}  // namespace

struct MediaSessions::State {
    std::atomic<bool> playing{false};
    mutable SRWLOCK lock = SRWLOCK_INIT;
    std::vector<DWORD> processes;  // Under lock.
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

std::vector<DWORD> MediaSessions::PlayingProcesses() const {
    AcquireSRWLockShared(&state_->lock);
    std::vector<DWORD> processes = state_->processes;
    ReleaseSRWLockShared(&state_->lock);
    return processes;
}

DWORD WINAPI MediaSessions::CheckLoop(void* parameter) {
    const std::unique_ptr<std::shared_ptr<State>> holder(static_cast<std::shared_ptr<State>*>(parameter));
    State& state = **holder;
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    {
        control::GlobalSystemMediaTransportControlsSessionManager manager{nullptr};
        std::vector<std::wstring> lastApps;  // The apps that played last, sorted.
        std::vector<HANDLE> watched;         // Their published processes, to notice when one exits.
        auto publish = [&](const std::vector<DWORD>& processes) {
            for (const HANDLE process : watched) {
                CloseHandle(process);
            }
            watched.clear();
            std::vector<DWORD> open;
            for (const DWORD pid : processes) {
                if (const HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
                    watched.push_back(process);
                    open.push_back(pid);
                }
            }
            AcquireSRWLockExclusive(&state.lock);
            state.processes = std::move(open);
            ReleaseSRWLockExclusive(&state.lock);
        };
        while (!state.stop) {
            const bool asked = GetTickCount64() - state.lastAsked < kIdleAfterMs;
            // A process that exits drops out, and the apps are looked up again at the next check, as one may have
            // started anew.
            if (std::any_of(watched.begin(), watched.end(),
                            [](HANDLE process) { return WaitForSingleObject(process, 0) == WAIT_OBJECT_0; })) {
                std::vector<DWORD> running;
                for (const HANDLE process : watched) {
                    if (WaitForSingleObject(process, 0) != WAIT_OBJECT_0) {
                        running.push_back(GetProcessId(process));
                    }
                }
                publish(running);
                lastApps.clear();
            }
            if (asked) {
                try {
                    if (!manager) {
                        manager = control::GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
                    }
                    std::vector<std::wstring> apps;
                    for (const auto& session : manager.GetSessions()) {
                        if (Plays(session)) {
                            apps.emplace_back(session.SourceAppUserModelId().c_str());
                        }
                    }
                    std::sort(apps.begin(), apps.end());
                    apps.erase(std::unique(apps.begin(), apps.end()), apps.end());
                    // While none plays, the last ones stay, so the visualizer keeps hearing them as it lingers.
                    if (!apps.empty() && apps != lastApps) {
                        lastApps = apps;
                        std::vector<DWORD> processes;
                        std::string found;
                        for (const std::wstring& app : apps) {
                            const std::vector<DWORD> roots = FindAppProcesses(app);
                            found += (found.empty() ? "" : "; ") + Utf8(app) + (roots.empty() ? " not found" : "");
                            for (const DWORD pid : roots) {
                                if (processes.size() < kMaxProcesses) {
                                    processes.push_back(pid);
                                    found += " " + std::to_string(pid);
                                }
                            }
                        }
                        publish(processes);
                        Log("media: plays: %s", found.c_str());
                    }
                    state.playing = !apps.empty();
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
        for (const HANDLE process : watched) {
            CloseHandle(process);
        }
    }
    winrt::uninit_apartment();
    return 0;
}
