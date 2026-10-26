#include "settings_transfer.h"

#include <windows.h>

#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr const wchar_t* kIniName = L"OpenChatMix.ini";
constexpr const wchar_t* kFolders[] = {L"Presets", L"PluginStates"};
constexpr LONGLONG kMaxFileBytes = 64ll << 20;  // Settings take a few hundred kilobytes at most.

// ZIP records (APPNOTE 6.3): stored entries only, names in UTF-8.
constexpr uint32_t kLocalSignature = 0x04034b50;
constexpr uint32_t kCentralSignature = 0x02014b50;
constexpr uint32_t kEndSignature = 0x06054b50;
constexpr size_t kLocalSize = 30;
constexpr size_t kCentralSize = 46;
constexpr size_t kEndSize = 22;
constexpr uint16_t kVersion = 20;
constexpr uint16_t kFlagEncrypted = 0x0001;
constexpr uint16_t kFlagUtf8 = 0x0800;

struct Entry {
    std::wstring name;  // Relative to the settings folder, with forward slashes.
    std::vector<uint8_t> data;
};

uint32_t Crc32(const std::vector<uint8_t>& data) {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> result{};
        uint32_t index = 0;
        for (uint32_t& entry : result) {
            uint32_t c = index++;
            for (int bit = 0; bit < 8; ++bit) {
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            entry = c;
        }
        return result;
    }();
    uint32_t crc = 0xFFFFFFFFu;
    for (const uint8_t byte : data) {
        crc = table[(crc ^ byte) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

std::string Utf8(const std::wstring& text) {
    const int length =
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(std::max(0, length)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length, nullptr, nullptr);
    return result;
}

std::wstring Widen(const std::string& text, UINT codePage) {
    const int length = MultiByteToWideChar(codePage, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(static_cast<size_t>(std::max(0, length)), L'\0');
    MultiByteToWideChar(codePage, 0, text.data(), static_cast<int>(text.size()), result.data(), length);
    return result;
}

std::optional<std::vector<uint8_t>> ReadAll(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }
    std::optional<std::vector<uint8_t>> result;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(file, &size) && size.QuadPart <= kMaxFileBytes) {
        std::vector<uint8_t> data(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        if (data.empty() ||
            (ReadFile(file, data.data(), static_cast<DWORD>(data.size()), &read, nullptr) && read == data.size())) {
            result = std::move(data);
        }
    }
    CloseHandle(file);
    return result;
}

bool WriteAll(const std::wstring& path, const std::vector<uint8_t>& data) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const bool ok = data.empty() || (WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) &&
                                     written == data.size());
    CloseHandle(file);
    return ok;
}

// Files of a folder and its subfolders as (name in the archive, path on disk). Links are skipped: they
// lead outside the settings.
void CollectFolder(const std::wstring& directory, const std::wstring& prefix,
                   std::vector<std::pair<std::wstring, std::wstring>>& files) {
    WIN32_FIND_DATAW found{};
    HANDLE find = FindFirstFileW((directory + L"\\*").c_str(), &found);
    if (find == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        const std::wstring name = found.cFileName;
        if (name == L"." || name == L".." || (found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            continue;
        }
        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            CollectFolder(directory + L"\\" + name, prefix + name + L"/", files);
        } else {
            files.emplace_back(prefix + name, directory + L"\\" + name);
        }
    } while (FindNextFileW(find, &found));
    FindClose(find);
}

void Put16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>(value >> 8));
}

void Put32(std::vector<uint8_t>& out, uint32_t value) {
    Put16(out, static_cast<uint16_t>(value & 0xFFFF));
    Put16(out, static_cast<uint16_t>(value >> 16));
}

uint16_t Get16(const std::vector<uint8_t>& in, size_t at) {
    return static_cast<uint16_t>(in[at] | (in[at + 1] << 8));
}

uint32_t Get32(const std::vector<uint8_t>& in, size_t at) {
    return Get16(in, at) | (static_cast<uint32_t>(Get16(in, at + 2)) << 16);
}

std::vector<uint8_t> BuildArchive(const std::vector<Entry>& entries) {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    const auto time = static_cast<uint16_t>((now.wHour << 11) | (now.wMinute << 5) | (now.wSecond / 2));
    const auto date = static_cast<uint16_t>(((now.wYear - 1980) << 9) | (now.wMonth << 5) | now.wDay);
    std::vector<uint8_t> out;
    std::vector<uint8_t> central;
    for (const Entry& entry : entries) {
        const std::string name = Utf8(entry.name);
        const auto nameLength = static_cast<uint16_t>(name.size());
        const uint32_t crc = Crc32(entry.data);
        const auto size = static_cast<uint32_t>(entry.data.size());
        const auto offset = static_cast<uint32_t>(out.size());

        Put32(out, kLocalSignature);
        Put16(out, kVersion);
        Put16(out, kFlagUtf8);
        Put16(out, 0);  // Stored.
        Put16(out, time);
        Put16(out, date);
        Put32(out, crc);
        Put32(out, size);
        Put32(out, size);
        Put16(out, nameLength);
        Put16(out, 0);  // Extra field.
        out.insert(out.end(), name.begin(), name.end());
        out.insert(out.end(), entry.data.begin(), entry.data.end());

        Put32(central, kCentralSignature);
        Put16(central, kVersion);  // Made by: MS-DOS file attributes.
        Put16(central, kVersion);
        Put16(central, kFlagUtf8);
        Put16(central, 0);
        Put16(central, time);
        Put16(central, date);
        Put32(central, crc);
        Put32(central, size);
        Put32(central, size);
        Put16(central, nameLength);
        Put16(central, 0);  // Extra field.
        Put16(central, 0);  // Comment.
        Put16(central, 0);  // Disk.
        Put16(central, 0);  // Internal attributes.
        Put32(central, 0);  // External attributes.
        Put32(central, offset);
        central.insert(central.end(), name.begin(), name.end());
    }
    const auto centralOffset = static_cast<uint32_t>(out.size());
    out.insert(out.end(), central.begin(), central.end());
    const auto count = static_cast<uint16_t>(entries.size());
    Put32(out, kEndSignature);
    Put16(out, 0);
    Put16(out, 0);
    Put16(out, count);
    Put16(out, count);
    Put32(out, static_cast<uint32_t>(central.size()));
    Put32(out, centralOffset);
    Put16(out, 0);  // Comment.
    return out;
}

// The files of an archive; nothing if it is damaged or holds entries other than stored ones.
std::optional<std::vector<Entry>> ParseArchive(const std::vector<uint8_t>& in) {
    if (in.size() < kEndSize) {
        return std::nullopt;
    }
    // The end record comes last, followed only by a comment of up to 64 KB.
    size_t end = in.size() - kEndSize;
    const size_t lowest = end > 0xFFFF ? end - 0xFFFF : 0;
    while (Get32(in, end) != kEndSignature) {
        if (end == lowest) {
            return std::nullopt;
        }
        --end;
    }
    const size_t count = Get16(in, end + 10);
    size_t at = Get32(in, end + 16);
    std::vector<Entry> entries;
    for (size_t i = 0; i < count; ++i) {
        if (at + kCentralSize > in.size() || Get32(in, at) != kCentralSignature) {
            return std::nullopt;
        }
        const uint16_t flags = Get16(in, at + 8);
        const uint16_t method = Get16(in, at + 10);
        const uint32_t crc = Get32(in, at + 16);
        const uint32_t packed = Get32(in, at + 20);
        const uint32_t size = Get32(in, at + 24);
        const size_t nameLength = Get16(in, at + 28);
        const size_t skip = static_cast<size_t>(Get16(in, at + 30)) + Get16(in, at + 32);
        const size_t local = Get32(in, at + 42);
        if (at + kCentralSize + nameLength > in.size()) {
            return std::nullopt;
        }
        const std::string rawName(reinterpret_cast<const char*>(in.data() + at + kCentralSize), nameLength);
        at += kCentralSize + nameLength + skip;
        Entry entry;
        entry.name = Widen(rawName, (flags & kFlagUtf8) ? CP_UTF8 : CP_OEMCP);
        if (!entry.name.empty() && (entry.name.back() == L'/' || entry.name.back() == L'\\')) {
            continue;  // A folder.
        }
        if (method != 0 || packed != size || (flags & kFlagEncrypted)) {
            return std::nullopt;
        }
        if (local + kLocalSize > in.size() || Get32(in, local) != kLocalSignature) {
            return std::nullopt;
        }
        const size_t data = local + kLocalSize + Get16(in, local + 26) + Get16(in, local + 28);
        if (data + size > in.size()) {
            return std::nullopt;
        }
        entry.data.assign(in.data() + data, in.data() + data + size);
        if (Crc32(entry.data) != crc) {
            return std::nullopt;
        }
        entries.push_back(std::move(entry));
    }
    return entries;
}

enum class Place { Ini, Folder, Other, Unsafe };

// Where an entry belongs; name gets forward slashes. Inside the settings folders every part of the path
// must be a plain name: no "." or "..", no empty parts, drive letters, streams or wildcards.
Place PlaceOf(std::wstring& name) {
    std::replace(name.begin(), name.end(), L'\\', L'/');
    if (_wcsicmp(name.c_str(), kIniName) == 0) {
        return Place::Ini;
    }
    for (const wchar_t* folder : kFolders) {
        const std::wstring prefix = std::wstring(folder) + L"/";
        if (name.size() <= prefix.size() || _wcsnicmp(name.c_str(), prefix.c_str(), prefix.size()) != 0) {
            continue;
        }
        for (size_t start = 0; start <= name.size();) {
            const size_t slash = std::min(name.find(L'/', start), name.size());
            const std::wstring_view part(name.data() + start, slash - start);
            const bool control = std::any_of(part.begin(), part.end(), [](wchar_t c) { return c < 32; });
            if (part.empty() || part == L"." || part == L".." || control ||
                part.find_first_of(L":*?\"<>|") != std::wstring_view::npos) {
                return Place::Unsafe;
            }
            start = slash + 1;
        }
        return Place::Folder;
    }
    return Place::Other;
}

// The ini is UTF-16 with a byte order mark, as OpenChatMix writes it, or 8-bit text.
bool LooksLikeSettings(const std::vector<uint8_t>& ini) {
    std::wstring text;
    if (ini.size() >= 2 && ini[0] == 0xFF && ini[1] == 0xFE) {
        text.resize((ini.size() - 2) / sizeof(wchar_t));
        std::memcpy(text.data(), ini.data() + 2, text.size() * sizeof(wchar_t));
    } else {
        text = Widen(std::string(ini.begin(), ini.end()), CP_UTF8);
    }
    return text.find(L"[ChatMix]") != std::wstring::npos;
}

// The settings files of an archive; files outside the settings are left out.
std::optional<std::vector<Entry>> ReadSettingsArchive(const std::wstring& path) {
    const std::optional<std::vector<uint8_t>> bytes = ReadAll(path);
    std::optional<std::vector<Entry>> entries = bytes ? ParseArchive(*bytes) : std::nullopt;
    if (!entries) {
        return std::nullopt;
    }
    std::vector<Entry> settings;
    bool ini = false;
    for (Entry& entry : *entries) {
        switch (PlaceOf(entry.name)) {
        case Place::Ini:
            if (ini || !LooksLikeSettings(entry.data)) {
                return std::nullopt;
            }
            ini = true;
            entry.name = kIniName;
            settings.push_back(std::move(entry));
            break;
        case Place::Folder:
            settings.push_back(std::move(entry));
            break;
        case Place::Other:
            break;
        case Place::Unsafe:
            return std::nullopt;
        }
    }
    if (!ini) {
        return std::nullopt;
    }
    return settings;
}

}  // namespace

bool ExportSettings(const std::wstring& configDirectory, const std::wstring& zipPath) {
    std::vector<std::pair<std::wstring, std::wstring>> files{{kIniName, configDirectory + L"\\" + kIniName}};
    for (const wchar_t* folder : kFolders) {
        CollectFolder(configDirectory + L"\\" + folder, std::wstring(folder) + L"/", files);
    }
    std::vector<Entry> entries;
    for (auto& [name, path] : files) {
        std::optional<std::vector<uint8_t>> data = ReadAll(path);
        if (!data) {
            return false;
        }
        entries.push_back({std::move(name), std::move(*data)});
    }
    // The archive replaces the target only once it is complete.
    const std::wstring partial = zipPath + L".part";
    if (!WriteAll(partial, BuildArchive(entries)) ||
        !MoveFileExW(partial.c_str(), zipPath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(partial.c_str());
        return false;
    }
    return true;
}

bool CopySettings(const std::wstring& from, const std::wstring& to) {
    std::vector<std::pair<std::wstring, std::wstring>> files;
    for (const wchar_t* folder : kFolders) {
        CollectFolder(from + L"\\" + folder, std::wstring(folder) + L"/", files);
    }
    files.emplace_back(kIniName, from + L"\\" + kIniName);
    for (auto& [name, path] : files) {
        std::replace(name.begin(), name.end(), L'/', L'\\');
        const std::wstring target = to + L"\\" + name;
        const std::wstring folder = target.substr(0, target.find_last_of(L'\\'));
        const int made = SHCreateDirectoryExW(nullptr, folder.c_str(), nullptr);
        if ((made != ERROR_SUCCESS && made != ERROR_ALREADY_EXISTS && made != ERROR_FILE_EXISTS) ||
            !CopyFileW(path.c_str(), target.c_str(), FALSE)) {
            return false;
        }
    }
    return true;
}

bool RecycleSettings(const std::wstring& directory) {
    // A list of paths, each ended by a null character and the whole by another.
    std::wstring paths;
    for (const std::wstring& name : {std::wstring(kIniName), std::wstring(kFolders[0]), std::wstring(kFolders[1])}) {
        const std::wstring path = directory + L"\\" + name;
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
            paths += path;
            paths += L'\0';
        }
    }
    if (!paths.empty()) {
        paths += L'\0';
        SHFILEOPSTRUCTW operation{};
        operation.wFunc = FO_DELETE;
        operation.pFrom = paths.c_str();
        operation.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
        SHFileOperationW(&operation);
    }
    return GetFileAttributesW((directory + L"\\" + kIniName).c_str()) == INVALID_FILE_ATTRIBUTES;
}

bool IsSettingsArchive(const std::wstring& zipPath) {
    return ReadSettingsArchive(zipPath).has_value();
}

std::wstring PendingImportPath(const std::wstring& configDirectory) {
    return configDirectory + L"\\Pending import.zip";
}

bool ImportSettings(const std::wstring& configDirectory, const std::wstring& zipPath) {
    const std::optional<std::vector<Entry>> entries = ReadSettingsArchive(zipPath);
    if (!entries) {
        return false;
    }
    const std::wstring backups = configDirectory + L"\\Backups";
    CreateDirectoryW(backups.c_str(), nullptr);
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t backup[64];
    swprintf_s(backup, L"\\Before import %04u-%02u-%02u %02u%02u%02u.zip", now.wYear, now.wMonth, now.wDay, now.wHour,
               now.wMinute, now.wSecond);
    if (!ExportSettings(configDirectory, backups + backup)) {
        return false;
    }
    bool ok = true;
    for (const Entry& entry : *entries) {
        std::wstring path = configDirectory + L"\\" + entry.name;
        std::replace(path.begin() + static_cast<std::ptrdiff_t>(configDirectory.size()), path.end(), L'/', L'\\');
        for (size_t slash = path.find(L'\\', configDirectory.size() + 1); slash != std::wstring::npos;
             slash = path.find(L'\\', slash + 1)) {
            CreateDirectoryW(path.substr(0, slash).c_str(), nullptr);
        }
        ok = WriteAll(path, entry.data) && ok;
    }
    return ok;
}
