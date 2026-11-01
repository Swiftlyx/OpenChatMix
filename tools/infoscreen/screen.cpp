// Draws everything on one 128x64 screen: the track at the top, the PC's figures in two columns of cells, the weather
// at the bottom. Figures use a built-in pixel font with digits of one width, so values do not shift as they change;
// the track and the place use GDI's text, which covers every script. Against burn-in the whole picture drifts by up
// to 3 pixels sideways and 2 up and down, one pixel at a time, and fans that do not fit take turns. A fan's icon
// turns while the fan spins, faster as it spins faster.

#include "infoscreen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <map>
#include <utility>

namespace {

constexpr int kContentWidth = 120;  // Leaves room for the drift on both sides.
constexpr int kContentLeft = (kScreenWidth - kContentWidth) / 2;
constexpr int kDriftX = 3;
constexpr int kDriftY = 2;
constexpr double kDriftPeriodXMs = 170000.0;
constexpr double kDriftPeriodYMs = 110000.0;
constexpr int kColumnGap = 8;
constexpr int kCellWidth = (kContentWidth - kColumnGap) / 2;
// A cell's second value ends at its right edge and the first this far before it, where it fits, so values line up
// in columns.
constexpr int kFirstValueInset = 22;
constexpr int kValueGap = 2;  // The least room between a label and its values where they are tight.
constexpr int kGlyphHeight = 7;
constexpr int kStatPitch = kGlyphHeight + 3;
constexpr int kTextRowGap = 3;
constexpr int kIconSize = 9;
constexpr int kIconGap = 3;
constexpr ULONGLONG kScrollPauseMs = 2000;
constexpr ULONGLONG kScrollStepMs = 70;  // One pixel per step.
constexpr int kScrollGap = 32;           // Between the end of the track and its start again.
constexpr ULONGLONG kFanPageMs = 4000;
constexpr wchar_t kFace[] = L"Tahoma";
constexpr int kFontPixels = 11;
constexpr int kFanSize = 7;
constexpr int kFanBlades = 3;
constexpr double kFanBladeDegrees = 45.0;
// How fast a fan's icon turns: from the slowest to the fastest speed, which it reaches at kFanFastRpm. Frames come
// about four times a second, so a turn stays well under half the 120 degrees between blades per frame.
constexpr double kFanSlowDegreesPerSecond = 60.0;
constexpr double kFanFastDegreesPerSecond = 160.0;
constexpr double kFanFastRpm = 2500.0;

struct Glyph {
    char ch;
    const char* rows[kGlyphHeight];
};

// '#' lit, '.' dark; every row of a glyph is as wide as its first.
constexpr Glyph kGlyphs[] = {
    {'0', {".##.", "#..#", "#..#", "#..#", "#..#", "#..#", ".##."}},
    {'1', {"..#.", ".##.", "..#.", "..#.", "..#.", "..#.", ".###"}},
    {'2', {".##.", "#..#", "...#", "..#.", ".#..", "#...", "####"}},
    {'3', {"###.", "...#", "...#", ".##.", "...#", "...#", "###."}},
    {'4', {"...#", "..##", ".#.#", "#..#", "####", "...#", "...#"}},
    {'5', {"####", "#...", "###.", "...#", "...#", "#..#", ".##."}},
    {'6', {".##.", "#...", "#...", "###.", "#..#", "#..#", ".##."}},
    {'7', {"####", "...#", "..#.", "..#.", ".#..", ".#..", ".#.."}},
    {'8', {".##.", "#..#", "#..#", ".##.", "#..#", "#..#", ".##."}},
    {'9', {".##.", "#..#", "#..#", ".###", "...#", "...#", ".##."}},
    {'A', {".##.", "#..#", "#..#", "####", "#..#", "#..#", "#..#"}},
    {'B', {"###.", "#..#", "#..#", "###.", "#..#", "#..#", "###."}},
    {'C', {".##.", "#..#", "#...", "#...", "#...", "#..#", ".##."}},
    {'G', {".##.", "#..#", "#...", "#.##", "#..#", "#..#", ".###"}},
    {'M', {"#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"}},
    {'P', {"###.", "#..#", "#..#", "###.", "#...", "#...", "#..."}},
    {'R', {"###.", "#..#", "#..#", "###.", "#.#.", "#..#", "#..#"}},
    {'U', {"#..#", "#..#", "#..#", "#..#", "#..#", "#..#", ".##."}},
    {' ', {"..", "..", "..", "..", "..", "..", ".."}},
    {'.', {".", ".", ".", ".", ".", ".", "#"}},
    {'%', {"##..#", "##..#", "...#.", "..#..", ".#...", "#..##", "#..##"}},
    {'\xB0', {".#.", "#.#", ".#.", "...", "...", "...", "..."}},  // Degree sign.
    {'(', {".#", "#.", "#.", "#.", "#.", "#.", ".#"}},
    {')', {"#.", ".#", ".#", ".#", ".#", ".#", "#."}},
    {'-', {"...", "...", "...", "###", "...", "...", "..."}},
    {':', {".", ".", "#", ".", ".", "#", "."}},
    {'F', {"####", "#...", "#...", "###.", "#...", "#...", "#..."}},
    {'N', {"#..#", "##.#", "##.#", "#.##", "#.##", "#..#", "#..#"}},
};

// Icons in place of the names CPU, GPU and RAM, as tall as the figures.
constexpr const char* kCpuIcon[kGlyphHeight] = {"..#.#..", ".#####.", "##...##", ".#.#.#.", "##...##", ".#####.",
                                                "..#.#.."};
constexpr const char* kGpuIcon[kGlyphHeight] = {"#........", "#########", "#.###...#", "#.#.#.#.#", "#.###...#",
                                                "#########", "#.#.#.#.."};
constexpr const char* kRamIcon[kGlyphHeight] = {".........", "#########", "#.#.#.#.#", "#.#.#.#.#", "#########",
                                                ".#.#.#.#.", "........."};

// 9 x 9 icons beside the track and the weather.
constexpr const char* kNoteIcon[kIconSize] = {"...#####.", "...#####.", "...#...#.", "...#...#.", "...#...#.",
                                              ".###.###.", "####.####", "####.####", ".##...##."};
constexpr const char* kSunIcon[kIconSize] = {"....#....", ".#.....#.", "...###...", "..#####..", "#.#####.#",
                                             "..#####..", "...###...", ".#.....#.", "....#...."};
constexpr const char* kMoonIcon[kIconSize] = {"...###...", ".####....", ".###.....", "###......", "###......",
                                              "###......", ".###....#", ".######..", "...####.."};
constexpr const char* kPartlyIcon[kIconSize] = {".#..#....", "...###...", "#.#####..", "..##...#.", ".#.....#.",
                                                "#.......#", "#.......#", ".#######.", "........."};
constexpr const char* kCloudIcon[kIconSize] = {".........", "...###...", "..#...#..", ".#.....##", "#........",
                                               "#.......#", "#.......#", ".#######.", "........."};
constexpr const char* kFogIcon[kIconSize] = {".........", "#######..", ".........", "..#######", ".........",
                                             "#######..", ".........", "..#######", "........."};
constexpr const char* kDrizzleIcon[kIconSize] = {"...###...", "..#...##.", ".#......#", "#.......#", ".#######.",
                                                 ".........", "..#...#..", ".........", "....#...#"};
constexpr const char* kRainIcon[kIconSize] = {"...###...", "..#...##.", ".#......#", "#.......#", ".#######.",
                                              ".#..#..#.", "#..#..#..", ".........", ".#..#..#."};
constexpr const char* kSnowIcon[kIconSize] = {"....#....", ".#..#..#.", "..#.#.#..", "...###...", "#########",
                                              "...###...", "..#.#.#..", ".#..#..#.", "....#...."};
constexpr const char* kThunderIcon[kIconSize] = {"...###...", "..#...##.", ".#......#", "#.......#", ".###.###.",
                                                 "....#....", "...##....", "....##...", ".....#..."};

const Glyph* Find(char ch) {
    for (const Glyph& glyph : kGlyphs) {
        if (glyph.ch == ch) {
            return &glyph;
        }
    }
    return nullptr;
}

int GlyphWidth(const Glyph& glyph) {
    return static_cast<int>(strlen(glyph.rows[0]));
}

int TextWidth(const std::string& text) {
    int width = 0;
    for (const char ch : text) {
        if (const Glyph* glyph = Find(ch)) {
            width += GlyphWidth(*glyph) + 1;
        }
    }
    return width > 0 ? width - 1 : 0;
}

void DrawPixelText(Frame& frame, const std::string& text, int left, int top) {
    for (const char ch : text) {
        const Glyph* glyph = Find(ch);
        if (!glyph) {
            continue;
        }
        const int width = GlyphWidth(*glyph);
        for (int y = 0; y < kGlyphHeight; ++y) {
            for (int x = 0; x < width; ++x) {
                if (glyph->rows[y][x] == '#') {
                    frame.Set(left + x, top + y);
                }
            }
        }
        left += width + 1;
    }
}

void DrawRight(Frame& frame, const std::string& text, int right, int top) {
    DrawPixelText(frame, text, right - TextWidth(text), top);
}

void DrawIcon(Frame& frame, const char* const* rows, int left, int top) {
    for (int y = 0; y < kIconSize; ++y) {
        for (int x = 0; x < kIconSize; ++x) {
            if (rows[y][x] == '#') {
                frame.Set(left + x, top + y);
            }
        }
    }
}

const char* const* SkyIcon(const Weather& weather) {
    switch (weather.sky) {
    case Sky::Clear: return weather.day ? kSunIcon : kMoonIcon;
    case Sky::PartlyCloudy: return kPartlyIcon;
    case Sky::Cloudy: return kCloudIcon;
    case Sky::Fog: return kFogIcon;
    case Sky::Drizzle: return kDrizzleIcon;
    case Sky::Rain: return kRainIcon;
    case Sky::Snow: return kSnowIcon;
    case Sky::Thunder: return kThunderIcon;
    }
    return kCloudIcon;
}

// A line of GDI text in 1-bit pixels, as tall as the font's line.
struct TextBits {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;

    // Draws the columns from..to of the text with its left edge at left; columns outside clipLeft..clipRight stay off.
    void Draw(Frame& frame, int left, int top, int clipLeft, int clipRight) const {
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const int column = left + x;
                if (column >= clipLeft && column < clipRight && pixels[y * width + x]) {
                    frame.Set(column, top + y);
                }
            }
        }
    }
};

// GDI text without antialiasing; GDI's font linking covers scripts the face lacks.
class PixelFont {
public:
    explicit PixelFont(int weight) {
        dc_ = CreateCompatibleDC(nullptr);
        font_ = CreateFontW(-kFontPixels, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY, DEFAULT_PITCH, kFace);
        if (dc_ && font_) {
            SelectObject(dc_, font_);
            TEXTMETRICW metrics{};
            GetTextMetricsW(dc_, &metrics);
            height_ = metrics.tmHeight;
        }
    }
    PixelFont(const PixelFont&) = delete;
    PixelFont& operator=(const PixelFont&) = delete;

    int Height() const { return height_; }

    int Width(const std::wstring& text) const {
        SIZE size{};
        if (!dc_ || text.empty() || !GetTextExtentPoint32W(dc_, text.c_str(), static_cast<int>(text.size()), &size)) {
            return 0;
        }
        return size.cx;
    }

    // The text cut to fit width, with an ellipsis when it had to be cut.
    std::wstring Fit(const std::wstring& text, int width) const {
        if (Width(text) <= width) {
            return text;
        }
        std::wstring cut = text;
        while (!cut.empty() && Width(cut + L"…") > width) {
            cut.pop_back();
            if (!cut.empty() && IS_HIGH_SURROGATE(cut.back())) {
                cut.pop_back();
            }
        }
        return cut + L"…";
    }

    const TextBits& Render(const std::wstring& text) {
        if (const auto found = cache_.find(text); found != cache_.end()) {
            return found->second;
        }
        // Only the texts of the last few frames stay.
        if (cache_.size() > 16) {
            cache_.clear();
        }
        TextBits& bits = cache_[text];
        const int width = Width(text);
        if (width <= 0 || height_ <= 0) {
            return bits;
        }
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height_;  // Top-down rows.
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* data = nullptr;
        HBITMAP bitmap = CreateDIBSection(dc_, &info, DIB_RGB_COLORS, &data, nullptr, 0);
        if (!bitmap) {
            return bits;
        }
        HGDIOBJ previous = SelectObject(dc_, bitmap);
        std::memset(data, 0, static_cast<size_t>(width) * height_ * 4);
        SetBkMode(dc_, TRANSPARENT);
        SetTextColor(dc_, RGB(255, 255, 255));
        TextOutW(dc_, 0, 0, text.c_str(), static_cast<int>(text.size()));
        GdiFlush();
        const auto* source = static_cast<const uint32_t*>(data);
        bits.width = width;
        bits.height = height_;
        bits.pixels.resize(static_cast<size_t>(width) * height_);
        for (size_t i = 0; i < bits.pixels.size(); ++i) {
            bits.pixels[i] = (source[i] & 0xFF) > 127 ? 1 : 0;
        }
        SelectObject(dc_, previous);
        DeleteObject(bitmap);
        return bits;
    }

private:
    HDC dc_ = nullptr;
    HFONT font_ = nullptr;
    int height_ = 13;
    std::map<std::wstring, TextBits> cache_;
};

PixelFont& Regular() {
    static PixelFont font(FW_NORMAL);
    return font;
}

PixelFont& Bold() {
    static PixelFont font(FW_BOLD);
    return font;
}

std::string Whole(double value) {
    return std::to_string(static_cast<long long>(std::lround(value)));
}

enum class CellIcon { Cpu, Gpu, Ram, Fan };

// One cell of the figures: a label and up to two values. The label is a name with a colon, or an icon with a tag such
// as a fan's number.
struct Cell {
    std::string name;  // E.g. "CPU", "FAN1", "GPU FAN".
    CellIcon icon = CellIcon::Cpu;
    std::string tag;   // After the icon: a fan's number or "GPU".
    std::string first;
    std::string second;
    double spin = 0.0;  // Degrees a second a fan's icon turns; 0 while the fan stands still.
};

// Three blades around a hub, turned by angle degrees.
void DrawFan(Frame& frame, int left, int top, double angle) {
    const double center = (kFanSize - 1) / 2.0;
    for (int y = 0; y < kFanSize; ++y) {
        for (int x = 0; x < kFanSize; ++x) {
            const double dx = x - center;
            const double dy = y - center;
            const double radius = std::hypot(dx, dy);
            bool lit = radius <= 0.9;
            if (!lit && radius <= center + 0.6) {
                const double direction = std::atan2(dy, dx) * 180.0 / 3.14159265358979323846;
                for (int blade = 0; blade < kFanBlades && !lit; ++blade) {
                    const double offset =
                        std::remainder(direction - angle - blade * 360.0 / kFanBlades, 360.0);
                    lit = offset >= -kFanBladeDegrees * 0.35 && offset <= kFanBladeDegrees;
                }
            }
            if (lit) {
                frame.Set(left + x, top + y);
            }
        }
    }
}

double SpinOf(const Fan& fan) {
    double share = 0.0;
    if (fan.rpm && *fan.rpm > 0.0) {
        share = *fan.rpm / kFanFastRpm;
    } else if (fan.percent && *fan.percent > 0.0) {
        share = *fan.percent / 100.0;
    } else {
        return 0.0;
    }
    return kFanSlowDegreesPerSecond + (kFanFastDegreesPerSecond - kFanSlowDegreesPerSecond) * std::min(1.0, share);
}

std::string FanValue(const Fan& fan, FanFormat format) {
    const std::string rpm = fan.rpm ? Whole(*fan.rpm) : std::string{};
    const std::string percent = fan.percent ? Whole(*fan.percent) + "%" : std::string{};
    switch (format) {
    case FanFormat::Percent: return percent.empty() ? rpm : percent;
    case FanFormat::Rpm: return rpm.empty() ? percent : rpm;
    case FanFormat::Both:
        if (rpm.empty() || percent.empty()) {
            return rpm.empty() ? percent : rpm;
        }
        return rpm + " (" + percent + ")";
    }
    return rpm;
}

bool SameText(wchar_t a, wchar_t b) {
    return towlower(a) == towlower(b);
}

// A fan the user picked: by its whole name, or by a part of it as typed in earlier versions.
const Fan* FindFan(const std::vector<Fan>& fans, const std::wstring& wanted) {
    for (const Fan& fan : fans) {
        if (std::equal(fan.name.begin(), fan.name.end(), wanted.begin(), wanted.end(), SameText)) {
            return &fan;
        }
    }
    for (const Fan& fan : fans) {
        if (!wanted.empty() &&
            std::search(fan.name.begin(), fan.name.end(), wanted.begin(), wanted.end(), SameText) != fan.name.end()) {
            return &fan;
        }
    }
    return nullptr;
}

// Fans as the user ticked them, else the GPU's and every other fan that spins. The GPU's are labelled GPU, a fan whose
// header is known by its tag (CPU, a chassis header's number, PUMP), the others numbered in that order.
std::vector<Cell> FanCells(const Options& options, const std::vector<Fan>& fans) {
    std::vector<const Fan*> shown;
    if (!options.fanNames.empty()) {
        for (const std::wstring& wanted : options.fanNames) {
            const Fan* fan = FindFan(fans, wanted);
            if (fan && std::find(shown.begin(), shown.end(), fan) == shown.end()) {
                shown.push_back(fan);
            }
        }
    } else {
        // The GPU's fans turn together, so one of them stands for all; it shows also while it rests.
        bool gpu = false;
        for (const Fan& fan : fans) {
            if (fan.gpu ? !std::exchange(gpu, true) : fan.rpm && *fan.rpm > 0.0) {
                shown.push_back(&fan);
            }
        }
    }
    const auto gpuFans = std::count_if(shown.begin(), shown.end(), [](const Fan* fan) { return fan->gpu; });
    std::vector<Cell> cells;
    int number = 0;
    int gpuNumber = 0;
    for (const Fan* fan : shown) {
        Cell cell;
        cell.icon = CellIcon::Fan;
        if (fan->gpu) {
            cell.tag = gpuFans > 1 ? "GPU" + Whole(++gpuNumber) : "GPU";
            cell.name = gpuFans > 1 ? "GPU FAN" + Whole(gpuNumber) : "GPU FAN";
        } else if (!fan->tag.empty()) {
            cell.tag = fan->tag;
            cell.name = fan->tag == "CPU" ? "CPU FAN" : fan->tag == "PUMP" ? "PUMP" : "FAN" + fan->tag;
        } else {
            cell.tag = Whole(++number % 100);
            cell.name = "FAN" + cell.tag;
        }
        cell.second = FanValue(*fan, options.fanFormat);
        cell.spin = SpinOf(*fan);
        if (!cell.second.empty()) {
            cells.push_back(std::move(cell));
        }
    }
    return cells;
}

int IconWidth(CellIcon icon) {
    switch (icon) {
    case CellIcon::Cpu: return static_cast<int>(strlen(kCpuIcon[0]));
    case CellIcon::Gpu: return static_cast<int>(strlen(kGpuIcon[0]));
    case CellIcon::Ram: return static_cast<int>(strlen(kRamIcon[0]));
    case CellIcon::Fan: return kFanSize;
    }
    return 0;
}

int LabelWidth(const Cell& cell, bool icons) {
    if (!icons) {
        return TextWidth(cell.name + ":");
    }
    return IconWidth(cell.icon) + (cell.tag.empty() ? 0 : 1 + TextWidth(cell.tag));
}

// How wide the cell is with its values packed beside the label.
int NaturalWidth(const Cell& cell, bool icons) {
    const int first = cell.first.empty() ? 0 : TextWidth(cell.first) + kValueGap;
    return LabelWidth(cell, icons) + kValueGap + first + TextWidth(cell.second);
}

void DrawCell(Frame& frame, const Cell& cell, bool icons, int left, int top, int width, ULONGLONG elapsedMs) {
    if (!icons) {
        DrawPixelText(frame, cell.name + ":", left, top);
    } else {
        const char* const* rows = cell.icon == CellIcon::Cpu   ? kCpuIcon
                                  : cell.icon == CellIcon::Gpu ? kGpuIcon
                                  : cell.icon == CellIcon::Ram ? kRamIcon
                                                               : nullptr;
        if (rows) {
            for (int y = 0; y < kGlyphHeight; ++y) {
                for (int x = 0; rows[y][x]; ++x) {
                    if (rows[y][x] == '#') {
                        frame.Set(left + x, top + y);
                    }
                }
            }
        } else {
            DrawFan(frame, left, top, std::fmod(cell.spin * static_cast<double>(elapsedMs) / 1000.0, 360.0));
        }
        if (!cell.tag.empty()) {
            DrawPixelText(frame, cell.tag, left + IconWidth(cell.icon) + 1, top);
        }
    }
    const int secondWidth = TextWidth(cell.second);
    if (!cell.first.empty()) {
        const int firstWidth = TextWidth(cell.first);
        const int fewest = LabelWidth(cell, icons) + kValueGap + firstWidth;
        const int most = width - secondWidth - kValueGap;
        DrawRight(frame, cell.first, left + std::clamp(width - kFirstValueInset, fewest, std::max(fewest, most)), top);
    }
    DrawRight(frame, cell.second, left + width, top);
}

// Where a cell goes: its row and column, and whether it takes both columns.
struct Slot {
    int row = 0;
    int column = 0;
    bool wide = false;
};

// Places cells in rows of two from row, column on, a cell too wide for one column taking a row of its own; stops at
// the first that does not fit within rows. Returns how many were placed.
size_t PackCells(const std::vector<Cell>& cells, size_t from, bool icons, int rows, int& row, int& column,
                 std::vector<Slot>* places) {
    size_t placed = 0;
    for (size_t i = from; i < cells.size(); ++i) {
        const bool wide = NaturalWidth(cells[i], icons) > kCellWidth;
        int r = row;
        int c = column;
        if (wide && c != 0) {
            ++r;
            c = 0;
        }
        if (r >= rows) {
            break;
        }
        if (places) {
            places->push_back({r, c, wide});
        }
        ++placed;
        if (wide || c == 1) {
            row = r + 1;
            column = 0;
        } else {
            row = r;
            column = 1;
        }
    }
    return placed;
}

}  // namespace

std::wstring Utf16(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int count = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(count, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), count);
    return wide;
}

std::string Utf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int count =
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string narrow(count, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), narrow.data(), count, nullptr, nullptr);
    return narrow;
}

void DrawScreen(const ScreenData& data, ULONGLONG elapsedMs, Frame& frame, bool& scrolling) {
    frame = Frame{};
    scrolling = false;
    const Options& options = data.options;
    const Readings& readings = data.readings;
    const double t = static_cast<double>(elapsedMs);
    const double pi = 3.14159265358979323846;
    const int dx = static_cast<int>(std::lround(kDriftX * std::sin(2.0 * pi * t / kDriftPeriodXMs)));
    const int dy = static_cast<int>(std::lround(kDriftY * std::sin(2.0 * pi * t / kDriftPeriodYMs + 1.0)));

    // The figures, in the order they are listed in the panel.
    std::vector<Cell> cells;
    auto addProcessor = [&](const char* name, CellIcon icon, bool wantTemp, const std::optional<double>& temp,
                            bool wantLoad, const std::optional<double>& load) {
        if (!(wantTemp && temp) && !(wantLoad && load)) {
            return;
        }
        Cell cell;
        cell.name = name;
        cell.icon = icon;
        // Parts are in degrees Celsius whatever the weather's unit, as hardware is everywhere.
        const std::string degrees = wantTemp && temp ? Whole(*temp) + "\xB0" : "";
        const std::string percent = wantLoad && load ? Whole(*load) + "%" : "";
        if (!degrees.empty() && !percent.empty()) {
            cell.first = degrees;
            cell.second = percent;
        } else {
            cell.second = degrees.empty() ? percent : degrees;
        }
        cells.push_back(cell);
    };
    addProcessor("CPU", CellIcon::Cpu, options.cpuTemp, readings.cpuTemp, options.cpuLoad, readings.cpuLoad);
    addProcessor("GPU", CellIcon::Gpu, options.gpuTemp, readings.gpuTemp, options.gpuLoad, readings.gpuLoad);
    if (options.ram && readings.ramUsedGb && readings.ramPercent) {
        char used[16];
        if (*readings.ramUsedGb < 9.95) {
            snprintf(used, sizeof(used), "%.1fGB", *readings.ramUsedGb);
        } else {
            snprintf(used, sizeof(used), "%lldGB", static_cast<long long>(std::lround(*readings.ramUsedGb)));
        }
        Cell cell;
        cell.name = "RAM";
        cell.icon = CellIcon::Ram;
        const std::string percent = Whole(*readings.ramPercent) + "%";
        cell.first = options.ramFormat == RamFormat::Both ? used : "";
        cell.second = options.ramFormat == RamFormat::Gigabytes ? used : percent;
        cells.push_back(cell);
    }
    const size_t fixedCells = cells.size();
    if (options.fans) {
        std::vector<Cell> fans = FanCells(options, readings.fans);
        cells.insert(cells.end(), fans.begin(), fans.end());
    }
    // Values too wide beside their label in one column, e.g. "1180 (42%)", drop their spaces, all alike; what still
    // does not fit takes both columns.
    const bool icons = options.icons;
    const bool tight = std::any_of(cells.begin(), cells.end(), [&](const Cell& cell) {
        return NaturalWidth(cell, icons) > kCellWidth && cell.second.find(' ') != std::string::npos;
    });
    if (tight) {
        for (Cell& cell : cells) {
            std::erase(cell.second, ' ');
        }
    }
    // A fan too wide for the free half of a row lets the next narrow fan take that half, so no half row stays empty.
    for (size_t i = 0, column = 0; i < cells.size(); ++i) {
        bool wide = NaturalWidth(cells[i], icons) > kCellWidth;
        if (wide && column == 1 && i >= fixedCells) {
            const auto narrow = std::find_if(cells.begin() + static_cast<std::ptrdiff_t>(i) + 1, cells.end(),
                                             [&](const Cell& cell) { return NaturalWidth(cell, icons) <= kCellWidth; });
            if (narrow != cells.end()) {
                std::rotate(cells.begin() + static_cast<std::ptrdiff_t>(i), narrow, narrow + 1);
                wide = false;
            }
        }
        column = wide ? 0 : 1 - column;
    }

    PixelFont& regular = Regular();
    const bool track = options.music && data.track;
    const bool weather = options.weather && data.weather;
    const int textHeight = regular.Height();
    int fixedHeight = 0;
    if (track) {
        fixedHeight += textHeight + kTextRowGap;
    }
    if (weather) {
        fixedHeight += textHeight;
    }
    // As many rows of figures as fit: the track and the weather leave more room while they are off or nothing plays.
    // The processors and the memory come first; fans that do not fit after them take turns, a page at a time.
    const int maxRows = std::max(0, (kScreenHeight - 2 * kDriftY - fixedHeight + (weather ? 0 : 3)) / kStatPitch);
    std::vector<Slot> places;
    int row = 0;
    int column = 0;
    const size_t fixedPlaced = PackCells(std::vector<Cell>(cells.begin(), cells.begin() + fixedCells), 0, icons,
                                         maxRows, row, column, &places);
    cells.resize(fixedPlaced == fixedCells ? cells.size() : fixedPlaced);
    std::vector<std::pair<size_t, size_t>> pages;  // First fan and how many.
    int statRows = places.empty() ? 0 : places.back().row + 1;
    for (size_t first = fixedCells; first < cells.size();) {
        int pageRow = row;
        int pageColumn = column;
        std::vector<Slot> pagePlaces;
        const size_t count = PackCells(cells, first, icons, maxRows, pageRow, pageColumn, &pagePlaces);
        if (count == 0) {
            break;
        }
        pages.emplace_back(first, count);
        statRows = std::max(statRows, pagePlaces.back().row + 1);
        first += count;
    }
    std::vector<size_t> shownCells(fixedPlaced);
    for (size_t i = 0; i < fixedPlaced; ++i) {
        shownCells[i] = i;
    }
    if (!pages.empty()) {
        const auto [first, count] = pages[static_cast<size_t>(elapsedMs / kFanPageMs) % pages.size()];
        int pageRow = row;
        int pageColumn = column;
        PackCells(cells, first, icons, maxRows, pageRow, pageColumn, &places);
        for (size_t i = 0; i < count; ++i) {
            shownCells.push_back(first + i);
        }
    }
    // The height of the fullest page, so the picture keeps its place while fans take turns.
    const int statsHeight = statRows ? statRows * kStatPitch - (weather ? 0 : 3) : 0;
    const int height = fixedHeight + statsHeight;
    if (height == 0) {
        return;
    }
    const int left = kContentLeft + dx;
    int top = (kScreenHeight - height) / 2 + dy;

    if (track) {
        const std::wstring text = data.track->artist.empty()  ? data.track->title
                                  : data.track->title.empty() ? data.track->artist
                                                              : data.track->artist + L" – " + data.track->title;
        DrawIcon(frame, kNoteIcon, left, top + (textHeight - kIconSize) / 2);
        const int textLeft = left + kIconSize + kIconGap;
        const int textRight = left + kContentWidth;
        const TextBits& bits = regular.Render(text);
        if (bits.width <= textRight - textLeft) {
            bits.Draw(frame, textLeft, top, textLeft, textRight);
        } else {
            // Waits, scrolls once around to where it started, and waits again.
            scrolling = true;
            const int loop = bits.width + kScrollGap;
            const ULONGLONG cycle = kScrollPauseMs + static_cast<ULONGLONG>(loop) * kScrollStepMs;
            const ULONGLONG into = data.trackMs % cycle;
            const int offset = into < kScrollPauseMs ? 0 : static_cast<int>((into - kScrollPauseMs) / kScrollStepMs);
            bits.Draw(frame, textLeft - offset, top, textLeft, textRight);
            bits.Draw(frame, textLeft - offset + loop, top, textLeft, textRight);
        }
        top += textHeight + kTextRowGap;
    }

    for (size_t i = 0; i < shownCells.size(); ++i) {
        const Slot& place = places[i];
        const Cell& cell = cells[shownCells[i]];
        const int cellLeft = left + place.column * (kCellWidth + kColumnGap);
        // A cell that takes the whole row grows only as far as its values need, so they stay beside the label.
        const int width = place.wide ? std::min(kContentWidth, NaturalWidth(cell, icons) + 1) : kCellWidth;
        DrawCell(frame, cell, icons, cellLeft, top + place.row * kStatPitch, width, elapsedMs);
    }
    top += statsHeight;

    if (weather) {
        const Weather& now = *data.weather;
        DrawIcon(frame, SkyIcon(now), left, top + (textHeight - kIconSize) / 2);
        const int textLeft = left + kIconSize + kIconGap;
        const std::wstring degrees = std::to_wstring(std::lround(now.temperature)) +
                                     (options.fahrenheit ? L"°F" : L"°C");
        PixelFont& bold = Bold();
        const TextBits& temperature = bold.Render(degrees);
        temperature.Draw(frame, textLeft, top, textLeft, left + kContentWidth);
        const int placeLeft = textLeft + temperature.width + 6;
        if (!now.place.empty() && placeLeft < left + kContentWidth) {
            const TextBits& place = regular.Render(regular.Fit(now.place, left + kContentWidth - placeLeft));
            place.Draw(frame, placeLeft, top, placeLeft, left + kContentWidth);
        }
    }
}
