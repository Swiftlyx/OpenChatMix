#include "addon_options.h"

#include <windows.h>

#include <algorithm>
#include <cwchar>
#include <string_view>

#include "i18n.h"
#include "json.h"

namespace {

constexpr wchar_t kResourceName[] = L"OPENCHATMIX_OPTIONS";
constexpr DWORD kQueryTimeoutMs = 20000;
constexpr DWORD kQueryPipeBytes = 256 * 1024;  // The whole answer fits, so the program never waits to write it.

struct CachedOptions {
    FILETIME written{};
    Language language = Language::English;
    std::vector<AddOnOption> options;
};

const wchar_t* LanguageCode(Language language) {
    switch (language) {
    case Language::Ukrainian: return L"uk";
    case Language::Turkish: return L"tr";
    default: return L"en";
    }
}

// A label: the string itself, or the one for the language from an object of them, else English, else any.
std::wstring Localized(const JsonValue* value, const wchar_t* language) {
    if (!value) {
        return {};
    }
    if (value->type == JsonValue::Type::String) {
        return value->string;
    }
    if (value->type != JsonValue::Type::Object || value->object.empty()) {
        return {};
    }
    for (const wchar_t* code : {language, L"en"}) {
        if (const JsonValue* text = value->Find(code); text && text->type == JsonValue::Type::String) {
            return text->string;
        }
    }
    const JsonValue& first = value->object.front().second;
    return first.type == JsonValue::Type::String ? first.string : std::wstring{};
}

std::wstring StringOf(const JsonValue& object, const wchar_t* key) {
    const JsonValue* field = object.Find(key);
    if (!field) {
        return {};
    }
    if (field->type == JsonValue::Type::Bool) {
        return field->boolean ? L"1" : L"0";
    }
    if (field->type == JsonValue::Type::Number) {
        return std::to_wstring(static_cast<long long>(field->number));
    }
    return field->type == JsonValue::Type::String ? field->string : std::wstring{};
}

std::vector<AddOnOption> ReadOptions(const std::wstring& path, Language language) {
    std::vector<AddOnOption> options;
    HMODULE module =
        LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!module) {
        return options;
    }
    std::string json;
    if (HRSRC resource = FindResourceW(module, kResourceName, RT_RCDATA)) {
        if (HGLOBAL loaded = LoadResource(module, resource)) {
            const auto* bytes = static_cast<const char*>(LockResource(loaded));
            const DWORD size = SizeofResource(module, resource);
            if (bytes && size) {
                json.assign(bytes, size);
            }
        }
    }
    FreeLibrary(module);
    const std::optional<JsonValue> root = json.empty() ? std::nullopt : ParseJson(json);
    const JsonValue* list = root ? root->Find(L"options") : nullptr;
    if (!list || list->type != JsonValue::Type::Array) {
        return options;
    }
    const wchar_t* code = LanguageCode(language);
    for (const JsonValue& entry : list->array) {
        const std::wstring type = StringOf(entry, L"type");
        AddOnOption option;
        if (type == L"section") {
            option.kind = AddOnOption::Kind::Section;
        } else if (type == L"switch") {
            option.kind = AddOnOption::Kind::Switch;
        } else if (type == L"choice") {
            option.kind = AddOnOption::Kind::Choice;
        } else if (type == L"multi") {
            option.kind = AddOnOption::Kind::Multi;
        } else if (type == L"text") {
            option.kind = AddOnOption::Kind::Text;
        } else if (type == L"search") {
            option.kind = AddOnOption::Kind::Search;
        } else if (type == L"hint") {
            option.kind = AddOnOption::Kind::Hint;
        } else {
            continue;
        }
        option.key = StringOf(entry, L"key");
        option.label = Localized(entry.Find(L"label"), code);
        option.hint = Localized(entry.Find(L"hint"), code);
        option.fallback = StringOf(entry, L"default");
        option.showIf = StringOf(entry, L"showIf");
        option.query = StringOf(entry, L"query") == L"1";
        option.empty = Localized(entry.Find(L"empty"), code);
        if (const JsonValue* choices = entry.Find(L"choices"); choices && choices->type == JsonValue::Type::Array) {
            for (const JsonValue& choice : choices->array) {
                option.choices.push_back({StringOf(choice, L"value"), Localized(choice.Find(L"label"), code)});
            }
        }
        const bool keyed = option.kind != AddOnOption::Kind::Section && option.kind != AddOnOption::Kind::Hint;
        if ((keyed && option.key.empty()) || (option.kind == AddOnOption::Kind::Choice && option.choices.empty())) {
            continue;
        }
        options.push_back(std::move(option));
    }
    return options;
}

}  // namespace

const std::vector<AddOnOption>& AddOnOptions(const std::wstring& path) {
    static std::map<std::wstring, CachedOptions> cache;
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        static const std::vector<AddOnOption> none;
        return none;
    }
    const Language language = CurrentLanguage();
    CachedOptions& cached = cache[path];
    if (CompareFileTime(&cached.written, &data.ftLastWriteTime) != 0 || cached.language != language) {
        cached.written = data.ftLastWriteTime;
        cached.language = language;
        cached.options = ReadOptions(path, language);
    }
    return cached.options;
}

std::wstring AddOnOptionValue(const AddOnSettings& addOn, const AddOnOption& option) {
    const auto found = addOn.options.find(option.key);
    return found == addOn.options.end() ? option.fallback : found->second;
}

bool AddOnOptionShown(const AddOnSettings& addOn, const std::vector<AddOnOption>& options, const AddOnOption& option) {
    if (option.showIf.empty()) {
        return true;
    }
    const auto control = std::find_if(options.begin(), options.end(),
                                      [&](const AddOnOption& other) { return other.key == option.showIf; });
    return control == options.end() || AddOnOptionValue(addOn, *control) == L"1";
}

namespace {

// One argument as the C runtime splits command lines: quoted, with backslashes doubled before quotes.
std::wstring QuoteArgument(const std::wstring& argument) {
    std::wstring quoted = L"\"";
    for (size_t i = 0;; ++i) {
        size_t backslashes = 0;
        while (i < argument.size() && argument[i] == L'\\') {
            ++backslashes;
            ++i;
        }
        if (i == argument.size()) {
            quoted.append(backslashes * 2, L'\\');
            break;
        }
        if (argument[i] == L'"') {
            quoted.append(backslashes * 2 + 1, L'\\');
        } else {
            quoted.append(backslashes, L'\\');
        }
        quoted += argument[i];
    }
    return quoted + L"\"";
}

}  // namespace

AddOnQueryResult QueryAddOn(const std::wstring& path, const std::wstring& key, const std::wstring& text) {
    AddOnQueryResult result;
    result.path = path;
    result.key = key;
    result.text = text;
    SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
    HANDLE read = nullptr;
    HANDLE write = nullptr;
    if (!CreatePipe(&read, &write, &inherit, kQueryPipeBytes)) {
        return result;
    }
    SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);
    std::wstring command = QuoteArgument(path) + L" --openchatmix-query " + QuoteArgument(key) +
                           L" --openchatmix-language " + LanguageCode(CurrentLanguage());
    if (!text.empty()) {
        command += L" --openchatmix-text " + QuoteArgument(text);
    }
    // Only the pipe's end goes to the program, so its output ends when the program does.
    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    std::vector<uint8_t> attributes(size);
    auto* list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.StartupInfo.wShowWindow = SW_HIDE;
    startup.StartupInfo.hStdOutput = write;
    startup.StartupInfo.hStdError = write;
    PROCESS_INFORMATION process{};
    const size_t slash = path.find_last_of(L"\\/");
    const std::wstring folder = slash == std::wstring::npos ? L"." : path.substr(0, slash);
    bool started = false;
    if (InitializeProcThreadAttributeList(list, 1, 0, &size)) {
        if (UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &write, sizeof(write), nullptr,
                                      nullptr)) {
            startup.lpAttributeList = list;
            started = CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, TRUE,
                                     CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, folder.c_str(),
                                     &startup.StartupInfo, &process);
        }
        DeleteProcThreadAttributeList(list);
    }
    CloseHandle(write);
    if (!started) {
        CloseHandle(read);
        return result;
    }
    CloseHandle(process.hThread);
    if (WaitForSingleObject(process.hProcess, kQueryTimeoutMs) != WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess, 1);
    }
    CloseHandle(process.hProcess);
    std::string output;
    char chunk[4096];
    DWORD got = 0;
    while (ReadFile(read, chunk, sizeof(chunk), &got, nullptr) && got) {
        output.append(chunk, got);
    }
    CloseHandle(read);
    const std::optional<JsonValue> json = ParseJson(output);
    if (!json || json->type != JsonValue::Type::Object) {
        return result;
    }
    result.answered = true;
    if (const JsonValue* error = json->Find(L"error"); error && error->type == JsonValue::Type::String) {
        result.error = error->string;
    }
    if (const JsonValue* choices = json->Find(L"choices"); choices && choices->type == JsonValue::Type::Array) {
        for (const JsonValue& choice : choices->array) {
            const std::wstring value = StringOf(choice, L"value");
            const std::wstring label = StringOf(choice, L"label");
            if (!value.empty()) {
                result.choices.push_back({value, label.empty() ? value : label});
            }
        }
    }
    return result;
}

std::wstring EncodeAddOnOptions(const std::map<std::wstring, std::wstring>& options) {
    auto encode = [](std::wstring_view text) {
        std::wstring encoded;
        for (const wchar_t ch : text) {
            if (ch < 0x20 || ch == L'%' || ch == L';' || ch == L'=' || ch == L'"' || ch == L'\\') {
                wchar_t escape[4];
                swprintf_s(escape, L"%%%02X", static_cast<unsigned>(ch));
                encoded += escape;
            } else {
                encoded += ch;
            }
        }
        return encoded;
    };
    std::wstring line;
    for (const auto& [key, value] : options) {
        line += (line.empty() ? L"" : L";") + encode(key) + L"=" + encode(value);
    }
    return line;
}
