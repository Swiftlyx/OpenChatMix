#include "parametric_eq.h"

#include <windows.h>

#include <shlobj.h>
#include <winsqlite/winsqlite3.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cwchar>
#include <cwctype>
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

// filter1..filter10 of GG's parametric equalizer, as Sonar and Engine store them.
ParametricEq FromGgFilters(const JsonValue& filters, float preamp, bool enabled) {
    ParametricEq eq = FlatEq();
    eq.preamp = preamp;
    for (int i = 0; i < kParametricBands; ++i) {
        const JsonValue* filter = filters.Find(L"filter" + std::to_wstring(i + 1));
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
        } else if (key == L"notch" || key == L"notchFilter") {
            // notchFilter in the databases of current GG releases.
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

// Sonar keeps one configuration per channel ("vad"): 1 game with a parametric equalizer, 2 chat with
// a ten-band graphic one, 3 the microphone, 4 media, 5 aux.
std::optional<ParametricEq> FromSonar(const JsonValue& data, EqChannel channel) {
    ParametricEq eq = FlatEq();
    // Game, media and aux always have the parametric equalizer; chat and the microphone have it in recent Sonar
    // versions and the ten-band graphic one before.
    if (const JsonValue* parametric = data.Find(L"parametricEQ")) {
        const float preamp =
            static_cast<float>(data.Find(L"generalGain") ? data.Find(L"generalGain")->NumberOr(0.0) : 0.0);
        const bool enabled = parametric->Find(L"enabled") ? parametric->Find(L"enabled")->BoolOr(true) : true;
        return FromGgFilters(*parametric, preamp, enabled);
    }
    const JsonValue* graphic = data.Find(L"equalizer");
    if (!graphic || channel == EqChannel::Game || channel == EqChannel::Media || channel == EqChannel::Aux) {
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

namespace {

// The first of GG's databases under ProgramData that exists, opened read-only and immutable, so no journal or
// lock files appear next to GG's own; null without one.
sqlite3* OpenGgDatabase(std::initializer_list<const wchar_t*> candidates) {
    PWSTR programData = nullptr;
    std::wstring path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &programData))) {
        for (const wchar_t* relative : candidates) {
            const std::wstring candidate = std::wstring(programData) + relative;
            if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
                path = candidate;
                break;
            }
        }
    }
    CoTaskMemFree(programData);
    if (path.empty()) {
        return nullptr;
    }
    std::string uri = "file:///" + Utf8(path) + "?immutable=1";
    std::replace(uri.begin(), uri.end(), '\\', '/');
    for (size_t space = uri.find(' '); space != std::string::npos; space = uri.find(' ', space)) {
        uri.replace(space, 1, "%20");
    }
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(uri.c_str(), &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_URI, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return nullptr;
    }
    return db;
}

// Current GG keeps Sonar's database under apps\sonar; older releases had it in sonar-db.
sqlite3* OpenSonarDatabase() {
    return OpenGgDatabase({L"\\SteelSeries\\GG\\apps\\sonar\\db\\database.db",
                           L"\\SteelSeries\\GG\\sonar-db\\database.db"});
}

std::wstring ColumnText(sqlite3_stmt* statement, int column) {
    const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(statement, column));
    return text ? FromUtf8(text) : std::wstring();
}

// Lowercase letters and digits only, so "Counter-Strike 2" and "counterstrike2" meet.
std::wstring MatchKey(const std::wstring& text) {
    std::wstring key;
    for (const wchar_t c : text) {
        if (std::iswalnum(c)) {
            key += static_cast<wchar_t>(std::towlower(c));
        }
    }
    return key;
}

// GG's form of a path in its game library: lowercase, forward slashes, no spaces.
std::wstring LibraryPathKey(const std::wstring& path) {
    std::wstring key;
    for (const wchar_t c : path) {
        if (c != L' ') {
            key += c == L'\\' ? L'/' : static_cast<wchar_t>(std::towlower(c));
        }
    }
    return key;
}

// The words of a name or file name, lowercase, split at spaces and signs, between a lowercase letter and an
// uppercase one and between letters and digits: "FortniteClient-Win64" gives fortnite, client, win, 64.
std::vector<std::wstring> NameWords(const std::wstring& text) {
    std::vector<std::wstring> words;
    std::wstring word;
    auto flush = [&] {
        if (!word.empty()) {
            words.push_back(word);
            word.clear();
        }
    };
    for (size_t i = 0; i < text.size(); ++i) {
        const wchar_t c = text[i];
        if (!std::iswalnum(c)) {
            flush();
            continue;
        }
        if (i > 0 && std::iswalnum(text[i - 1]) &&
            ((std::iswupper(c) && std::iswlower(text[i - 1])) || (std::iswdigit(c) != std::iswdigit(text[i - 1])))) {
            flush();
        }
        word += static_cast<wchar_t>(std::towlower(c));
    }
    flush();
    return words;
}

// GG names some games by their usual short forms ("COD: Black Ops 6", "CS2 Pro Preset"); the long forms are
// shortened the same way before names are compared.
std::wstring Abbreviated(const std::wstring& name) {
    std::wstring text = name;
    std::transform(text.begin(), text.end(), text.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    constexpr std::pair<const wchar_t*, const wchar_t*> kShort[] = {
        {L"call of duty", L"cod"}, {L"counter-strike", L"cs"}, {L"counter strike", L"cs"},
        {L"grand theft auto", L"gta"}};
    for (const auto& [full, abbreviation] : kShort) {
        for (size_t at = text.find(full); at != std::wstring::npos; at = text.find(full, at)) {
            text.replace(at, std::wcslen(full), abbreviation);
        }
    }
    return text;
}

// A preset's name for comparing: shortened like the game's, without the "Pro Preset" GG adds to some.
std::wstring PresetKey(const std::wstring& name) {
    std::wstring key = MatchKey(Abbreviated(name));
    for (const std::wstring suffix : {L"propreset", L"preset"}) {
        if (key.size() > suffix.size() && key.compare(key.size() - suffix.size(), suffix.size(), suffix) == 0) {
            key.resize(key.size() - suffix.size());
            break;
        }
    }
    return key;
}

// The words of a name that tell a game apart: four letters or more, without those every launcher, edition or
// preset name uses.
std::vector<std::wstring> SignificantWords(const std::wstring& text) {
    constexpr const wchar_t* kGeneric[] = {L"game",       L"games",      L"launcher", L"client",  L"shipping",
                                           L"edition",    L"online",     L"remastered", L"definitive", L"preset",
                                           L"steam",      L"windows",    L"retail",   L"release", L"application"};
    std::vector<std::wstring> words;
    for (std::wstring& word : NameWords(text)) {
        const bool generic = std::any_of(std::begin(kGeneric), std::end(kGeneric),
                                         [&](const wchar_t* known) { return word == known; });
        if (word.size() >= 4 && !generic && std::find(words.begin(), words.end(), word) == words.end()) {
            words.push_back(std::move(word));
        }
    }
    return words;
}

std::wstring FileStem(const std::wstring& path) {
    std::wstring name = FileNameOf(path);
    if (const size_t dot = name.rfind(L'.'); dot != std::wstring::npos) {
        name.resize(dot);
    }
    return name;
}

}  // namespace

std::vector<NamedEq> SonarEqPresets(EqChannel channel) {
    std::vector<NamedEq> presets;
    sqlite3* db = OpenSonarDatabase();
    if (!db) {
        return presets;
    }
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT name, data FROM configs WHERE vad = ? ORDER BY name COLLATE NOCASE", -1,
                           &statement, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(statement, 1,
                         channel == EqChannel::Chat    ? 2
                         : channel == EqChannel::Mic   ? 3
                         : channel == EqChannel::Media ? 4
                         : channel == EqChannel::Aux   ? 5
                                                       : 1);
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

    // Engine keeps game presets of its own for headsets with a parametric equalizer: mostly Sonar's, plus some
    // for console and mobile games. Mode 3 is the game list, 4 the microphone's; others repeat headset presets.
    const int engineMode = channel == EqChannel::Game ? 3 : channel == EqChannel::Mic ? 4 : 0;
    if (sqlite3* engine = engineMode ? OpenGgDatabase({L"\\SteelSeries\\GG\\apps\\engine\\db\\database.db"})
                                     : nullptr) {
        statement = nullptr;
        if (sqlite3_prepare_v2(engine, "SELECT display_name, eq_preset_data FROM device_game_eq_presets "
                                       "WHERE supported_mode = ?", -1, &statement, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(statement, 1, engineMode);
            while (sqlite3_step(statement) == SQLITE_ROW) {
                std::wstring title = ColumnText(statement, 0);
                const auto* data = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
                const std::optional<JsonValue> json = data ? ParseJson(data) : std::nullopt;
                const bool known = std::any_of(presets.begin(), presets.end(), [&](const NamedEq& preset) {
                    return _wcsicmp(preset.name.c_str(), title.c_str()) == 0;
                });
                if (json && !title.empty() && !known) {
                    presets.push_back({std::move(title), FromGgFilters(*json, 0.0f, true)});
                }
            }
        }
        sqlite3_finalize(statement);
        sqlite3_close(engine);
        std::stable_sort(presets.begin(), presets.end(), [](const NamedEq& a, const NamedEq& b) {
            return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
        });
    }
    return presets;
}

std::vector<std::wstring> SuggestedEqPresets(const std::wstring& exePath, const std::wstring& appName) {
    std::vector<std::wstring> names;
    auto add = [&names](const std::wstring& name) {
        if (!name.empty() && std::none_of(names.begin(), names.end(), [&](const std::wstring& known) {
                return _wcsicmp(known.c_str(), name.c_str()) == 0;
            })) {
            names.push_back(name);
        }
    };

    // GG's game library knows where a game is installed and under which store IDs ("1_<Steam app>"); Sonar
    // links its game presets to those IDs.
    std::vector<std::string> gameIds;
    if (sqlite3* gg = OpenGgDatabase({L"\\SteelSeries\\GG\\db\\database.db"}); gg && !exePath.empty()) {
        const std::wstring pathKey = LibraryPathKey(exePath);
        const std::wstring fileKey = LibraryPathKey(FileNameOf(exePath));
        sqlite3_stmt* statement = nullptr;
        if (sqlite3_prepare_v2(gg, "SELECT unique_identifier, exe_path FROM library_scan WHERE uninstalled = 0", -1,
                               &statement, nullptr) == SQLITE_OK) {
            while (sqlite3_step(statement) == SQLITE_ROW) {
                const std::wstring library = LibraryPathKey(ColumnText(statement, 1));
                const size_t slash = library.rfind(L'/');
                const std::wstring libraryFile = slash == std::wstring::npos ? library : library.substr(slash + 1);
                if (library == pathKey || (!fileKey.empty() && libraryFile == fileKey)) {
                    if (const auto* id = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0))) {
                        gameIds.emplace_back(id);
                    }
                }
            }
        }
        sqlite3_finalize(statement);
        sqlite3_close(gg);
    }
    if (sqlite3* sonar = gameIds.empty() ? nullptr : OpenSonarDatabase()) {
        sqlite3_stmt* statement = nullptr;
        if (sqlite3_prepare_v2(sonar,
                               "SELECT name, game_ids FROM configs WHERE vad = 1 ORDER BY name COLLATE NOCASE", -1,
                               &statement, nullptr) == SQLITE_OK) {
            while (sqlite3_step(statement) == SQLITE_ROW) {
                const auto* ids = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
                std::string list = ids ? std::string(",") + ids + "," : std::string();
                for (const std::string& id : gameIds) {
                    if (list.find("," + id + ",") != std::string::npos) {
                        add(ColumnText(statement, 0));
                        break;
                    }
                }
            }
        }
        sqlite3_finalize(statement);
        sqlite3_close(sonar);
    }

    // Then by name: the same name, one name within the other (five letters or more: "Minecraft" in "Minecraft for
    // Windows"), or every significant word of the game's name in a preset's ("fortnite" of
    // FortniteClient-Win64-Shipping). One short shared word ("black", "dark") is not enough.
    const std::wstring gameKey = MatchKey(Abbreviated(appName.empty() ? FileStem(exePath) : appName));
    std::vector<std::wstring> gameWords = SignificantWords(Abbreviated(appName));
    if (gameWords.empty()) {
        gameWords = SignificantWords(FileStem(exePath));
    }
    const bool wordsTell = gameWords.size() >= 2 || (gameWords.size() == 1 && gameWords[0].size() >= 6);
    std::vector<std::wstring> close;
    for (const NamedEq& preset : SonarEqPresets(EqChannel::Game)) {
        const std::wstring key = PresetKey(preset.name);
        if (key.empty() || gameKey.empty()) {
            continue;
        }
        const bool contained = (key.size() >= 5 && gameKey.find(key) != std::wstring::npos) ||
                               (gameKey.size() >= 5 && key.find(gameKey) != std::wstring::npos);
        const bool words =
            wordsTell && std::all_of(gameWords.begin(), gameWords.end(),
                                     [&](const std::wstring& word) { return key.find(word) != std::wstring::npos; });
        if (key == gameKey) {
            add(preset.name);
        } else if (contained || words) {
            close.push_back(preset.name);
        }
    }
    for (const std::wstring& name : close) {
        add(name);
    }
    return names;
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
