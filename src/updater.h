#pragma once

#include <windows.h>

#include <optional>
#include <string>

// Updates from the GitHub releases of the project. Nothing but these requests leaves the PC.
inline constexpr wchar_t kUpdateRepository[] = L"Swiftlyx/OpenChatMix";
// "<exe> --install-update <file> <sha256>": the administrator half of an update into a folder only
// administrators may write to.
inline constexpr wchar_t kInstallUpdateFlag[] = L"--install-update";

struct UpdateRelease {
    std::wstring version;  // Without the leading "v".
    std::wstring pageUrl;
    std::wstring exeUrl;
    std::wstring sumsUrl;
};

struct UpdateCheckResult {
    bool ok = false;                       // GitHub answered; release stays empty when nothing is newer.
    bool manual = false;
    std::optional<UpdateRelease> release;
};

// Asks GitHub for the latest release on a worker thread, then posts `message` to window with an
// UpdateCheckResult* in lParam; the receiver deletes it.
void CheckForUpdate(HWND window, UINT message, bool manual);

// Downloads the release's executable on a worker thread, checks it against the release's
// SHA256SUMS.txt and puts it in place of the running one, which stays as "<exe>.old" until the next
// start. In a folder only administrators may write to (the root of the Windows drive, Program Files),
// Windows asks for permission and this executable, started with kInstallUpdateFlag, puts it in place.
// Posts `message` to window with wParam 1 on success, 0 on failure.
void InstallUpdate(HWND window, UINT message, const UpdateRelease& release);

// The administrator half: reads the file once, checks it against the SHA-256 hash and puts it in place of
// this executable. Returns the process exit code, 0 on success, otherwise a Windows error code.
int RunElevatedInstall(const std::wstring& file, const std::wstring& hash);

// Deletes the executable an earlier update left behind.
void CleanUpAfterUpdate();

// True when version a ("1.2.3", optionally with a leading "v") is newer than b.
bool IsNewerVersion(const std::wstring& a, const std::wstring& b);
