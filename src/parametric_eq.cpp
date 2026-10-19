#include "parametric_eq.h"

#include <windows.h>

#include <shlobj.h>
#include <winsqlite/winsqlite3.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <initializer_list>
#include <utility>

#include "app.h"
#include "app_info.h"
#include "eq_presets.h"
#include "json.h"

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr float kOctaveCenters[kParametricBands] = {32, 64, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
constexpr float kDefaultQ = 1.0f;
// Q of a peaking band one octave wide, for ten-band graphic equalizers.
constexpr float kOctaveQ = 1.41f;

constexpr const wchar_t* kTypeKeys[kFilterTypeCount] = {L"peaking", L"lowShelf", L"highShelf",
                                                        L"lowPass", L"highPass", L"notch"};

std::optional<FilterType> ParseTypeKey(const std::wstring& key) {
    for (int i = 0; i < kFilterTypeCount; ++i) {
        if (_wcsicmp(key.c_str(), kTypeKeys[i]) == 0) {
            return static_cast<FilterType>(i);
        }
    }
    return std::nullopt;
}

EqBand Sanitize(EqBand band) {
    band.frequency = std::clamp(band.frequency, kEqMinFrequency, kEqMaxFrequency);
    band.gain = std::clamp(band.gain, -kEqMaxGain, kEqMaxGain);
    band.q = std::clamp(band.q, kEqMinQ, kEqMaxQ);
    return band;
}

// Builds a preset from the flat layout; each change is {band index, band}.
ParametricEq Shape(float preamp, std::initializer_list<std::pair<int, EqBand>> changes) {
    ParametricEq eq = FlatEq();
    eq.preamp = preamp;
    for (const auto& [index, band] : changes) {
        eq.bands[index] = band;
    }
    return eq;
}

EqBand Peak(float frequency, float gain, float q = kDefaultQ) {
    return {FilterType::Peaking, frequency, gain, q, true};
}
EqBand LowShelf(float frequency, float gain) {
    return {FilterType::LowShelf, frequency, gain, 0.707f, true};
}
EqBand HighShelf(float frequency, float gain) {
    return {FilterType::HighShelf, frequency, gain, 0.707f, true};
}
EqBand HighPass(float frequency) {
    return {FilterType::HighPass, frequency, 0.0f, 0.707f, true};
}
EqBand LowPass(float frequency) {
    return {FilterType::LowPass, frequency, 0.0f, 0.707f, true};
}

std::string ReadFileUtf8(const std::wstring& path) {
    std::string data;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return data;
    }
    LARGE_INTEGER size{};
    if (GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart < 1 << 20) {
        data.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        if (!ReadFile(file, data.data(), static_cast<DWORD>(data.size()), &read, nullptr)) {
            data.clear();
        }
        data.resize(read);
    }
    CloseHandle(file);
    return data;
}

std::string JsonEscape(const std::wstring& text) {
    std::string utf8 = Utf8(text);
    std::string out;
    for (const char c : utf8) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            char buffer[8];
            sprintf_s(buffer, "\\u%04x", static_cast<unsigned char>(c));
            out += buffer;
        } else {
            out += c;
        }
    }
    return out;
}

// Sonar keeps one configuration per channel ("vad"): 1 game with a parametric equalizer, 2 chat with
// a ten-band graphic one, 3 the microphone.
std::optional<ParametricEq> FromSonar(const JsonValue& data, EqChannel channel) {
    ParametricEq eq = FlatEq();
    // Game always has the parametric equalizer; the microphone has it in recent Sonar versions.
    if (channel == EqChannel::Game || channel == EqChannel::Media ||
        (channel == EqChannel::Mic && data.Find(L"parametricEQ"))) {
        const JsonValue* parametric = data.Find(L"parametricEQ");
        if (!parametric) {
            return std::nullopt;
        }
        eq.preamp = static_cast<float>(data.Find(L"generalGain") ? data.Find(L"generalGain")->NumberOr(0.0) : 0.0);
        const bool enabled = parametric->Find(L"enabled") ? parametric->Find(L"enabled")->BoolOr(true) : true;
        for (int i = 0; i < kParametricBands; ++i) {
            const JsonValue* filter = parametric->Find(L"filter" + std::to_wstring(i + 1));
            if (!filter) {
                continue;
            }
            EqBand band;
            const JsonValue* type = filter->Find(L"type");
            const std::wstring key = type ? type->string : L"";
            if (key == L"lowShelving") {
                band.type = FilterType::LowShelf;
            } else if (key == L"highShelving") {
                band.type = FilterType::HighShelf;
            } else if (key == L"lowPass") {
                band.type = FilterType::LowPass;
            } else if (key == L"highPass") {
                band.type = FilterType::HighPass;
            } else if (key == L"notch") {
                band.type = FilterType::Notch;
            }
            band.frequency = static_cast<float>(filter->Find(L"frequency") ? filter->Find(L"frequency")->NumberOr(1000)
                                                                           : 1000);
            band.gain = static_cast<float>(filter->Find(L"gain") ? filter->Find(L"gain")->NumberOr(0) : 0);
            band.q = static_cast<float>(filter->Find(L"qFactor") ? filter->Find(L"qFactor")->NumberOr(0.707) : 0.707);
            band.enabled = enabled && (filter->Find(L"enabled") ? filter->Find(L"enabled")->BoolOr(true) : true);
            eq.bands[i] = Sanitize(band);
        }
        return eq;
    }
    const JsonValue* graphic = data.Find(L"equalizer");
    if (!graphic) {
        return std::nullopt;
    }
    const bool enabled = graphic->Find(L"enabled") ? graphic->Find(L"enabled")->BoolOr(true) : true;
    constexpr const wchar_t* kKeys[kParametricBands] = {L"31Hz",   L"62Hz",   L"125Hz",  L"250Hz",  L"500Hz",
                                                        L"1000Hz", L"2000Hz", L"4000Hz", L"8000Hz", L"16000Hz"};
    for (int i = 0; i < kParametricBands; ++i) {
        const JsonValue* gain = graphic->Find(kKeys[i]);
        eq.bands[i] = Sanitize(Peak(kOctaveCenters[i], static_cast<float>(gain ? gain->NumberOr(0) : 0), kOctaveQ));
        eq.bands[i].enabled = enabled;
    }
    return eq;
}

}  // namespace

ParametricEq FlatEq() {
    ParametricEq eq;
    for (int i = 0; i < kParametricBands; ++i) {
        eq.bands[i] = Peak(kOctaveCenters[i], 0.0f);
    }
    return eq;
}

bool UsesGain(FilterType type) {
    return type == FilterType::Peaking || type == FilterType::LowShelf || type == FilterType::HighShelf;
}

Biquad MakeBiquad(const EqBand& band, double sampleRate) {
    if (!band.enabled || sampleRate <= 0.0) {
        return {};
    }
    const double frequency = std::min<double>(band.frequency, sampleRate * 0.45);
    const double a = std::pow(10.0, band.gain / 40.0);
    const double w0 = 2.0 * kPi * frequency / sampleRate;
    const double cosW = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * std::max<double>(band.q, kEqMinQ));
    const double rootA = std::sqrt(a);
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a0 = 1.0, a1 = 0.0, a2 = 0.0;
    switch (band.type) {
    case FilterType::Peaking:
        b0 = 1.0 + alpha * a;
        b1 = -2.0 * cosW;
        b2 = 1.0 - alpha * a;
        a0 = 1.0 + alpha / a;
        a1 = -2.0 * cosW;
        a2 = 1.0 - alpha / a;
        break;
    case FilterType::LowShelf:
        b0 = a * ((a + 1.0) - (a - 1.0) * cosW + 2.0 * rootA * alpha);
        b1 = 2.0 * a * ((a - 1.0) - (a + 1.0) * cosW);
        b2 = a * ((a + 1.0) - (a - 1.0) * cosW - 2.0 * rootA * alpha);
        a0 = (a + 1.0) + (a - 1.0) * cosW + 2.0 * rootA * alpha;
        a1 = -2.0 * ((a - 1.0) + (a + 1.0) * cosW);
        a2 = (a + 1.0) + (a - 1.0) * cosW - 2.0 * rootA * alpha;
        break;
    case FilterType::HighShelf:
        b0 = a * ((a + 1.0) + (a - 1.0) * cosW + 2.0 * rootA * alpha);
        b1 = -2.0 * a * ((a - 1.0) + (a + 1.0) * cosW);
        b2 = a * ((a + 1.0) + (a - 1.0) * cosW - 2.0 * rootA * alpha);
        a0 = (a + 1.0) - (a - 1.0) * cosW + 2.0 * rootA * alpha;
        a1 = 2.0 * ((a - 1.0) - (a + 1.0) * cosW);
        a2 = (a + 1.0) - (a - 1.0) * cosW - 2.0 * rootA * alpha;
        break;
    case FilterType::LowPass:
        b0 = (1.0 - cosW) / 2.0;
        b1 = 1.0 - cosW;
        b2 = (1.0 - cosW) / 2.0;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cosW;
        a2 = 1.0 - alpha;
        break;
    case FilterType::HighPass:
        b0 = (1.0 + cosW) / 2.0;
        b1 = -(1.0 + cosW);
        b2 = (1.0 + cosW) / 2.0;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cosW;
        a2 = 1.0 - alpha;
        break;
    case FilterType::Notch:
        b0 = 1.0;
        b1 = -2.0 * cosW;
        b2 = 1.0;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cosW;
        a2 = 1.0 - alpha;
        break;
    }
    return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

namespace {

double BandResponseDb(const EqBand& band, std::complex<double> z1, double sampleRate) {
    const std::complex<double> z2 = z1 * z1;
    const Biquad f = MakeBiquad(band, sampleRate);
    const double numerator = std::abs(f.b0 + f.b1 * z1 + f.b2 * z2);
    const double denominator = std::abs(1.0 + f.a1 * z1 + f.a2 * z2);
    return 20.0 * std::log10(std::max(numerator, 1e-9) / std::max(denominator, 1e-9));
}

// Solves the small dense system a x = b (Gaussian elimination with partial pivoting).
template <size_t N>
std::array<double, N> Solve(std::array<std::array<double, N>, N> a, std::array<double, N> b) {
    // Rows are addressed through pointers; the indices stay below N by construction.
    std::array<double*, N> rows{};
    for (size_t i = 0; i < N; ++i) {
        rows[i] = a[i].data();
    }
    double* rhs = b.data();
    for (size_t column = 0; column < N; ++column) {
        // The row with the largest pivot ends up at column.
        for (size_t row = column + 1; row < N; ++row) {
            if (std::fabs(rows[row][column]) > std::fabs(rows[column][column])) {
                std::swap(rows[row], rows[column]);
                std::swap(rhs[row], rhs[column]);
            }
        }
        if (std::fabs(rows[column][column]) < 1e-12) {
            continue;
        }
        for (size_t row = column + 1; row < N; ++row) {
            const double factor = rows[row][column] / rows[column][column];
            for (size_t k = column; k < N; ++k) {
                rows[row][k] -= factor * rows[column][k];
            }
            rhs[row] -= factor * rhs[column];
        }
    }
    std::array<double, N> x{};
    double* solution = x.data();
    for (size_t i = N; i-- > 0;) {
        double sum = rhs[i];
        for (size_t k = i + 1; k < N; ++k) {
            sum -= rows[i][k] * solution[k];
        }
        solution[i] = std::fabs(rows[i][i]) < 1e-12 ? 0.0 : sum / rows[i][i];
    }
    return x;
}

}  // namespace

double ResponseDb(const ParametricEq& eq, double frequency, double sampleRate) {
    const std::complex<double> z1 = std::polar(1.0, -2.0 * kPi * frequency / sampleRate);
    double db = eq.preamp;
    for (const auto& band : eq.bands) {
        if (band.enabled) {
            db += BandResponseDb(band, z1, sampleRate);
        }
    }
    for (const auto& band : EffectBands(eq.effects)) {
        db += BandResponseDb(band, z1, sampleRate);
    }
    return db;
}

void AssignBands(ParametricEq& target, const ParametricEq& preset) {
    target.bands = preset.bands;
    target.preamp = preset.preamp;
}

std::array<EqBand, kEffectBands> EffectBands(const EqEffects& effects) {
    return {EqBand{FilterType::LowShelf, 120.0f, effects.bassBoost, 0.707f, true},
            EqBand{FilterType::Peaking, 2500.0f, effects.voiceClarity, 0.9f, true},
            EqBand{FilterType::HighShelf, 6000.0f, effects.trebleBoost, 0.707f, true}};
}

const wchar_t* SmartVolumeKey(SmartVolume volume) {
    constexpr const wchar_t* kKeys[kSmartVolumeCount] = {L"off", L"quiet", L"balanced", L"loud"};
    return kKeys[static_cast<int>(volume)];
}

SmartVolume ParseSmartVolume(const std::wstring& key) {
    for (int i = 0; i < kSmartVolumeCount; ++i) {
        if (_wcsicmp(key.c_str(), SmartVolumeKey(static_cast<SmartVolume>(i))) == 0) {
            return static_cast<SmartVolume>(i);
        }
    }
    return SmartVolume::Off;
}

std::array<float, kParametricBands> GraphicEqGains(const ParametricEq& eq, float maxGain) {
    // Least squares over the audible range with one-octave peaking bands; the response of a peaking
    // band in dB grows almost in proportion to its gain, so a few corrections settle the overlap.
    // Each pass keeps the gains within the limit, so neighbours make up for a band that hits it.
    constexpr int kPoints = 48;
    constexpr double kRate = 48000.0;
    std::array<double, kPoints> frequencies{};
    std::array<double, kPoints> target{};
    ParametricEq shape = eq;
    shape.preamp = 0.0f;
    for (int i = 0; i < kPoints; ++i) {
        frequencies[i] = 25.0 * std::pow(18000.0 / 25.0, static_cast<double>(i) / (kPoints - 1));
        target[i] = ResponseDb(shape, frequencies[i], kRate);
    }
    std::array<std::array<double, kParametricBands>, kPoints> unit{};
    for (int j = 0; j < kParametricBands; ++j) {
        const EqBand band = Peak(kOctaveCenters[j], 1.0f, kOctaveQ);
        for (int i = 0; i < kPoints; ++i) {
            unit[i][j] = BandResponseDb(band, std::polar(1.0, -2.0 * kPi * frequencies[i] / kRate), kRate);
        }
    }
    std::array<std::array<double, kParametricBands>, kParametricBands> normal{};
    for (int j = 0; j < kParametricBands; ++j) {
        for (int k = 0; k < kParametricBands; ++k) {
            for (int i = 0; i < kPoints; ++i) {
                normal[j][k] += unit[i][j] * unit[i][k];
            }
        }
        normal[j][j] += 0.05;  // Keeps neighbouring bands from fighting each other.
    }
    std::array<double, kParametricBands> gains{};
    for (int pass = 0; pass < 6; ++pass) {
        std::array<double, kParametricBands> rhs{};
        for (int i = 0; i < kPoints; ++i) {
            double fitted = 0.0;
            for (int j = 0; j < kParametricBands; ++j) {
                fitted += BandResponseDb(Peak(kOctaveCenters[j], static_cast<float>(gains[j]), kOctaveQ),
                                         std::polar(1.0, -2.0 * kPi * frequencies[i] / kRate), kRate);
            }
            const double residual = target[i] - fitted;
            for (int j = 0; j < kParametricBands; ++j) {
                rhs[j] += unit[i][j] * residual;
            }
        }
        const auto step = Solve(normal, rhs);
        for (int j = 0; j < kParametricBands; ++j) {
            gains[j] = std::clamp(gains[j] + step[j], -static_cast<double>(maxGain), static_cast<double>(maxGain));
        }
    }
    std::array<float, kParametricBands> result{};
    for (int j = 0; j < kParametricBands; ++j) {
        result[j] = static_cast<float>(gains[j]);
    }
    return result;
}

std::vector<NamedEq> BuiltinEqPresets(EqChannel channel) {
    if (channel == EqChannel::Mic) {
        return {
            {L"Flat", FlatEq()},
            {L"Voice Clarity", Shape(-2.0f, {{0, HighPass(80)}, {3, Peak(300, -2.0f)}, {7, Peak(3500, 3.0f, 1.2f)},
                                             {9, HighShelf(9000, 1.5f)}})},
            {L"Broadcast", Shape(-3.0f, {{0, HighPass(70)}, {1, LowShelf(150, 3.0f)}, {3, Peak(350, -2.5f)},
                                         {7, Peak(4000, 3.0f)}, {8, Peak(7000, -1.5f, 2.0f)}})},
            {L"Bass Cut", Shape(0.0f, {{0, HighPass(120)}})},
            {L"Less Nasal", Shape(0.0f, {{0, HighPass(80)}, {5, Peak(1000, -3.0f, 1.4f)}})},
            {L"Less Sibilance", Shape(0.0f, {{0, HighPass(80)}, {8, Peak(7000, -4.0f, 2.0f)}})},
        };
    }
    if (channel == EqChannel::Chat) {
        return {
            {L"Flat", FlatEq()},
            {L"Clear Voice", Shape(-3.0f, {{0, HighPass(90)}, {3, Peak(250, -2.0f)}, {7, Peak(3000, 4.0f, 1.2f)},
                                           {8, Peak(6000, 2.0f)}})},
            {L"Warm Voice", Shape(-2.0f, {{2, LowShelf(200, 3.0f)}, {6, Peak(2500, 1.0f)}, {9, HighShelf(6000, -2.0f)}})},
            {L"Less Harsh", Shape(0.0f, {{7, Peak(3000, -2.0f, 1.2f)}, {9, HighShelf(5000, -4.0f)}})},
            {L"Radio", Shape(-4.0f, {{0, HighPass(300)}, {5, Peak(1500, 4.0f, 0.8f)}, {9, LowPass(3400)}})},
        };
    }
    return {
        {L"Flat", FlatEq()},
        {L"Bass Boost", Shape(-5.0f, {{0, LowShelf(110, 6.0f)}, {1, Peak(60, 1.5f)}})},
        {L"Footsteps", Shape(-4.0f, {{0, HighPass(40)}, {2, Peak(125, -3.0f)}, {3, Peak(250, -2.0f)},
                                     {6, Peak(2000, 3.0f, 1.2f)}, {7, Peak(4000, 5.0f, 1.2f)}, {8, Peak(8000, 2.0f)}})},
        {L"Treble Boost", Shape(-4.0f, {{9, HighShelf(6000, 5.0f)}})},
        {L"V-Shape", Shape(-4.0f, {{0, LowShelf(120, 5.0f)}, {5, Peak(1000, -3.0f, 0.8f)}, {9, HighShelf(8000, 5.0f)}})},
        {L"Clear Dialog", Shape(-2.0f, {{0, HighPass(60)}, {3, Peak(250, -2.0f)}, {6, Peak(2000, 3.0f)},
                                        {7, Peak(4000, 2.0f)}})},
        {L"Night", Shape(0.0f, {{0, LowShelf(120, -4.0f)}, {6, Peak(2000, 2.0f)}, {9, HighShelf(8000, -4.0f)}})},
    };
}

std::vector<NamedEq> SonarEqPresets(EqChannel channel) {
    std::vector<NamedEq> presets;
    PWSTR programData = nullptr;
    std::wstring path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &programData))) {
        path = std::wstring(programData) + L"\\SteelSeries\\GG\\sonar-db\\database.db";
    }
    CoTaskMemFree(programData);
    if (path.empty() || GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return presets;
    }
    // Read-only and immutable: no journal or lock files next to GG's database.
    std::string uri = "file:///" + Utf8(path) + "?immutable=1";
    std::replace(uri.begin(), uri.end(), '\\', '/');
    for (size_t space = uri.find(' '); space != std::string::npos; space = uri.find(' ', space)) {
        uri.replace(space, 1, "%20");
    }
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(uri.c_str(), &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_URI, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return presets;
    }
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT name, data FROM configs WHERE vad = ? ORDER BY name COLLATE NOCASE", -1,
                           &statement, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(statement, 1,
                         channel == EqChannel::Chat ? 2 : channel == EqChannel::Mic ? 3 : 1);
        while (sqlite3_step(statement) == SQLITE_ROW) {
            const auto* name = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
            const auto* data = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
            if (!name || !data) {
                continue;
            }
            const std::optional<JsonValue> json = ParseJson(data);
            if (!json) {
                continue;
            }
            const std::optional<ParametricEq> eq = FromSonar(*json, channel);
            std::wstring title = FromUtf8(name);
            if (!eq || title.empty()) {
                continue;
            }
            // Sonar keeps copies of some presets under the same name.
            const bool duplicate = std::any_of(presets.begin(), presets.end(), [&](const NamedEq& preset) {
                return _wcsicmp(preset.name.c_str(), title.c_str()) == 0;
            });
            if (!duplicate) {
                presets.push_back({std::move(title), *eq});
            }
        }
    }
    sqlite3_finalize(statement);
    sqlite3_close(db);
    return presets;
}

std::wstring ParametricPresetDirectory() {
    const std::wstring directory = PresetDirectory() + L"\\Parametric";
    CreateDirectoryW(directory.c_str(), nullptr);
    return directory;
}

std::optional<NamedEq> ReadParametricPreset(const std::wstring& path) {
    const std::optional<JsonValue> json = ParseJson(ReadFileUtf8(path));
    const JsonValue* bands = json ? json->Find(L"bands") : nullptr;
    if (!bands || bands->type != JsonValue::Type::Array) {
        return std::nullopt;
    }
    NamedEq preset;
    preset.eq = FlatEq();
    const JsonValue* name = json->Find(L"name");
    preset.name = name && !name->string.empty() ? name->string : FileNameOf(path);
    if (const size_t dot = preset.name.rfind(L".json"); dot != std::wstring::npos && !name) {
        preset.name.resize(dot);
    }
    const JsonValue* preamp = json->Find(L"preamp");
    preset.eq.preamp = std::clamp(static_cast<float>(preamp ? preamp->NumberOr(0) : 0), -kEqMaxGain, kEqMaxGain);
    for (size_t i = 0; i < bands->array.size() && i < kParametricBands; ++i) {
        const JsonValue& item = bands->array[i];
        EqBand band;
        if (const JsonValue* type = item.Find(L"type")) {
            band.type = ParseTypeKey(type->string).value_or(FilterType::Peaking);
        }
        band.frequency = static_cast<float>(item.Find(L"frequency") ? item.Find(L"frequency")->NumberOr(1000) : 1000);
        band.gain = static_cast<float>(item.Find(L"gain") ? item.Find(L"gain")->NumberOr(0) : 0);
        band.q = static_cast<float>(item.Find(L"q") ? item.Find(L"q")->NumberOr(kDefaultQ) : kDefaultQ);
        band.enabled = item.Find(L"enabled") ? item.Find(L"enabled")->BoolOr(true) : true;
        preset.eq.bands[i] = Sanitize(band);
    }
    return preset;
}

std::vector<NamedEq> LoadParametricPresets() {
    std::vector<NamedEq> presets;
    const std::wstring directory = ParametricPresetDirectory();
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW((directory + L"\\*.json").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) {
        return presets;
    }
    do {
        if (auto preset = ReadParametricPreset(directory + L"\\" + data.cFileName)) {
            presets.push_back(std::move(*preset));
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
    std::sort(presets.begin(), presets.end(),
              [](const NamedEq& a, const NamedEq& b) { return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0; });
    return presets;
}

bool WriteParametricPreset(const std::wstring& path, const NamedEq& preset) {
    std::string json = "{\n  \"app\": \"OpenChatMix\",\n  \"type\": \"parametric-equalizer\",\n  \"name\": \"" +
                       JsonEscape(preset.name) + "\",\n";
    char buffer[256];
    sprintf_s(buffer, "  \"preamp\": %.2f,\n  \"bands\": [\n", preset.eq.preamp);
    json += buffer;
    for (int i = 0; i < kParametricBands; ++i) {
        const EqBand& band = preset.eq.bands[i];
        sprintf_s(buffer, "    {\"type\": \"%s\", \"frequency\": %.1f, \"gain\": %.2f, \"q\": %.3f, \"enabled\": %s}%s\n",
                  Utf8(kTypeKeys[static_cast<int>(band.type)]).c_str(), band.frequency, band.gain, band.q,
                  band.enabled ? "true" : "false", i + 1 < kParametricBands ? "," : "");
        json += buffer;
    }
    json += "  ]\n}\n";
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const bool ok = WriteFile(file, json.data(), static_cast<DWORD>(json.size()), &written, nullptr) &&
                    written == json.size();
    CloseHandle(file);
    return ok;
}

std::wstring FormatBand(const EqBand& band) {
    wchar_t buffer[96];
    swprintf_s(buffer, L"%ls,%.1f,%.2f,%.3f,%d", kTypeKeys[static_cast<int>(band.type)], band.frequency, band.gain,
               band.q, band.enabled ? 1 : 0);
    return buffer;
}

std::optional<EqBand> ParseBand(const std::wstring& text) {
    wchar_t type[32] = {};
    EqBand band;
    int enabled = 1;
    if (swscanf_s(text.c_str(), L"%31[^,],%f,%f,%f,%d", type, static_cast<unsigned>(std::size(type)), &band.frequency,
                  &band.gain, &band.q, &enabled) < 4) {
        return std::nullopt;
    }
    const std::optional<FilterType> parsed = ParseTypeKey(type);
    if (!parsed) {
        return std::nullopt;
    }
    band.type = *parsed;
    band.enabled = enabled != 0;
    return Sanitize(band);
}
