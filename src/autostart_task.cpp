#include "autostart_task.h"

#include <windows.h>
#include <sddl.h>
#include <taskschd.h>
#include <wrl/client.h>

#include <string>
#include <vector>

#include "log.h"
#include "settings.h"

using Microsoft::WRL::ComPtr;

namespace {

// A BSTR owned for the length of one call.
class Bstr {
public:
    explicit Bstr(const std::wstring& text) : value_(SysAllocStringLen(text.data(), static_cast<UINT>(text.size()))) {}
    ~Bstr() { SysFreeString(value_); }
    Bstr(const Bstr&) = delete;
    Bstr& operator=(const Bstr&) = delete;
    operator BSTR() const { return value_; }

private:
    BSTR value_;
};

std::wstring UserSid() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return {};
    }
    std::wstring sid;
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<BYTE> buffer(size);
    wchar_t* text = nullptr;
    if (size > 0 && GetTokenInformation(token, TokenUser, buffer.data(), size, &size) &&
        ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &text)) {
        sid = text;
        LocalFree(text);
    }
    CloseHandle(token);
    return sid;
}

// One task per user, since each user signs in on their own. User names contain no character a task name forbids.
std::wstring TaskName() {
    wchar_t user[257] = {};
    DWORD size = ARRAYSIZE(user);
    if (!GetUserNameW(user, &size)) {
        return L"OpenChatMix";
    }
    return std::wstring(L"OpenChatMix (") + user + L")";
}

std::wstring XmlEscape(const std::wstring& text) {
    std::wstring escaped;
    for (const wchar_t ch : text) {
        switch (ch) {
            case L'&': escaped += L"&amp;"; break;
            case L'<': escaped += L"&lt;"; break;
            case L'>': escaped += L"&gt;"; break;
            case L'"': escaped += L"&quot;"; break;
            default: escaped += ch; break;
        }
    }
    return escaped;
}

std::wstring TaskXml(const std::wstring& sid, const std::wstring& exe) {
    const size_t slash = exe.find_last_of(L'\\');
    const std::wstring folder = slash == std::wstring::npos ? std::wstring() : exe.substr(0, slash);
    // No time limit (the default ends a task after 3 days), on batteries too, and at normal priority (the
    // default, 7, runs the program below normal).
    return L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>"
           L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">"
           L"<RegistrationInfo><Author>OpenChatMix</Author>"
           L"<Description>Starts OpenChatMix with administrator rights when you sign in.</Description>"
           L"</RegistrationInfo>"
           L"<Triggers><LogonTrigger><Enabled>true</Enabled><UserId>" + sid + L"</UserId></LogonTrigger></Triggers>"
           L"<Principals><Principal id=\"Author\"><UserId>" + sid + L"</UserId>"
           L"<LogonType>InteractiveToken</LogonType><RunLevel>HighestAvailable</RunLevel></Principal></Principals>"
           L"<Settings><MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>"
           L"<DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>"
           L"<StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>"
           L"<ExecutionTimeLimit>PT0S</ExecutionTimeLimit><Priority>4</Priority></Settings>"
           L"<Actions Context=\"Author\"><Exec><Command>\"" + XmlEscape(exe) + L"\"</Command>"
           L"<WorkingDirectory>" + XmlEscape(folder) + L"</WorkingDirectory></Exec></Actions></Task>";
}

// Runs use on the root folder of the Task Scheduler; false if the service cannot be reached.
template <typename Use>
bool WithRootFolder(Use&& use) {
    // The task service is an in-process server, so the caller's apartment works as it is.
    const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool reached = false;
    {
        VARIANT none;
        VariantInit(&none);
        ComPtr<ITaskService> service;
        ComPtr<ITaskFolder> folder;
        if (SUCCEEDED(CoCreateInstance(__uuidof(TaskScheduler), nullptr, CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(&service))) &&
            SUCCEEDED(service->Connect(none, none, none, none)) &&
            SUCCEEDED(service->GetFolder(Bstr(L"\\"), &folder))) {
            use(folder.Get());
            reached = true;
        }
    }
    if (SUCCEEDED(init)) {
        CoUninitialize();
    }
    return reached;
}

}  // namespace

bool IsElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }
    TOKEN_ELEVATION elevation{};
    DWORD size = sizeof(elevation);
    const bool elevated =
        GetTokenInformation(token, TokenElevation, &elevation, size, &size) && elevation.TokenIsElevated != 0;
    CloseHandle(token);
    return elevated;
}

bool AutostartTaskExists() {
    bool exists = false;
    WithRootFolder([&](ITaskFolder* folder) {
        ComPtr<IRegisteredTask> task;
        exists = SUCCEEDED(folder->GetTask(Bstr(TaskName()), &task));
    });
    return exists;
}

bool SetAutostartTask(bool enable) {
    HRESULT result = E_FAIL;
    WithRootFolder([&](ITaskFolder* folder) {
        if (!enable) {
            result = folder->DeleteTask(Bstr(TaskName()), 0);
            if (result == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) {
                result = S_OK;
            }
            return;
        }
        const std::wstring sid = UserSid();
        if (sid.empty()) {
            return;
        }
        VARIANT none;
        VariantInit(&none);
        ComPtr<IRegisteredTask> task;
        result = folder->RegisterTask(Bstr(TaskName()), Bstr(TaskXml(sid, ExePath())), TASK_CREATE_OR_UPDATE, none,
                                      none, TASK_LOGON_INTERACTIVE_TOKEN, none, &task);
    });
    if (FAILED(result)) {
        Log("autostart: %s the sign-in task failed, 0x%08lx", enable ? "registering" : "removing",
            static_cast<unsigned long>(result));
    }
    return SUCCEEDED(result);
}
