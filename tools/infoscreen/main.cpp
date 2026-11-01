// OpenChatMix Info Screen: reads the options OpenChatMix passes, gathers the figures on threads of their own and sends
// the screen to OpenChatMix's GameSense server as a 128x64 bitmap: often while it shows, every two seconds while it
// does not. Also renders a picture of the screen for the README (--png) and lists what the sensors see (--sources).

#include "infoscreen.h"

#include <shellapi.h>
#include <tlhelp32.h>
#include <wincodec.h>
#include <winhttp.h>

#include <algorithm>
#include <atomic>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <mutex>
#include <thread>

#include "../../src/json.h"

namespace {

constexpr char kGame[] = "OPENCHATMIX_INFO";
constexpr char kEvent[] = "SCREEN";
constexpr wchar_t kOptionsResource[] = L"OPENCHATMIX_OPTIONS";
constexpr DWORD kScrollFrameMs = 70;   // A long track moves a pixel per frame.
constexpr DWORD kShownFrameMs = 250;
constexpr DWORD kHiddenFrameMs = 1000;
// While the screen does not show, it is still sent this often: OpenChatMix drops a screen it has not had for 10 s,
// and the reply tells when it shows again.
constexpr ULONGLONG kHiddenSendMs = 2000;
constexpr DWORD kSensorShownMs = 1000;
constexpr DWORD kSensorHiddenMs = 5000;
constexpr DWORD kMediaMs = 1000;
constexpr ULONGLONG kWeatherMs = 15 * 60 * 1000;
constexpr ULONGLONG kWeatherRetryMs = 2 * 60 * 1000;
constexpr ULONGLONG kWeatherStaleMs = 3 * 60 * 60 * 1000;  // Older weather is not shown.
constexpr ULONGLONG kReconnectMs = 2000;

// The figures, as the data threads last left them.
struct Shared {
    std::mutex mutex;
    Readings readings;
    std::optional<Track> track;
    std::optional<Weather> weather;
    ULONGLONG weatherTick = 0;
    std::atomic<bool> shown{false};
};

std::wstring Trim(const std::wstring& text) {
    const size_t first = text.find_first_not_of(L" \t");
    if (first == std::wstring::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(L" \t") - first + 1);
}

// The defaults of the options resource that OpenChatMix's panel shows too.
std::map<std::wstring, std::wstring> Defaults() {
    std::map<std::wstring, std::wstring> values;
    HRSRC resource = FindResourceW(nullptr, kOptionsResource, RT_RCDATA);
    HGLOBAL loaded = resource ? LoadResource(nullptr, resource) : nullptr;
    const auto* bytes = loaded ? static_cast<const char*>(LockResource(loaded)) : nullptr;
    if (!bytes) {
        return values;
    }
    const std::optional<JsonValue> root = ParseJson(std::string_view(bytes, SizeofResource(nullptr, resource)));
    const JsonValue* list = root ? root->Find(L"options") : nullptr;
    if (!list || list->type != JsonValue::Type::Array) {
        return values;
    }
    for (const JsonValue& entry : list->array) {
        const JsonValue* key = entry.Find(L"key");
        const JsonValue* fallback = entry.Find(L"default");
        if (key && key->type == JsonValue::Type::String && fallback && fallback->type == JsonValue::Type::String) {
            values[key->string] = fallback->string;
        }
    }
    return values;
}

// key=value pairs joined by ';', with %XX escapes, as OpenChatMix writes them.
void DecodeOptions(const std::wstring& line, std::map<std::wstring, std::wstring>& values) {
    auto decode = [](const std::wstring& text) {
        std::wstring plain;
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] == L'%' && i + 2 < text.size() && iswxdigit(text[i + 1]) && iswxdigit(text[i + 2])) {
                plain += static_cast<wchar_t>(wcstoul(text.substr(i + 1, 2).c_str(), nullptr, 16));
                i += 2;
            } else {
                plain += text[i];
            }
        }
        return plain;
    };
    for (size_t start = 0; start < line.size();) {
        size_t end = line.find(L';', start);
        if (end == std::wstring::npos) {
            end = line.size();
        }
        const std::wstring pair = line.substr(start, end - start);
        const size_t equals = pair.find(L'=');
        if (equals != std::wstring::npos) {
            values[decode(pair.substr(0, equals))] = decode(pair.substr(equals + 1));
        }
        start = end + 1;
    }
}

Options MakeOptions(const std::map<std::wstring, std::wstring>& values) {
    auto text = [&](const wchar_t* key) {
        const auto found = values.find(key);
        return found == values.end() ? std::wstring{} : found->second;
    };
    auto on = [&](const wchar_t* key) { return text(key) != L"0"; };
    Options options;
    options.music = on(L"music");
    options.weather = on(L"weather");
    options.city = Trim(text(L"city"));
    const std::wstring label = text(L"city.label");
    options.placeName = Trim(label.substr(0, label.find(L',')));
    options.fahrenheit = text(L"units") == L"f";
    options.cpuTemp = on(L"cpuTemp");
    options.cpuLoad = on(L"cpuLoad");
    options.gpuTemp = on(L"gpuTemp");
    options.gpuLoad = on(L"gpuLoad");
    options.ram = on(L"ram");
    const std::wstring memory = text(L"ramFormat");
    options.ramFormat = memory == L"percent" ? RamFormat::Percent
                        : memory == L"gb"    ? RamFormat::Gigabytes
                                             : RamFormat::Both;
    options.fans = on(L"fans");
    options.icons = text(L"labels") == L"icons";
    const std::wstring format = text(L"fanFormat");
    options.fanFormat = format == L"percent" ? FanFormat::Percent : format == L"both" ? FanFormat::Both : FanFormat::Rpm;
    // Picked in the panel, one per line; typed in earlier versions, separated by commas.
    const std::wstring names = text(L"fanNames");
    const wchar_t separator = names.find(L'\n') != std::wstring::npos ? L'\n' : L',';
    for (size_t start = 0; start <= names.size();) {
        size_t end = names.find(separator, start);
        if (end == std::wstring::npos) {
            end = names.size();
        }
        if (std::wstring name = Trim(names.substr(start, end - start)); !name.empty()) {
            options.fanNames.push_back(std::move(name));
        }
        start = end + 1;
    }
    return options;
}

// Whether OpenChatMix (or its preview) started this program.
bool StartedByOpenChatMix() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return true;
    }
    const DWORD self = GetCurrentProcessId();
    DWORD parent = 0;
    std::map<DWORD, std::wstring> names;
    PROCESSENTRY32W entry{sizeof(entry)};
    for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry)) {
        names[entry.th32ProcessID] = entry.szExeFile;
        if (entry.th32ProcessID == self) {
            parent = entry.th32ParentProcessID;
        }
    }
    CloseHandle(snapshot);
    const auto found = names.find(parent);
    return found != names.end() && _wcsnicmp(found->second.c_str(), L"OpenChatMix", 11) == 0;
}

// A client of the GameSense server whose address SteelSeries' coreProps.json holds; OpenChatMix writes its own
// there before it starts its add-ons.
class GameSense {
public:
    ~GameSense() { Close(); }

    bool Open() {
        Close();
        wchar_t data[MAX_PATH] = {};
        const DWORD length = GetEnvironmentVariableW(L"ProgramData", data, MAX_PATH);
        const std::wstring root = length && length < MAX_PATH ? data : L"C:\\ProgramData";
        std::ifstream file(root + L"\\SteelSeries\\SteelSeries Engine 3\\coreProps.json", std::ios::binary);
        const std::string props((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        const std::optional<JsonValue> json = ParseJson(props);
        const JsonValue* address = json ? json->Find(L"address") : nullptr;
        if (!address || address->type != JsonValue::Type::String) {
            return false;
        }
        const size_t colon = address->string.rfind(L':');
        if (colon == std::wstring::npos) {
            return false;
        }
        const std::wstring host = address->string.substr(0, colon);
        const int port = _wtoi(address->string.c_str() + colon + 1);
        session_ = WinHttpOpen(L"OpenChatMix-InfoScreen", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0);
        connection_ = session_ && port > 0 ? WinHttpConnect(session_, host.c_str(), static_cast<INTERNET_PORT>(port), 0)
                                           : nullptr;
        if (!connection_) {
            Close();
            return false;
        }
        const std::string metadata = std::string(R"({"game":")") + kGame +
                                     R"(","game_display_name":"OpenChatMix Info Screen","developer":"OpenChatMix"})";
        // The handler's own image is the empty screen; each event brings its frame.
        std::string bind = std::string(R"({"game":")") + kGame + R"(","event":")" + kEvent +
                           R"(","value_optional":true,"handlers":[{"device-type":"screened-128x64","zone":"one",)"
                           R"("mode":"screen","openchatmix-show":"idle","datas":[{"has-text":false,"image-data":[)";
        for (int i = 0; i < kScreenWidth * kScreenHeight / 8; ++i) {
            bind += i ? ",0" : "0";
        }
        bind += "]}]}]}";
        if (!Post("game_metadata", metadata, nullptr) || !Post("bind_game_event", bind, nullptr)) {
            Close();
            return false;
        }
        return true;
    }

    void Close() {
        for (HINTERNET* handle : {&connection_, &session_}) {
            if (*handle) {
                WinHttpCloseHandle(*handle);
                *handle = nullptr;
            }
        }
    }

    bool Post(const char* endpoint, const std::string& body, std::string* reply) {
        if (!connection_) {
            return false;
        }
        const std::wstring path = L"/" + Utf16(endpoint);
        HINTERNET request = WinHttpOpenRequest(connection_, L"POST", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                               WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
        if (!request) {
            return false;
        }
        const wchar_t headers[] = L"Content-Type: application/json\r\n";
        DWORD status = 0;
        DWORD size = sizeof(status);
        bool ok = WinHttpSendRequest(request, headers, static_cast<DWORD>(-1), const_cast<char*>(body.data()),
                                     static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0) &&
                  WinHttpReceiveResponse(request, nullptr) &&
                  WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                      WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) &&
                  status == 200;
        if (ok && reply) {
            reply->clear();
            DWORD available = 0;
            while (WinHttpQueryDataAvailable(request, &available) && available && reply->size() < 4096) {
                std::string chunk(available, '\0');
                DWORD read = 0;
                if (!WinHttpReadData(request, chunk.data(), available, &read) || !read) {
                    break;
                }
                reply->append(chunk.data(), read);
            }
        }
        WinHttpCloseHandle(request);
        return ok;
    }

private:
    HINTERNET session_ = nullptr;
    HINTERNET connection_ = nullptr;
};

// A game event with the frame packed as GameSense bitmaps are: row by row, eight pixels a byte, the leftmost in the
// most significant bit.
std::string FrameEvent(const Frame& frame, int value) {
    std::string json = std::string(R"({"game":")") + kGame + R"(","event":")" + kEvent +
                       R"(","data":{"value":)" + std::to_string(value) + R"(,"frame":{"image-data-128x64":[)";
    for (size_t i = 0; i < frame.pixels.size(); i += 8) {
        unsigned byte = 0;
        for (size_t bit = 0; bit < 8; ++bit) {
            byte = (byte << 1) | frame.pixels[i + bit];
        }
        json += (i ? "," : "") + std::to_string(byte);
    }
    json += "]}}}";
    return json;
}

// The screen as a PNG; from a scale of 4 each pixel gets a thin dark gap, as the display looks up close.
bool SavePng(const Frame& frame, const std::wstring& path, int scale) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IWICImagingFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) {
        return false;
    }
    const UINT width = kScreenWidth * scale;
    const UINT height = kScreenHeight * scale;
    std::vector<uint32_t> pixels(static_cast<size_t>(width) * height, 0xFF000000u);
    const int lit = scale >= 4 ? scale - 1 : scale;
    for (int y = 0; y < kScreenHeight; ++y) {
        for (int x = 0; x < kScreenWidth; ++x) {
            if (!frame.pixels[y * kScreenWidth + x]) {
                continue;
            }
            for (int py = 0; py < lit; ++py) {
                for (int px = 0; px < lit; ++px) {
                    pixels[static_cast<size_t>(y * scale + py) * width + x * scale + px] = 0xFFFFFFFFu;
                }
            }
        }
    }
    bool saved = false;
    IWICStream* stream = nullptr;
    IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* target = nullptr;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(factory->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
        SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
        SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache)) &&
        SUCCEEDED(encoder->CreateNewFrame(&target, nullptr)) && SUCCEEDED(target->Initialize(nullptr)) &&
        SUCCEEDED(target->SetSize(width, height)) && SUCCEEDED(target->SetPixelFormat(&format)) &&
        format == GUID_WICPixelFormat32bppBGRA &&
        SUCCEEDED(target->WritePixels(height, width * 4, static_cast<UINT>(pixels.size() * 4),
                                      reinterpret_cast<BYTE*>(pixels.data()))) &&
        SUCCEEDED(target->Commit()) && SUCCEEDED(encoder->Commit())) {
        saved = true;
    }
    for (IUnknown* object : std::initializer_list<IUnknown*>{target, encoder, stream, factory}) {
        if (object) {
            object->Release();
        }
    }
    return saved;
}

// Figures for the README picture.
void FillDemo(ScreenData& data, int demoFans) {
    data.track = Track{L"Around the World", L"Daft Punk"};
    data.weather = Weather{L"Kyiv", data.options.fahrenheit ? 45.0 : 7.0, Sky::PartlyCloudy, true};
    Readings& readings = data.readings;
    readings.cpuTemp = 54.0;
    readings.cpuLoad = 12.0;
    readings.gpuTemp = 47.0;
    readings.gpuLoad = 31.0;
    readings.ramUsedGb = 14.2;
    readings.ramPercent = 45.0;
    readings.fans = {{.name = L"CPU Fan", .tag = "CPU", .rpm = 1180.0, .percent = 42.0},
                     {.name = L"Chassis Fan 1", .tag = "1", .rpm = 860.0, .percent = 35.0},
                     {.name = L"GPU Fan", .rpm = 1450.0, .percent = 38.0, .gpu = true}};
    // --demo-fans: more case fans, to see how they take turns.
    for (int i = 0; i < demoFans; ++i) {
        readings.fans.push_back({.name = L"Chassis Fan " + std::to_wstring(i + 2),
                                 .tag = std::to_string(i + 2),
                                 .rpm = 700.0 + 90.0 * i,
                                 .percent = 30.0 + 4.0 * i});
    }
}

std::string JsonString(const std::wstring& text) {
    std::string quoted = "\"";
    for (const char ch : Utf8(text)) {
        if (ch == '"' || ch == '\\') {
            quoted += '\\';
            quoted += ch;
        } else if (static_cast<unsigned char>(ch) < 0x20) {
            char escape[8];
            snprintf(escape, sizeof(escape), "\\u%04x", static_cast<unsigned>(ch));
            quoted += escape;
        } else {
            quoted += ch;
        }
    }
    return quoted + "\"";
}

// Answers a query of OpenChatMix's panel on standard output: {"choices": [{"value", "label"}]} or {"error"}.
// fanNames lists the fans the sensors find; city lists the places of the typed name.
int AnswerQuery(const std::wstring& key, const std::wstring& text, const std::wstring& language) {
    std::vector<std::pair<std::wstring, std::wstring>> choices;
    std::wstring error;
    if (key == L"fanNames") {
        Options all;
        Sensors sensors;
        for (const Fan& fan : sensors.Read(all).fans) {
            std::wstring label = fan.name;
            if (fan.rpm) {
                label += L" \u00B7 " + std::to_wstring(std::lround(*fan.rpm)) + L" RPM";
            } else if (fan.percent) {
                label += L" \u00B7 " + std::to_wstring(std::lround(*fan.percent)) + L"%";
            }
            choices.emplace_back(fan.name, label);
        }
    } else if (key == L"city") {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        for (const Place& place : SearchPlaces(text, language, error)) {
            choices.emplace_back(place.value, place.label);
        }
    } else {
        error = L"unknown query";
    }
    std::string json = "{";
    if (!error.empty()) {
        json += "\"error\":" + JsonString(error) + ",";
    }
    json += "\"choices\":[";
    for (size_t i = 0; i < choices.size(); ++i) {
        json += (i ? ",{" : "{") + std::string("\"value\":") + JsonString(choices[i].first) +
                ",\"label\":" + JsonString(choices[i].second) + "}";
    }
    json += "]}";
    DWORD written = 0;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), json.data(), static_cast<DWORD>(json.size()), &written, nullptr);
    return 0;
}

void WriteUtf8(const std::wstring& path, const std::wstring& text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    const std::string bytes = Utf8(text);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void StartThreads(const Options& options, Shared& shared) {
    std::thread([&options, &shared] {
        Sensors sensors;
        // The CPU load is the difference of two readings, so the second comes soon after the first.
        for (bool first = true;; first = false) {
            Readings readings = sensors.Read(options);
            {
                std::lock_guard<std::mutex> lock(shared.mutex);
                shared.readings = std::move(readings);
            }
            Sleep(first || shared.shown ? kSensorShownMs : kSensorHiddenMs);
        }
    }).detach();
    if (options.music) {
        std::thread([&shared] {
            CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            for (;;) {
                std::optional<Track> track = ReadPlayingTrack();
                {
                    std::lock_guard<std::mutex> lock(shared.mutex);
                    shared.track = std::move(track);
                }
                Sleep(kMediaMs);
            }
        }).detach();
    }
    if (options.weather && !options.city.empty()) {
        std::thread([&options, &shared] {
            for (;;) {
                std::wstring error;
                std::optional<Weather> weather = FetchWeather(options.city, options.fahrenheit, error);
                if (weather && !options.placeName.empty()) {
                    weather->place = options.placeName;
                }
                if (weather) {
                    std::lock_guard<std::mutex> lock(shared.mutex);
                    shared.weather = std::move(weather);
                    shared.weatherTick = GetTickCount64();
                }
                Sleep(static_cast<DWORD>(weather ? kWeatherMs : kWeatherRetryMs));
            }
        }).detach();
    }
}

ScreenData Snapshot(const Options& options, Shared& shared, ULONGLONG now) {
    ScreenData data;
    data.options = options;
    std::lock_guard<std::mutex> lock(shared.mutex);
    data.readings = shared.readings;
    data.track = shared.track;
    if (shared.weather && now - shared.weatherTick < kWeatherStaleMs) {
        data.weather = shared.weather;
    }
    return data;
}

int Serve(const Options& options) {
    Shared shared;
    StartThreads(options, shared);
    GameSense server;
    bool connected = false;
    ULONGLONG retryAt = 0;
    const ULONGLONG start = GetTickCount64();
    Frame sent;
    ULONGLONG sentAt = 0;
    int value = 0;
    std::optional<Track> lastTrack;
    ULONGLONG trackStart = start;
    for (;;) {
        const ULONGLONG now = GetTickCount64();
        ScreenData data = Snapshot(options, shared, now);
        const bool shown = shared.shown;
        if (data.track != lastTrack) {
            lastTrack = data.track;
            trackStart = now;
        }
        // A long track starts scrolling from its beginning once it shows.
        if (!shown) {
            trackStart = now;
        }
        data.trackMs = now - trackStart;
        Frame frame;
        bool scrolling = false;
        DrawScreen(data, now - start, frame, scrolling);
        const bool due = now - sentAt >= kHiddenSendMs || (shown && frame != sent);
        if (due && !connected && now >= retryAt) {
            connected = server.Open();
            retryAt = now + kReconnectMs;
        }
        if (due && connected) {
            std::string reply;
            if (server.Post("game_event", FrameEvent(frame, ++value), &reply)) {
                sent = frame;
                sentAt = now;
                shared.shown = reply.find(R"("openchatmix-shown":true)") != std::string::npos;
            } else {
                server.Close();
                connected = false;
                retryAt = now + kReconnectMs;
            }
        }
        Sleep(!shared.shown ? kHiddenFrameMs : scrolling ? kScrollFrameMs : kShownFrameMs);
    }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int count = 0;
    wchar_t** args = CommandLineToArgvW(GetCommandLineW(), &count);
    std::map<std::wstring, std::wstring> values = Defaults();
    std::wstring png;
    std::wstring sources;
    std::wstring query;
    std::wstring queryText;
    std::wstring language = L"en";
    bool demo = false;
    int demoFans = 0;
    bool given = false;
    ULONGLONG at = 0;
    int scale = 4;
    for (int i = 1; i < count; ++i) {
        const std::wstring arg = args[i];
        const bool hasValue = i + 1 < count;
        if (arg == L"--openchatmix-options" && hasValue) {
            DecodeOptions(args[++i], values);
            given = true;
        } else if (arg == L"--png" && hasValue) {
            png = args[++i];
        } else if (arg == L"--openchatmix-query" && hasValue) {
            query = args[++i];
        } else if (arg == L"--openchatmix-text" && hasValue) {
            queryText = args[++i];
        } else if (arg == L"--openchatmix-language" && hasValue) {
            language = args[++i];
        } else if (arg == L"--sources" && hasValue) {
            sources = args[++i];
        } else if (arg == L"--demo") {
            demo = true;
        } else if (arg == L"--demo-fans" && hasValue) {
            demoFans = std::clamp(_wtoi(args[++i]), 0, 12);
        } else if (arg == L"--time" && hasValue) {
            at = _wtoi64(args[++i]);
        } else if (arg == L"--scale" && hasValue) {
            scale = std::clamp(_wtoi(args[++i]), 1, 16);
        }
    }
    LocalFree(args);
    const Options options = MakeOptions(values);

    if (!query.empty()) {
        return AnswerQuery(query, queryText, language);
    }
    if (!sources.empty()) {
        Sensors sensors;
        std::wstring text = sensors.Describe();
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const std::optional<Track> track = ReadPlayingTrack();
        text += L"\r\nPlaying: " + (track ? track->artist + L" - " + track->title : std::wstring(L"nothing")) + L"\r\n";
        if (!options.city.empty()) {
            std::wstring error;
            const std::optional<Weather> weather = FetchWeather(options.city, options.fahrenheit, error);
            text += L"Weather in " + options.city + L": " +
                    (weather ? std::to_wstring(weather->temperature) + L", sky " +
                                   std::to_wstring(static_cast<int>(weather->sky))
                             : error) +
                    L"\r\n";
        }
        WriteUtf8(sources, text);
        return 0;
    }
    if (!png.empty()) {
        ScreenData data;
        data.options = options;
        // The data threads keep running until the process ends, so their state outlives this block.
        static Shared shared;
        if (demo) {
            FillDemo(data, demoFans);
        } else {
            StartThreads(options, shared);
            // The CPU load needs two readings; the weather needs its answer.
            for (int wait = 0; wait < 50; ++wait) {
                Sleep(200);
                std::lock_guard<std::mutex> lock(shared.mutex);
                if (shared.readings.cpuLoad && (!options.weather || options.city.empty() || shared.weather)) {
                    break;
                }
            }
            data = Snapshot(options, shared, GetTickCount64());
        }
        data.trackMs = at;
        Frame frame;
        bool scrolling = false;
        DrawScreen(data, at, frame, scrolling);
        const bool saved = SavePng(frame, png, scale);
        ExitProcess(saved ? 0 : 1);
    }
    if (!given && !StartedByOpenChatMix()) {
        const bool ukrainian = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_UKRAINIAN;
        MessageBoxW(nullptr,
                    ukrainian ? L"OpenChatMix Info Screen є доповненням для OpenChatMix.\n\nДодайте його в OpenChatMix: "
                                L"Налаштування → Сторонні доповнення → Додати. Тоді OpenChatMix сам запускатиме його, "
                                L"поки SteelSeries GG закритий."
                              : L"OpenChatMix Info Screen is an add-on for OpenChatMix.\n\nAdd it in OpenChatMix: "
                                L"Settings → 3rd party add-ons → Add. OpenChatMix then starts it while SteelSeries GG "
                                L"is closed.",
                    L"OpenChatMix Info Screen", MB_ICONINFORMATION | MB_OK);
        return 0;
    }
    return Serve(options);
}
