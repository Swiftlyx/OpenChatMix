// The weather from Open-Meteo (open-meteo.com), which needs no key: the place's coordinates from its geocoding
// API, then the current temperature and weather code.

#include "infoscreen.h"

#include <winhttp.h>

#include <cmath>
#include <cwchar>
#include <map>

#include "../../src/json.h"

namespace {

constexpr wchar_t kUserAgent[] = L"OpenChatMix-InfoScreen";
constexpr int kTimeoutMs = 15000;
constexpr size_t kMaxBody = 1024 * 1024;

std::wstring UrlEncode(const std::wstring& text) {
    const std::string utf8 = Utf8(text);
    std::wstring encoded;
    for (const unsigned char ch : utf8) {
        if (isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            encoded += static_cast<wchar_t>(ch);
        } else {
            wchar_t escape[4];
            swprintf_s(escape, L"%%%02X", ch);
            encoded += escape;
        }
    }
    return encoded;
}

// WMO weather interpretation codes, grouped as the screen draws them.
Sky SkyOf(int code) {
    if (code <= 0) {
        return Sky::Clear;
    }
    if (code <= 2) {
        return Sky::PartlyCloudy;
    }
    if (code == 3) {
        return Sky::Cloudy;
    }
    if (code == 45 || code == 48) {
        return Sky::Fog;
    }
    if (code >= 51 && code <= 57) {
        return Sky::Drizzle;
    }
    if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82)) {
        return Sky::Rain;
    }
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) {
        return Sky::Snow;
    }
    if (code >= 95) {
        return Sky::Thunder;
    }
    return Sky::Cloudy;
}

// "50.45, 30.52" or "50.45 30.52".
bool ParseCoordinates(const std::wstring& text, double& latitude, double& longitude) {
    wchar_t rest = 0;
    if (swscanf_s(text.c_str(), L" %lf , %lf %c", &latitude, &longitude, &rest, 1) == 2 ||
        swscanf_s(text.c_str(), L" %lf %lf %c", &latitude, &longitude, &rest, 1) == 2) {
        return std::abs(latitude) <= 90.0 && std::abs(longitude) <= 180.0;
    }
    return false;
}

}  // namespace

std::optional<std::string> HttpGet(const std::wstring& url) {
    URL_COMPONENTS parts{sizeof(parts)};
    wchar_t host[256] = {};
    wchar_t path[2048] = {};
    parts.lpszHostName = host;
    parts.dwHostNameLength = static_cast<DWORD>(std::size(host));
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = static_cast<DWORD>(std::size(path));
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) {
        return std::nullopt;
    }
    std::optional<std::string> body;
    HINTERNET session = WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET connection = session ? WinHttpConnect(session, host, parts.nPort, 0) : nullptr;
    HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                                        WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                        parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                                   : nullptr;
    if (request) {
        WinHttpSetTimeouts(request, kTimeoutMs, kTimeoutMs, kTimeoutMs, kTimeoutMs);
    }
    DWORD status = 0;
    DWORD size = sizeof(status);
    if (request && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr) &&
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                            &status, &size, WINHTTP_NO_HEADER_INDEX) &&
        status == 200) {
        std::string data;
        for (;;) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request, &available) || available == 0 || data.size() > kMaxBody) {
                break;
            }
            std::string chunk(available, '\0');
            DWORD read = 0;
            if (!WinHttpReadData(request, chunk.data(), available, &read) || read == 0) {
                break;
            }
            data.append(chunk.data(), read);
        }
        if (data.size() <= kMaxBody) {
            body = std::move(data);
        }
    }
    for (HINTERNET handle : {request, connection, session}) {
        if (handle) {
            WinHttpCloseHandle(handle);
        }
    }
    return body;
}

std::vector<Place> SearchPlaces(const std::wstring& text, const std::wstring& language, std::wstring& error) {
    std::vector<Place> places;
    const std::optional<std::string> found = HttpGet(
        L"https://geocoding-api.open-meteo.com/v1/search?count=10&format=json&language=" + UrlEncode(language) +
        L"&name=" + UrlEncode(text));
    const std::optional<JsonValue> json = found ? ParseJson(*found) : std::nullopt;
    if (!json) {
        error = L"no answer from the geocoding service";
        return places;
    }
    const JsonValue* results = json->Find(L"results");
    if (!results || results->type != JsonValue::Type::Array) {
        return places;
    }
    auto field = [](const JsonValue& result, const wchar_t* key) {
        const JsonValue* value = result.Find(key);
        return value && value->type == JsonValue::Type::String ? value->string : std::wstring{};
    };
    // Name, region and country; the district joins them where two places would read alike.
    std::vector<std::wstring> names;
    std::map<std::wstring, int> seen;
    for (const JsonValue& result : results->array) {
        std::wstring label = field(result, L"name");
        for (const wchar_t* key : {L"admin1", L"country"}) {
            const std::wstring part = field(result, key);
            if (!part.empty() && part != field(result, L"name")) {
                label += L", " + part;
            }
        }
        names.push_back(label);
        ++seen[label];
    }
    // Where names still repeat, the district and then the community join them, and the coordinates last.
    for (const wchar_t* key : {L"admin2", L"admin3"}) {
        std::map<std::wstring, int> again;
        for (size_t i = 0; i < names.size(); ++i) {
            const std::wstring part = field(results->array[i], key);
            if (seen[names[i]] > 1 && !part.empty()) {
                const size_t comma = names[i].find(L',');
                names[i].insert(comma == std::wstring::npos ? names[i].size() : comma, L", " + part);
            }
            ++again[names[i]];
        }
        seen = std::move(again);
    }
    for (size_t i = 0; i < results->array.size(); ++i) {
        const JsonValue& result = results->array[i];
        const JsonValue* latitude = result.Find(L"latitude");
        const JsonValue* longitude = result.Find(L"longitude");
        if (!latitude || !longitude) {
            continue;
        }
        std::wstring label = names[i];
        if (seen[names[i]] > 1) {
            wchar_t where[64];
            swprintf_s(where, L" (%.2f, %.2f)", latitude->NumberOr(0.0), longitude->NumberOr(0.0));
            label += where;
        }
        wchar_t value[64];
        swprintf_s(value, L"%.4f,%.4f", latitude->NumberOr(0.0), longitude->NumberOr(0.0));
        places.push_back({value, label});
    }
    return places;
}

std::optional<Weather> FetchWeather(const std::wstring& place, bool fahrenheit, std::wstring& error) {
    // The place is looked up once; later forecasts reuse its coordinates.
    static std::wstring foundPlace;
    static double foundLatitude = 0.0;
    static double foundLongitude = 0.0;
    Weather weather;
    double latitude = 0.0;
    double longitude = 0.0;
    if (ParseCoordinates(place, latitude, longitude)) {
        weather.place.clear();
    } else if (place == foundPlace) {
        latitude = foundLatitude;
        longitude = foundLongitude;
        weather.place = place;
    } else {
        const std::optional<std::string> found = HttpGet(L"https://geocoding-api.open-meteo.com/v1/search?count=1&format=json&name=" +
                                                         UrlEncode(place));
        const std::optional<JsonValue> json = found ? ParseJson(*found) : std::nullopt;
        if (!json) {
            error = L"no answer from the geocoding service";
            return std::nullopt;
        }
        const JsonValue* results = json->Find(L"results");
        if (!results || results->type != JsonValue::Type::Array || results->array.empty()) {
            error = L"place not found";
            return std::nullopt;
        }
        const JsonValue& first = results->array.front();
        const JsonValue* lat = first.Find(L"latitude");
        const JsonValue* lon = first.Find(L"longitude");
        if (!lat || !lon) {
            error = L"place without coordinates";
            return std::nullopt;
        }
        latitude = lat->NumberOr(0.0);
        longitude = lon->NumberOr(0.0);
        weather.place = place;
        foundPlace = place;
        foundLatitude = latitude;
        foundLongitude = longitude;
    }
    wchar_t url[512];
    swprintf_s(url,
               L"https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
               L"&current=temperature_2m,weather_code,is_day&temperature_unit=%ls&timezone=auto",
               latitude, longitude, fahrenheit ? L"fahrenheit" : L"celsius");
    const std::optional<std::string> answer = HttpGet(url);
    const std::optional<JsonValue> json = answer ? ParseJson(*answer) : std::nullopt;
    const JsonValue* current = json ? json->Find(L"current") : nullptr;
    const JsonValue* temperature = current ? current->Find(L"temperature_2m") : nullptr;
    if (!temperature || temperature->type != JsonValue::Type::Number) {
        error = L"no answer from the forecast service";
        return std::nullopt;
    }
    weather.temperature = temperature->number;
    const JsonValue* code = current->Find(L"weather_code");
    weather.sky = SkyOf(code ? static_cast<int>(code->NumberOr(0.0)) : 0);
    const JsonValue* day = current->Find(L"is_day");
    weather.day = !day || day->NumberOr(1.0) != 0.0;
    return weather;
}
