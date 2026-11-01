#pragma once

#include <windows.h>

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

// OpenChatMix Info Screen: an add-on that draws what plays, the weather and the PC's sensors on the GameDAC's
// 128x64 display. OpenChatMix starts it while SteelSeries GG is closed; it talks GameSense to OpenChatMix's server
// and sends whole frames as bitmaps.

constexpr int kScreenWidth = 128;
constexpr int kScreenHeight = 64;

// One frame, one byte per pixel (0 off, 1 on), row-major.
struct Frame {
    std::array<uint8_t, kScreenWidth * kScreenHeight> pixels{};

    void Set(int x, int y) {
        if (x >= 0 && x < kScreenWidth && y >= 0 && y < kScreenHeight) {
            pixels[y * kScreenWidth + x] = 1;
        }
    }
    bool operator==(const Frame&) const = default;
};

enum class FanFormat { Percent, Rpm, Both };
enum class RamFormat { Percent, Gigabytes, Both };

// What the user chose in OpenChatMix's panel; defaults come from the options resource.
struct Options {
    bool music = true;
    bool weather = true;
    std::wstring city;       // A place name, or "latitude,longitude" of a place picked in the panel.
    std::wstring placeName;  // Shown beside the weather: the picked place's name, else the typed one.
    bool fahrenheit = false;  // The weather's unit; the PC's parts stay in degrees Celsius.
    bool cpuTemp = true;
    bool cpuLoad = true;
    bool gpuTemp = true;
    bool gpuLoad = true;
    bool ram = true;
    RamFormat ramFormat = RamFormat::Both;
    bool fans = true;
    FanFormat fanFormat = FanFormat::Rpm;
    std::vector<std::wstring> fanNames;  // Fans to show, by name, in this order; empty for every fan that spins.
    bool icons = false;  // Icons label the figures instead of the names CPU, GPU, RAM and FAN.
};

struct Fan {
    std::wstring name;               // As the sensor tool names it.
    std::string tag;                 // Its short label on the screen, e.g. "CPU" or a header's number; empty: numbered.
    std::optional<double> rpm;
    std::optional<double> percent;   // Its control's duty, when the source has one.
    bool gpu = false;
};

// The PC's sensors at the last check; a value is missing when no source has it.
struct Readings {
    std::optional<double> cpuTemp;  // °C.
    std::optional<double> cpuLoad;  // Percent.
    std::optional<double> gpuTemp;
    std::optional<double> gpuLoad;
    std::optional<double> ramUsedGb;
    std::optional<double> ramPercent;
    std::vector<Fan> fans;
};

struct Track {
    std::wstring title;
    std::wstring artist;
    bool operator==(const Track&) const = default;
};

// WMO weather code groups, as the screen draws them.
enum class Sky { Clear, PartlyCloudy, Cloudy, Fog, Drizzle, Rain, Snow, Thunder };

struct Weather {
    std::wstring place;
    double temperature = 0.0;  // In the unit chosen when it was fetched.
    Sky sky = Sky::Clear;
    bool day = true;
};

// What goes on the screen.
struct ScreenData {
    Options options;
    Readings readings;
    std::optional<Track> track;  // While something plays.
    ULONGLONG trackMs = 0;       // Since the track came on the screen; scrolls a long one.
    std::optional<Weather> weather;
};

// pawnio.cpp: the CPU temperature and the motherboard's fans through PawnIO, when it is installed and this program
// runs as administrator. One thread.
class PawnIo {
public:
    PawnIo();
    ~PawnIo();
    PawnIo(const PawnIo&) = delete;
    PawnIo& operator=(const PawnIo&) = delete;

    std::optional<double> CpuTemperature();
    // The fans of a Nuvoton Super I/O chip, every channel, with their duty.
    std::vector<Fan> BoardFans();
    std::wstring Describe() const;

private:
    struct Impl;
    Impl* impl_;
};

// sensors.cpp: polled from one thread.
class Sensors {
public:
    Sensors();
    ~Sensors();
    Sensors(const Sensors&) = delete;
    Sensors& operator=(const Sensors&) = delete;

    Readings Read(const Options& options);
    // Every reading of every source, for --sources.
    std::wstring Describe();

private:
    struct Impl;
    Impl* impl_;
};

// media.cpp: the track that plays in a Windows media session; runs on its own thread (COM MTA).
std::optional<Track> ReadPlayingTrack();

// weather.cpp: Open-Meteo, no key needed. place is a city name or "latitude, longitude".
std::optional<Weather> FetchWeather(const std::wstring& place, bool fahrenheit, std::wstring& error);

struct Place {
    std::wstring value;  // "latitude,longitude".
    std::wstring label;  // Name, region and country, with the district where names repeat.
};
// Places of that name, the most populous first; language is a code such as "en" or "uk" for their names.
std::vector<Place> SearchPlaces(const std::wstring& text, const std::wstring& language, std::wstring& error);
// HTTPS GET of a public URL; the body as UTF-8.
std::optional<std::string> HttpGet(const std::wstring& url);

// screen.cpp
// Draws the screen; elapsedMs moves the burn-in shift and scrolls a long track. scrolling is set when the track
// does not fit and moves with time.
void DrawScreen(const ScreenData& data, ULONGLONG elapsedMs, Frame& frame, bool& scrolling);
std::wstring Utf16(const std::string& utf8);
std::string Utf8(const std::wstring& text);
