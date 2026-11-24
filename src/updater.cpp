#include "updater.h"

#include <bcrypt.h>
#include <objbase.h>
#include <shellapi.h>
#include <winhttp.h>

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <memory>
#include <thread>
#include <vector>

#include "app.h"
#include "autostart_task.h"
#include "json.h"
#include "log.h"
#include "settings.h"
#include "version.h"

namespace {

constexpr size_t kMaxJsonBytes = 1 << 20;
constexpr size_t kMaxExeBytes = 32 << 20;
constexpr wchar_t kExeAsset[] = L"OpenChatMix.exe";
constexpr wchar_t kSumsAsset[] = L"SHA256SUMS.txt";
// The download handed to the administrator half, in the temp folder.
constexpr wchar_t kStagedFile[] = L"OpenChatMix.exe.download";
// How long the update waits for the administrator half, the consent prompt included.
constexpr DWORD kElevatedInstallMs = 5 * 60 * 1000;

struct InternetHandle {
    HINTERNET handle = nullptr;
    ~InternetHandle() {
        if (handle) {
            WinHttpCloseHandle(handle);
        }
    }
};

// GET over HTTPS; redirects to other HTTPS hosts (release assets) are followed.
std::optional<std::string> HttpGet(const std::wstring& url, const wchar_t* accept, size_t limit) {
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) {
        return std::nullopt;
    }
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.lpszExtraInfo) {
        path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    }

    const std::wstring agent = std::wstring(L"OpenChatMix/") + APP_VERSION_WSTRING;
    InternetHandle session{WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                       WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.handle) {
        return std::nullopt;
    }
    WinHttpSetTimeouts(session.handle, 10000, 10000, 15000, 30000);
    InternetHandle connection{WinHttpConnect(session.handle, host.c_str(), parts.nPort, 0)};
    InternetHandle request{connection.handle ? WinHttpOpenRequest(connection.handle, L"GET", path.c_str(), nullptr,
                                                                  WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                                  WINHTTP_FLAG_SECURE)
                                             : nullptr};
    if (!request.handle) {
        return std::nullopt;
    }
    const std::wstring headers = std::wstring(L"Accept: ") + accept + L"\r\n";
    if (!WinHttpSendRequest(request.handle, headers.c_str(), static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.handle, nullptr)) {
        Log("update: request failed, error %lu", GetLastError());
        return std::nullopt;
    }
    DWORD status = 0;
    DWORD size = sizeof(status);
    WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &size, WINHTTP_NO_HEADER_INDEX);
    if (status != 200) {
        Log("update: %s answered %lu", Utf8(host).c_str(), status);
        return std::nullopt;
    }
    std::string body;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.handle, &available)) {
            return std::nullopt;
        }
        if (available == 0) {
            return body;
        }
        if (body.size() + available > limit) {
            return std::nullopt;
        }
        const size_t offset = body.size();
        body.resize(offset + available);
        DWORD read = 0;
        if (!WinHttpReadData(request.handle, body.data() + offset, available, &read)) {
            return std::nullopt;
        }
        body.resize(offset + read);
    }
}

std::wstring StripV(std::wstring version) {
    if (!version.empty() && (version[0] == L'v' || version[0] == L'V')) {
        version.erase(0, 1);
    }
    return version;
}

std::vector<int> VersionParts(const std::wstring& version) {
    std::vector<int> parts;
    const std::wstring text = StripV(version);
    size_t start = 0;
    while (start < text.size() && std::iswdigit(text[start])) {
        size_t end = start;
        int value = 0;
        while (end < text.size() && std::iswdigit(text[end])) {
            value = value * 10 + (text[end] - L'0');
            ++end;
        }
        parts.push_back(value);
        if (end >= text.size() || text[end] != L'.') {
            break;
        }
        start = end + 1;
    }
    parts.resize(3, 0);
    return parts;
}

std::optional<UpdateRelease> ParseRelease(const std::string& json) {
    const auto root = ParseJson(json);
    if (!root || root->type != JsonValue::Type::Object) {
        return std::nullopt;
    }
    const JsonValue* tag = root->Find(L"tag_name");
    const JsonValue* page = root->Find(L"html_url");
    const JsonValue* assets = root->Find(L"assets");
    if (!tag || tag->string.empty() || !assets || assets->type != JsonValue::Type::Array) {
        return std::nullopt;
    }
    UpdateRelease release;
    release.version = StripV(tag->string);
    release.pageUrl = page ? page->string : L"";
    for (const JsonValue& asset : assets->array) {
        const JsonValue* name = asset.Find(L"name");
        const JsonValue* url = asset.Find(L"browser_download_url");
        if (!name || !url) {
            continue;
        }
        if (_wcsicmp(name->string.c_str(), kExeAsset) == 0) {
            release.exeUrl = url->string;
        } else if (_wcsicmp(name->string.c_str(), kSumsAsset) == 0) {
            release.sumsUrl = url->string;
        }
    }
    return release;
}

std::optional<std::string> Sha256Hex(const std::string& data) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
        return std::nullopt;
    }
    unsigned char hash[32] = {};
    const NTSTATUS status = BCryptHash(algorithm, nullptr, 0,
                                       reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())),
                                       static_cast<ULONG>(data.size()), hash, sizeof(hash));
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!BCRYPT_SUCCESS(status)) {
        return std::nullopt;
    }
    std::string hex;
    constexpr char kDigits[] = "0123456789abcdef";
    for (const unsigned char byte : hash) {
        hex += kDigits[byte >> 4];
        hex += kDigits[byte & 0x0F];
    }
    return hex;
}

// "hash  name" lines as written by sha256sum and package.ps1.
std::optional<std::string> ExpectedHash(const std::string& sums) {
    const std::string asset = Utf8(kExeAsset);
    size_t start = 0;
    while (start < sums.size()) {
        size_t end = sums.find('\n', start);
        if (end == std::string::npos) {
            end = sums.size();
        }
        std::string line = sums.substr(start, end - start);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        const size_t space = line.find(' ');
        if (space == 64 && line.size() > asset.size() &&
            _stricmp(line.c_str() + line.size() - asset.size(), asset.c_str()) == 0) {
            std::string hash = line.substr(0, 64);
            std::transform(hash.begin(), hash.end(), hash.begin(), [](char c) { return static_cast<char>(tolower(c)); });
            return hash;
        }
        start = end + 1;
    }
    return std::nullopt;
}

bool WriteFileAll(const std::wstring& path, const std::string& data) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const bool ok = WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) &&
                    written == data.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (!ok) {
        const DWORD error = GetLastError();
        DeleteFileW(path.c_str());
        SetLastError(error);
    }
    return ok;
}

std::optional<std::string> ReadFileAll(const std::wstring& path, size_t limit) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }
    std::optional<std::string> data;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(file, &size) && size.QuadPart > 0 && static_cast<unsigned long long>(size.QuadPart) <= limit) {
        std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
        DWORD read = 0;
        if (ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) && read == bytes.size()) {
            data = std::move(bytes);
        }
    }
    CloseHandle(file);
    return data;
}

// Puts exe in place of the running executable: written beside it as "<exe>.new", the running one renamed to
// "<exe>.old" (a running executable can be renamed but not overwritten), then the new one moved in. Returns
// ERROR_SUCCESS, or the error of the step that failed once the earlier steps are undone.
DWORD PlaceExecutable(const std::string& exe) {
    const std::wstring path = ExePath();
    const std::wstring fresh = path + L".new";
    const std::wstring old = path + L".old";
    auto lastError = [] {
        const DWORD error = GetLastError();
        return error != ERROR_SUCCESS ? error : ERROR_GEN_FAILURE;
    };
    if (!WriteFileAll(fresh, exe)) {
        const DWORD error = lastError();
        Log("update: cannot write next to the executable, error %lu", error);
        return error;
    }
    if (!MoveFileExW(path.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        const DWORD error = lastError();
        Log("update: cannot move the running executable, error %lu", error);
        DeleteFileW(fresh.c_str());
        return error;
    }
    if (!MoveFileExW(fresh.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        const DWORD error = lastError();
        Log("update: cannot place the new executable, error %lu", error);
        MoveFileExW(old.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
        DeleteFileW(fresh.c_str());
        return error;
    }
    return ERROR_SUCCESS;
}

// A folder only administrators may write to: the checked download goes to the temp folder, and this
// executable, started as administrator once the user agrees, checks it against the same hash and puts it in
// place. The hash travels on the command line, so the file it checks is the one it writes.
bool InstallElevated(HWND window, const std::string& exe, const std::string& hash) {
    wchar_t temp[MAX_PATH + 1] = {};
    const DWORD length = GetTempPathW(ARRAYSIZE(temp), temp);
    if (length == 0 || length > MAX_PATH) {
        Log("update: no temp folder, error %lu", GetLastError());
        return false;
    }
    const std::wstring staged = std::wstring(temp) + kStagedFile;
    if (!WriteFileAll(staged, exe)) {
        Log("update: cannot write to the temp folder, error %lu", GetLastError());
        return false;
    }
    const std::wstring path = ExePath();
    const std::wstring parameters =
        std::wstring(kInstallUpdateFlag) + L" \"" + staged + L"\" " + std::wstring(hash.begin(), hash.end());
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    info.hwnd = window;
    info.lpVerb = L"runas";
    info.lpFile = path.c_str();
    info.lpParameters = parameters.c_str();
    info.nShow = SW_HIDE;
    Log("update: the folder needs administrator rights");
    // ShellExecuteEx may hand the request to shell extensions, which need COM on this thread.
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool started = ShellExecuteExW(&info) && info.hProcess;
    const DWORD startError = GetLastError();
    if (SUCCEEDED(com)) {
        CoUninitialize();
    }
    if (!started) {
        if (startError == ERROR_CANCELLED) {
            Log("update: administrator rights declined");
        } else {
            Log("update: cannot start as administrator, error %lu", startError);
        }
        DeleteFileW(staged.c_str());
        return false;
    }
    DWORD code = ERROR_TIMEOUT;
    const bool finished = WaitForSingleObject(info.hProcess, kElevatedInstallMs) == WAIT_OBJECT_0;
    if (finished) {
        GetExitCodeProcess(info.hProcess, &code);
        DeleteFileW(staged.c_str());
    }
    CloseHandle(info.hProcess);
    if (code != ERROR_SUCCESS) {
        Log("update: the administrator install failed, error %lu", code);
        return false;
    }
    return true;
}

bool Install(HWND window, const UpdateRelease& release) {
    if (release.exeUrl.empty() || release.sumsUrl.empty()) {
        Log("update: release %s has no executable or checksums", Utf8(release.version).c_str());
        return false;
    }
    const auto sums = HttpGet(release.sumsUrl, L"application/octet-stream", kMaxJsonBytes);
    const auto expected = sums ? ExpectedHash(*sums) : std::nullopt;
    if (!expected) {
        Log("update: no checksum for %s", Utf8(kExeAsset).c_str());
        return false;
    }
    const auto exe = HttpGet(release.exeUrl, L"application/octet-stream", kMaxExeBytes);
    const auto actual = exe ? Sha256Hex(*exe) : std::nullopt;
    if (!actual || *actual != *expected) {
        Log("update: download missing or checksum mismatch");
        return false;
    }
    const DWORD placed = PlaceExecutable(*exe);
    // Elevation does not help an administrator who still may not write there.
    if (placed != ERROR_SUCCESS && (placed != ERROR_ACCESS_DENIED || IsElevated() ||
                                    !InstallElevated(window, *exe, *expected))) {
        return false;
    }
    Log("update: %s installed", Utf8(release.version).c_str());
    return true;
}

}  // namespace

bool IsNewerVersion(const std::wstring& a, const std::wstring& b) {
    return VersionParts(a) > VersionParts(b);
}

void CheckForUpdate(HWND window, UINT message, bool manual) {
    std::thread([window, message, manual] {
        auto result = std::make_unique<UpdateCheckResult>();
        result->manual = manual;
        const std::wstring url = std::wstring(L"https://api.github.com/repos/") + kUpdateRepository + L"/releases/latest";
        if (const auto body = HttpGet(url, L"application/vnd.github+json", kMaxJsonBytes)) {
            if (auto release = ParseRelease(*body)) {
                result->ok = true;
                if (IsNewerVersion(release->version, APP_VERSION_WSTRING) && !release->exeUrl.empty()) {
                    Log("update: %s available", Utf8(release->version).c_str());
                    result->release = std::move(release);
                }
            }
        }
        if (PostMessageW(window, message, 0, reinterpret_cast<LPARAM>(result.get()))) {
            result.release();
        }
    }).detach();
}

void InstallUpdate(HWND window, UINT message, const UpdateRelease& release) {
    std::thread([window, message, release] {
        PostMessageW(window, message, Install(window, release) ? 1 : 0, 0);
    }).detach();
}

int RunElevatedInstall(const std::wstring& file, const std::wstring& hash) {
    const auto data = ReadFileAll(file, kMaxExeBytes);
    if (!data) {
        return ERROR_READ_FAULT;
    }
    std::string expected = Utf8(hash);
    std::transform(expected.begin(), expected.end(), expected.begin(),
                   [](char c) { return static_cast<char>(tolower(static_cast<unsigned char>(c))); });
    const auto actual = Sha256Hex(*data);
    if (!actual || expected.size() != 64 || *actual != expected) {
        return ERROR_INVALID_DATA;
    }
    if (const DWORD placed = PlaceExecutable(*data); placed != ERROR_SUCCESS) {
        return static_cast<int>(placed);
    }
    // Only administrators may delete the old executable there, and the updated app runs without those rights:
    // Windows deletes it on the next restart instead.
    MoveFileExW((ExePath() + L".old").c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    return 0;
}

void CleanUpAfterUpdate() {
    DeleteFileW((ExePath() + L".old").c_str());
    DeleteFileW((ExePath() + L".new").c_str());
}
