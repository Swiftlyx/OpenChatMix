#include "gg_screen.h"

#include <algorithm>

#include "app.h"
#include "log.h"

namespace {

constexpr DWORD kFastMs = 5;      // Rounded up to the system timer, about 16 ms.
constexpr DWORD kSlowMs = 1000;
constexpr ULONGLONG kNoiseBytes = 64;  // Less is a connection being opened, probed or closed.
// A heartbeat repeats kHeartbeatBeats times at least kHeartbeatMinMs apart, at an interval steady within
// kHeartbeatJitterMs.
constexpr size_t kHeartbeatBeats = 4;
constexpr ULONGLONG kHeartbeatMinMs = 4000;
constexpr ULONGLONG kHeartbeatJitterMs = 60;
constexpr size_t kRequestsKept = 24;
constexpr size_t kHeartbeatSizesKept = 4;
// A helper without a known heartbeat is checked closely for this long, so its heartbeat is learned.
constexpr ULONGLONG kLearnMs = 60000;
constexpr ULONGLONG kEventLogMs = 1000;

ULONGLONG Operations(const IO_COUNTERS& c) {
    return c.ReadOperationCount + c.WriteOperationCount + c.OtherOperationCount;
}

ULONGLONG Bytes(const IO_COUNTERS& c) {
    return c.ReadTransferCount + c.WriteTransferCount + c.OtherTransferCount;
}

}  // namespace

GgScreenWatch::~GgScreenWatch() {
    Stop();
}

void GgScreenWatch::Init(HWND notify, UINT eventMessage, UINT heartbeatMessage, void (*onEvent)()) {
    notify_ = notify;
    eventMessage_ = eventMessage;
    heartbeatMessage_ = heartbeatMessage;
    onEvent_ = onEvent;
}

void GgScreenWatch::SetHeartbeats(const GgHeartbeats& heartbeats) {
    AcquireSRWLockExclusive(&lock_);
    heartbeats_ = heartbeats;
    ReleaseSRWLockExclusive(&lock_);
}

GgHeartbeats GgScreenWatch::Heartbeats() const {
    AcquireSRWLockShared(&lock_);
    GgHeartbeats heartbeats = heartbeats_;
    ReleaseSRWLockShared(&lock_);
    return heartbeats;
}

bool GgScreenWatch::KnowsHeartbeat(const std::wstring& exe) const {
    AcquireSRWLockShared(&lock_);
    const auto known = heartbeats_.find(exe);
    const bool knows = known != heartbeats_.end() && !known->second.empty();
    ReleaseSRWLockShared(&lock_);
    return knows;
}

void GgScreenWatch::Watch(const std::vector<GgHelperProcess>& helpers) {
    if (helpers == watched_) {
        return;
    }
    StopThread();
    watched_ = helpers;
    for (const GgHelperProcess& process : helpers) {
        Helper helper;
        helper.spec = process.spec;
        helper.process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process.pid);
        if (helper.process && GetProcessIoCounters(helper.process, &helper.last)) {
            helpers_.push_back(helper);
        } else if (helper.process) {
            CloseHandle(helper.process);
        }
    }
    if (helpers_.empty()) {
        Log("GG watch: off");
        return;
    }
    stop_ = false;
    wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    thread_ = wake_ ? CreateThread(nullptr, 0, ThreadProc, this, 0, nullptr) : nullptr;
    if (!thread_) {
        Log("GG watch: thread start failed, error %lu", GetLastError());
        StopThread();
        return;
    }
    std::string names;
    for (const Helper& helper : helpers_) {
        names += (names.empty() ? "" : ", ") + Utf8(helper.spec->exe);
    }
    Log("GG watch: %s", names.c_str());
}

void GgScreenWatch::StopThread() {
    if (thread_) {
        stop_ = true;
        SetEvent(wake_);
        WaitForSingleObject(thread_, INFINITE);
        CloseHandle(thread_);
        thread_ = nullptr;
    }
    if (wake_) {
        CloseHandle(wake_);
        wake_ = nullptr;
    }
    for (const Helper& helper : helpers_) {
        CloseHandle(helper.process);
    }
    helpers_.clear();
}

void GgScreenWatch::SetFast(bool fast) {
    if (fast_.exchange(fast) != fast && wake_) {
        SetEvent(wake_);
    }
}

DWORD WINAPI GgScreenWatch::ThreadProc(void* self) {
    static_cast<GgScreenWatch*>(self)->Run();
    return 0;
}

void GgScreenWatch::Run() {
    const ULONGLONG started = GetTickCount64();
    bool wasFast = false;
    while (!stop_) {
        const ULONGLONG now = GetTickCount64();
        bool learning = false;
        for (Helper& helper : helpers_) {
            learning = learning || (now - started < kLearnMs && !KnowsHeartbeat(helper.spec->exe));
            IO_COUNTERS counters{};
            if (!GetProcessIoCounters(helper.process, &counters)) {
                continue;
            }
            if (IsEvent(helper, counters, now, wasFast)) {
                // At most one line a second: a playing track is reported several times a second.
                if (now - helper.logged >= kEventLogMs) {
                    helper.logged = now;
                    Log("GG watch: %s hands GG %llu bytes", Utf8(helper.spec->exe).c_str(),
                        Bytes(counters) - Bytes(helper.last));
                }
                events_[static_cast<int>(helper.spec->app)].store(now);
                if (onEvent_) {
                    onEvent_();
                }
                PostMessageW(notify_, eventMessage_, static_cast<WPARAM>(helper.spec->app), 0);
            }
            helper.last = counters;
        }
        wasFast = fast_ || learning;
        WaitForSingleObject(wake_, wasFast ? kFastMs : kSlowMs);
    }
}

bool GgScreenWatch::IsEvent(const Helper& helper, const IO_COUNTERS& counters, ULONGLONG tick, bool precise) {
    const ULONGLONG operations = Operations(counters) - Operations(helper.last);
    const ULONGLONG bytes = Bytes(counters) - Bytes(helper.last);
    if (operations == 0 || bytes < kNoiseBytes) {
        return false;
    }
    const std::wstring exe = helper.spec->exe;
    AcquireSRWLockShared(&lock_);
    const auto known = heartbeats_.find(exe);
    const std::vector<ULONGLONG> sizes = known == heartbeats_.end() ? std::vector<ULONGLONG>{} : known->second;
    ReleaseSRWLockShared(&lock_);
    // A heartbeat, possibly with a connection's upkeep in the same check.
    if (std::any_of(sizes.begin(), sizes.end(),
                    [&](ULONGLONG size) { return bytes >= size && bytes - size < kNoiseBytes; })) {
        return false;
    }
    if (operations > 1 || !precise) {
        return true;
    }
    std::vector<Request>& requests = requests_[exe];
    requests.push_back({bytes, tick});
    if (requests.size() > kRequestsKept) {
        requests.erase(requests.begin());
    }
    std::vector<ULONGLONG> beats;
    for (const Request& request : requests) {
        if (request.bytes == bytes) {
            beats.push_back(request.tick);
        }
    }
    if (beats.size() < kHeartbeatBeats) {
        return true;
    }
    const ULONGLONG* last = beats.data() + beats.size() - kHeartbeatBeats;
    const ULONGLONG interval = last[1] - last[0];
    bool periodic = interval >= kHeartbeatMinMs;
    for (size_t i = 2; i < kHeartbeatBeats && periodic; ++i) {
        const ULONGLONG gap = last[i] - last[i - 1];
        periodic = (gap > interval ? gap - interval : interval - gap) <= kHeartbeatJitterMs;
    }
    if (!periodic) {
        return true;
    }
    AcquireSRWLockExclusive(&lock_);
    std::vector<ULONGLONG>& learned = heartbeats_[exe];
    learned.push_back(bytes);
    if (learned.size() > kHeartbeatSizesKept) {
        learned.erase(learned.begin());
    }
    ReleaseSRWLockExclusive(&lock_);
    Log("GG watch: %s heartbeat is %llu bytes every %llu ms", Utf8(exe).c_str(), bytes, interval);
    PostMessageW(notify_, heartbeatMessage_, 0, 0);
    return false;
}
