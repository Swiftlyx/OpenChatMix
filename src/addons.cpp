#include "addons.h"

#include <algorithm>
#include <fstream>
#include <iterator>

#include "app.h"
#include "app_info.h"
#include "log.h"

namespace {

constexpr int kMaxRestarts = 3;
constexpr ULONGLONG kStableMs = 10 * 60 * 1000;  // An add-on that ran this long gets its restarts back.
constexpr DWORD kEndTimeoutMs = 2000;

std::wstring CorePropsPath() {
    wchar_t data[MAX_PATH] = {};
    const DWORD length = GetEnvironmentVariableW(L"ProgramData", data, MAX_PATH);
    const std::wstring root = length && length < MAX_PATH ? data : L"C:\\ProgramData";
    return root + L"\\SteelSeries\\SteelSeries Engine 3\\coreProps.json";
}

bool ReadFileBytes(const std::wstring& path, std::string& bytes) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

bool WriteFileBytes(const std::wstring& path, const std::string& bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(file);
}

bool IsFile(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

}  // namespace

AddOnHost::~AddOnHost() {
    Stop();
}

void AddOnHost::Update(bool run, const std::vector<AddOnSettings>& addOns, HWND notify, UINT message) {
    std::vector<std::wstring> wanted;
    if (run) {
        for (const AddOnSettings& addOn : addOns) {
            if (addOn.enabled && IsFile(addOn.path)) {
                wanted.push_back(addOn.path);
            }
        }
    }
    std::erase_if(processes_, [&](Process& process) {
        const bool keep = std::any_of(wanted.begin(), wanted.end(), [&](const std::wstring& path) {
            return _wcsicmp(path.c_str(), process.path.c_str()) == 0;
        });
        if (!keep) {
            End(process);
        }
        return !keep;
    });
    if (wanted.empty()) {
        Stop();
        return;
    }
    if (!server_.IsRunning() && !server_.Start(notify, message)) {
        Stop();
        return;
    }
    // The address goes in before the add-ons start, as they read it once at their start.
    WriteCoreProps(server_.Port());
    if (!job_) {
        job_ = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (job_ && !SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
            CloseHandle(job_);
            job_ = nullptr;
        }
    }
    for (const std::wstring& path : wanted) {
        const bool known = std::any_of(processes_.begin(), processes_.end(), [&](const Process& process) {
            return _wcsicmp(path.c_str(), process.path.c_str()) == 0;
        });
        if (!known) {
            Process process;
            process.path = path;
            if (!Launch(process)) {
                process.restarts = kMaxRestarts;
            }
            processes_.push_back(std::move(process));
        }
    }
}

void AddOnHost::Stop() {
    for (Process& process : processes_) {
        End(process);
    }
    processes_.clear();
    server_.Stop();
    RestoreCoreProps();
    if (job_) {
        CloseHandle(job_);
        job_ = nullptr;
    }
}

bool AddOnHost::CheckProcesses() {
    bool changed = false;
    const ULONGLONG now = GetTickCount64();
    for (Process& process : processes_) {
        if (!process.handle || WaitForSingleObject(process.handle, 0) != WAIT_OBJECT_0) {
            continue;
        }
        DWORD code = 0;
        GetExitCodeProcess(process.handle, &code);
        CloseHandle(process.handle);
        process.handle = nullptr;
        changed = true;
        if (now - process.started >= kStableMs) {
            process.restarts = 0;
        }
        Log("add-on exited: %s, code %lu", Utf8(FileNameOf(process.path)).c_str(), code);
        if (process.restarts < kMaxRestarts) {
            ++process.restarts;
            Launch(process);
        }
    }
    return changed;
}

bool AddOnHost::IsRunning(const std::wstring& path) const {
    return std::any_of(processes_.begin(), processes_.end(), [&](const Process& process) {
        return process.handle && _wcsicmp(path.c_str(), process.path.c_str()) == 0 &&
               WaitForSingleObject(process.handle, 0) == WAIT_TIMEOUT;
    });
}

bool AddOnHost::Launch(Process& process) {
    const size_t slash = process.path.find_last_of(L"\\/");
    const std::wstring folder = slash == std::wstring::npos ? L"." : process.path.substr(0, slash);
    std::wstring command = L"\"" + process.path + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION info{};
    // Suspended until it is in the job, so not even its first moments run outside it.
    if (!CreateProcessW(process.path.c_str(), command.data(), nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, folder.c_str(), &startup, &info)) {
        Log("add-on could not start: %s, error %lu", Utf8(FileNameOf(process.path)).c_str(), GetLastError());
        return false;
    }
    if (job_) {
        AssignProcessToJobObject(job_, info.hProcess);
    }
    ResumeThread(info.hThread);
    CloseHandle(info.hThread);
    process.handle = info.hProcess;
    process.started = GetTickCount64();
    Log("add-on started: %s", Utf8(FileNameOf(process.path)).c_str());
    return true;
}

void AddOnHost::End(Process& process) {
    if (!process.handle) {
        return;
    }
    TerminateProcess(process.handle, 0);
    WaitForSingleObject(process.handle, kEndTimeoutMs);
    CloseHandle(process.handle);
    process.handle = nullptr;
    Log("add-on ended: %s", Utf8(FileNameOf(process.path)).c_str());
}

bool AddOnHost::WriteCoreProps(int port) {
    const std::string content = "{\"address\":\"127.0.0.1:" + std::to_string(port) + "\"}";
    if (content == propsWritten_) {
        return true;
    }
    if (propsWritten_.empty()) {
        propsPath_ = CorePropsPath();
        propsExisted_ = ReadFileBytes(propsPath_, propsBefore_);
        const std::wstring folder = propsPath_.substr(0, propsPath_.find_last_of(L'\\'));
        CreateDirectoryW(folder.substr(0, folder.find_last_of(L'\\')).c_str(), nullptr);
        CreateDirectoryW(folder.c_str(), nullptr);
    }
    if (!WriteFileBytes(propsPath_, content)) {
        Log("GameSense: coreProps.json could not be written");
        return false;
    }
    propsWritten_ = content;
    return true;
}

void AddOnHost::RestoreCoreProps() {
    if (propsWritten_.empty()) {
        return;
    }
    // SteelSeries GG writes its own address when it starts; that one stays.
    std::string current;
    if (ReadFileBytes(propsPath_, current) && current == propsWritten_) {
        if (propsExisted_) {
            WriteFileBytes(propsPath_, propsBefore_);
        } else {
            DeleteFileW(propsPath_.c_str());
        }
    }
    propsWritten_.clear();
}
