#include "eq_presets.h"

#include <windows.h>

#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cwchar>

#include "app.h"
#include "app_info.h"
#include "settings.h"

namespace {

constexpr wchar_t kBandFrequencies[] = L"[32, 64, 125, 250, 500, 1000, 2000, 4000, 8000, 16000]";
constexpr size_t kMaxPresetBytes = 64 * 1024;

std::wstring ReadUtf8File(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return {};
    }
    std::string bytes(kMaxPresetBytes, '\0');
    DWORD read = 0;
    const BOOL ok = ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr);
    CloseHandle(file);
    bytes.resize(ok ? read : 0);
    if (bytes.rfind("\xEF\xBB\xBF", 0) == 0) {
        bytes.erase(0, 3);
    }
    const int length = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    std::wstring text(length, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), text.data(), length);
    return text;
}

// Value of a "key" in a flat JSON object; enough for the files this app writes and simple edits.
size_t FindValue(const std::wstring& text, const wchar_t* key) {
    const std::wstring quoted = std::wstring(L"\"") + key + L"\"";
    const size_t at = text.find(quoted);
    if (at == std::wstring::npos) {
        return std::wstring::npos;
    }
    const size_t colon = text.find(L':', at + quoted.size());
    return colon == std::wstring::npos ? std::wstring::npos : text.find_first_not_of(L" \t\r\n", colon + 1);
}

std::optional<std::wstring> ReadString(const std::wstring& text, const wchar_t* key) {
    size_t at = FindValue(text, key);
    if (at == std::wstring::npos || text[at] != L'"') {
        return std::nullopt;
    }
    std::wstring value;
    for (++at; at < text.size() && text[at] != L'"'; ++at) {
        if (text[at] == L'\\' && at + 1 < text.size()) {
            ++at;
        }
        value += text[at];
    }
    return value;
}

std::wstring Escape(const std::wstring& text) {
    std::wstring escaped;
    for (const wchar_t c : text) {
        if (c == L'"' || c == L'\\') {
            escaped += L'\\';
        }
        if (c >= L' ') {
            escaped += c;
        }
    }
    return escaped;
}

// A file name for a preset name: characters Windows doesn't allow in names become spaces.
std::wstring FileStemFor(const std::wstring& name) {
    std::wstring stem;
    for (const wchar_t c : name) {
        const wchar_t kept = c < L' ' || std::wcschr(L"<>:\"/\\|?*", c) ? L' ' : c;
        if (kept != L' ' || (!stem.empty() && stem.back() != L' ')) {
            stem += kept;
        }
    }
    while (!stem.empty() && (stem.back() == L' ' || stem.back() == L'.')) {
        stem.pop_back();
    }
    return stem.empty() ? std::wstring(L"Preset") : stem;
}

std::wstring StemOf(const std::wstring& path) {
    std::wstring name = FileNameOf(path);
    const size_t dot = name.find_last_of(L'.');
    return dot == std::wstring::npos ? name : name.substr(0, dot);
}

}  // namespace

std::wstring PresetDirectory() {
    const std::wstring directory = ConfigDirectory() + L"\\Presets";
    CreateDirectoryW(directory.c_str(), nullptr);
    return directory;
}

namespace {

// A path in the preset directory for name that no other file uses; keep may be reused.
std::wstring FreePathFor(const std::wstring& name, const std::wstring& keep = L"") {
    const std::wstring directory = PresetDirectory();
    const std::wstring stem = FileStemFor(name);
    std::wstring path = directory + L"\\" + stem + L".json";
    for (int copy = 2;
         _wcsicmp(path.c_str(), keep.c_str()) != 0 && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
         ++copy) {
        path = directory + L"\\" + stem + L" (" + std::to_wstring(copy) + L").json";
    }
    return path;
}

}  // namespace

std::optional<UserPreset> ReadPresetFile(const std::wstring& path) {
    const std::wstring text = ReadUtf8File(path);
    size_t at = FindValue(text, L"gainsDb");
    if (at == std::wstring::npos || text[at] != L'[') {
        return std::nullopt;
    }
    UserPreset preset;
    preset.path = path;
    preset.name = ReadString(text, L"name").value_or(StemOf(path));
    if (preset.name.empty()) {
        preset.name = StemOf(path);
    }
    ++at;
    for (int band = 0; band < kEqBands; ++band) {
        const wchar_t* start = text.c_str() + at;
        wchar_t* end = nullptr;
        const double value = std::wcstod(start, &end);
        if (end == start || !std::isfinite(value)) {
            return std::nullopt;
        }
        preset.gainsDb[band] = static_cast<float>(value);
        at = text.find_first_not_of(L" \t\r\n,", static_cast<size_t>(end - text.c_str()));
        if (at == std::wstring::npos) {
            return std::nullopt;
        }
    }
    return preset;
}

std::vector<UserPreset> LoadUserPresets() {
    std::vector<UserPreset> presets;
    WIN32_FIND_DATAW found{};
    const std::wstring directory = PresetDirectory();
    HANDLE search = FindFirstFileW((directory + L"\\*.json").c_str(), &found);
    if (search == INVALID_HANDLE_VALUE) {
        return presets;
    }
    do {
        if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            if (auto preset = ReadPresetFile(directory + L"\\" + found.cFileName)) {
                presets.push_back(std::move(*preset));
            }
        }
    } while (FindNextFileW(search, &found));
    FindClose(search);
    std::sort(presets.begin(), presets.end(), [](const UserPreset& a, const UserPreset& b) {
        return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    return presets;
}

bool WritePresetFile(const std::wstring& path, const std::wstring& name, const EqCurve& curve) {
    std::array<float, kEqBands> gains{};
    for (int band = 0; band < kEqBands; ++band) {
        gains[band] = curve[band] / 2.0f;
    }
    return WritePresetGains(path, name, gains);
}

bool WritePresetGains(const std::wstring& path, const std::wstring& name, const std::array<float, kEqBands>& gainsDb) {
    std::wstring gains;
    for (int band = 0; band < kEqBands; ++band) {
        wchar_t value[16];
        swprintf_s(value, L"%s%.1f", band ? L", " : L"", gainsDb[band]);
        gains += value;
    }
    const std::wstring json = L"{\n  \"app\": \"OpenChatMix\",\n  \"type\": \"equalizer\",\n  \"name\": \"" + Escape(name) +
                              L"\",\n  \"bandsHz\": " + kBandFrequencies + L",\n  \"gainsDb\": [" + gains + L"]\n}\n";
    const std::string utf8 = Utf8(json);
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const BOOL ok = WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    CloseHandle(file);
    return ok && written == utf8.size();
}

bool RenamePreset(const UserPreset& preset, const std::wstring& name) {
    // The file follows the name, so the folder stays readable; the old file goes once the new one is written.
    const std::wstring path = FreePathFor(name, preset.path);
    if (!WritePresetGains(path, name, preset.gainsDb)) {
        return false;
    }
    if (_wcsicmp(path.c_str(), preset.path.c_str()) != 0) {
        DeleteFileW(preset.path.c_str());
    }
    return true;
}

bool CopyPreset(const UserPreset& preset, const std::wstring& name) {
    return WritePresetGains(FreePathFor(name), name, preset.gainsDb);
}

bool DeletePreset(const UserPreset& preset) {
    // To the Recycle Bin, so a preset deleted by mistake can be restored. The path list ends with two nulls.
    std::wstring from = preset.path;
    from.push_back(L'\0');
    SHFILEOPSTRUCTW operation{};
    operation.wFunc = FO_DELETE;
    operation.pFrom = from.c_str();
    operation.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    return SHFileOperationW(&operation) == 0 && !operation.fAnyOperationsAborted;
}

std::optional<UserPreset> ImportPresetFile(const std::wstring& source) {
    auto preset = ReadPresetFile(source);
    if (!preset) {
        return std::nullopt;
    }
    const std::wstring directory = PresetDirectory();
    if (_wcsicmp(source.substr(0, source.find_last_of(L"\\/")).c_str(), directory.c_str()) == 0) {
        return preset;
    }
    // Keeps an existing preset with the same file name.
    std::wstring target = directory + L"\\" + FileNameOf(source);
    for (int copy = 2; GetFileAttributesW(target.c_str()) != INVALID_FILE_ATTRIBUTES; ++copy) {
        target = directory + L"\\" + StemOf(source) + L" (" + std::to_wstring(copy) + L").json";
    }
    if (!CopyFileW(source.c_str(), target.c_str(), TRUE)) {
        return std::nullopt;
    }
    preset->path = target;
    return preset;
}

EqCurve PresetCurve(const UserPreset& preset, int8_t range) {
    EqCurve curve{};
    for (int band = 0; band < kEqBands; ++band) {
        const long halfSteps = std::lround(preset.gainsDb[band] * 2.0f);
        curve[band] = static_cast<int8_t>(std::clamp<long>(halfSteps, -range, range));
    }
    return curve;
}
