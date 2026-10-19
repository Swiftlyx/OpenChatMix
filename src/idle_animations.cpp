#include "idle_animations.h"

#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

#include "app.h"
#include "log.h"

using Microsoft::WRL::ComPtr;

namespace {

// xorshift32; statistical quality does not matter for these animations.
class Random {
public:
    explicit Random(uint32_t seed) : state_(seed ? seed : 0x9E3779B9u) {}
    uint32_t Next() {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return state_;
    }
    float Range(float min, float max) { return min + (max - min) * static_cast<float>(Next() >> 8) / 16777216.0f; }

private:
    uint32_t state_;
};

// Position on a path that bounces between 0 and range at `speed` pixels per second.
int BouncePosition(ULONGLONG elapsedMs, double speed, int range) {
    if (range <= 0) {
        return 0;
    }
    const double period = 2.0 * range;
    const double phase = std::fmod(static_cast<double>(elapsedMs) * speed / 1000.0, period);
    return static_cast<int>(phase <= range ? phase : period - phase);
}

struct Sprite {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;  // Row-major, 0 or 1.

    void DrawTo(OledBitmap& frame, int left, int top) const {
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                if (pixels[y * width + x]) {
                    frame.Set(left + x, top + y);
                }
            }
        }
    }
};

// Renders text with a GDI font without antialiasing, cropped to its lit pixels.
Sprite RenderText(const std::wstring& text, const wchar_t* face, int pixelHeight, int weight) {
    Sprite sprite;
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) {
        return sprite;
    }
    HFONT font = CreateFontW(-pixelHeight, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY, DEFAULT_PITCH, face);
    HGDIOBJ previousFont = SelectObject(dc, font);
    const int length = static_cast<int>(text.size());
    SIZE size{};
    GetTextExtentPoint32W(dc, text.c_str(), length, &size);
    if (size.cx > 0 && size.cy > 0) {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = size.cx;
        info.bmiHeader.biHeight = -size.cy;  // Top-down rows.
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        if (HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0)) {
            HGDIOBJ previousBitmap = SelectObject(dc, bitmap);
            std::memset(bits, 0, static_cast<size_t>(size.cx) * size.cy * 4);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(255, 255, 255));
            TextOutW(dc, 0, 0, text.c_str(), length);
            GdiFlush();
            const auto* source = static_cast<const uint32_t*>(bits);
            auto lit = [&](int x, int y) { return (source[y * size.cx + x] & 0xFF) > 127; };
            int left = size.cx;
            int top = size.cy;
            int right = -1;
            int bottom = -1;
            for (int y = 0; y < size.cy; ++y) {
                for (int x = 0; x < size.cx; ++x) {
                    if (lit(x, y)) {
                        left = std::min(left, x);
                        right = std::max(right, x);
                        top = std::min(top, y);
                        bottom = std::max(bottom, y);
                    }
                }
            }
            if (right >= 0) {
                sprite.width = right - left + 1;
                sprite.height = bottom - top + 1;
                sprite.pixels.resize(static_cast<size_t>(sprite.width) * sprite.height);
                for (int y = 0; y < sprite.height; ++y) {
                    for (int x = 0; x < sprite.width; ++x) {
                        sprite.pixels[y * sprite.width + x] = lit(left + x, top + y) ? 1 : 0;
                    }
                }
            }
            SelectObject(dc, previousBitmap);
            DeleteObject(bitmap);
        }
    }
    SelectObject(dc, previousFont);
    DeleteObject(font);
    DeleteDC(dc);
    return sprite;
}

// The largest rendering of text, starting at pixelHeight, that fits maxWidth.
Sprite FitText(const std::wstring& text, const wchar_t* face, int pixelHeight, int weight, int maxWidth) {
    Sprite sprite = RenderText(text, face, pixelHeight, weight);
    while (sprite.width > maxWidth && pixelHeight > 8) {
        pixelHeight -= 2;
        sprite = RenderText(text, face, pixelHeight, weight);
    }
    return sprite;
}

// Stars flying towards the viewer.
class Starfield final : public IdleAnimation {
public:
    DWORD Render(ULONGLONG elapsedMs, OledBitmap& frame) override {
        if (stars_.empty() || elapsedMs < lastMs_) {
            stars_.resize(kCount);
            for (Star& star : stars_) {
                Spawn(star, true);
            }
            lastMs_ = elapsedMs;
        }
        const float seconds = std::min(0.2f, static_cast<float>(elapsedMs - lastMs_) / 1000.0f);
        lastMs_ = elapsedMs;
        for (Star& star : stars_) {
            star.z -= kSpeed * seconds;
            int x = 0;
            int y = 0;
            if (star.z <= kNear || !Project(star, x, y)) {
                Spawn(star, false);
                continue;
            }
            frame.Set(x, y);
            if (star.z < kCloseDepth) {
                frame.Set(x + 1, y);
                frame.Set(x, y + 1);
                frame.Set(x + 1, y + 1);
            }
        }
        return kFrameMs;
    }

private:
    struct Star {
        float x;  // -1..1 at the far plane spans the screen width.
        float y;
        float z;  // 1 far, kNear closest.
    };
    static constexpr int kCount = 48;
    static constexpr float kSpeed = 0.3f;  // Depth units per second.
    static constexpr float kNear = 0.05f;
    static constexpr float kCloseDepth = 0.3f;  // Closer stars are drawn 2x2.
    static constexpr DWORD kFrameMs = 66;

    static bool Project(const Star& star, int& x, int& y) {
        constexpr float kScale = kOledWidth / 2.0f;
        x = static_cast<int>(std::lround(kOledWidth / 2.0f + star.x / star.z * kScale));
        y = static_cast<int>(std::lround(kOledHeight / 2.0f + star.y / star.z * kScale));
        return x >= 0 && x < kOledWidth && y >= 0 && y < kOledHeight;
    }

    void Spawn(Star& star, bool anyDepth) {
        star.x = random_.Range(-1.0f, 1.0f);
        star.y = random_.Range(-0.5f, 0.5f);
        star.z = anyDepth ? random_.Range(2.0f * kNear, 1.0f) : 1.0f;
    }

    Random random_{static_cast<uint32_t>(GetTickCount64())};
    std::vector<Star> stars_;
    ULONGLONG lastMs_ = 0;
};

// Time and date that move by one pixel every few seconds.
class DriftingClock final : public IdleAnimation {
public:
    DWORD Render(ULONGLONG elapsedMs, OledBitmap& frame) override {
        SYSTEMTIME now;
        GetLocalTime(&now);
        const int minute = now.wDay * 1440 + now.wHour * 60 + now.wMinute;
        if (minute != minute_) {
            minute_ = minute;
            wchar_t text[64] = {};
            GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &now, nullptr, text, 64);
            time_ = FitText(text, L"Segoe UI Semibold", 34, FW_SEMIBOLD, kOledWidth - 4);
            text[0] = L'\0';
            GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &now, L"ddd d MMM", text, 64, nullptr);
            date_ = FitText(text, L"Segoe UI", 13, FW_SEMIBOLD, kOledWidth - 4);
        }
        const int width = std::max(time_.width, date_.width);
        const int height = time_.height + kGap + date_.height;
        const int left = BouncePosition(elapsedMs, kSpeedX, kOledWidth - width);
        const int top = BouncePosition(elapsedMs, kSpeedY, kOledHeight - height);
        time_.DrawTo(frame, left + (width - time_.width) / 2, top);
        date_.DrawTo(frame, left + (width - date_.width) / 2, top + time_.height + kGap);
        return kFrameMs;
    }

private:
    static constexpr int kGap = 5;
    static constexpr double kSpeedX = 0.5;  // Pixels per second.
    static constexpr double kSpeedY = 0.3;
    static constexpr DWORD kFrameMs = 500;
    int minute_ = -1;
    Sprite time_;
    Sprite date_;
};

// Conway's Game of Life on a torus, reseeded when it settles.
class Life final : public IdleAnimation {
public:
    DWORD Render(ULONGLONG elapsedMs, OledBitmap& frame) override {
        if (cells_.empty() || elapsedMs < lastStepMs_ || elapsedMs - seededMs_ >= kReseedMs) {
            Seed(elapsedMs);
        } else if (elapsedMs - lastStepMs_ >= kStepMs) {
            lastStepMs_ = elapsedMs;
            if (!Step()) {
                Seed(elapsedMs);
            }
        }
        for (size_t i = 0; i < cells_.size(); ++i) {
            frame.pixels[i] = cells_[i];
        }
        return kStepMs;
    }

private:
    static constexpr DWORD kStepMs = 150;
    static constexpr ULONGLONG kReseedMs = 3 * 60 * 1000;
    static constexpr size_t kHistory = 40;  // Generations compared to detect a settled board.
    static constexpr int kMinPopulation = 24;

    void Seed(ULONGLONG elapsedMs) {
        cells_.assign(kOledWidth * kOledHeight, 0);
        for (auto& cell : cells_) {
            cell = random_.Next() % 4 == 0 ? 1 : 0;
        }
        populations_.clear();
        seededMs_ = lastStepMs_ = elapsedMs;
    }

    // Returns false once the board is nearly empty or only repeats itself.
    bool Step() {
        std::vector<uint8_t> next(cells_.size(), 0);
        int population = 0;
        for (int y = 0; y < kOledHeight; ++y) {
            for (int x = 0; x < kOledWidth; ++x) {
                int neighbours = 0;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx || dy) {
                            const int nx = (x + dx + kOledWidth) % kOledWidth;
                            const int ny = (y + dy + kOledHeight) % kOledHeight;
                            neighbours += cells_[ny * kOledWidth + nx];
                        }
                    }
                }
                const bool alive = cells_[y * kOledWidth + x] != 0;
                if (neighbours == 3 || (alive && neighbours == 2)) {
                    next[y * kOledWidth + x] = 1;
                    ++population;
                }
            }
        }
        cells_ = std::move(next);
        populations_.push_back(population);
        if (populations_.size() > kHistory) {
            populations_.erase(populations_.begin());
        }
        const auto [low, high] = std::minmax_element(populations_.begin(), populations_.end());
        const bool settled = populations_.size() == kHistory && *high - *low <= 3;
        return population >= kMinPopulation && !settled;
    }

    Random random_{static_cast<uint32_t>(GetTickCount64()) ^ 0xA5A5A5A5u};
    std::vector<uint8_t> cells_;
    std::vector<int> populations_;
    ULONGLONG seededMs_ = 0;
    ULONGLONG lastStepMs_ = 0;
};

// All pixels off.
class BlankScreen final : public IdleAnimation {
public:
    DWORD Render(ULONGLONG, OledBitmap&) override { return 1000; }
};

UINT MetadataUInt(IWICMetadataQueryReader* reader, const wchar_t* name, UINT fallback) {
    PROPVARIANT value;
    PropVariantInit(&value);
    UINT result = fallback;
    if (reader && SUCCEEDED(reader->GetMetadataByName(name, &value))) {
        if (value.vt == VT_UI1) {
            result = value.bVal;
        } else if (value.vt == VT_UI2) {
            result = value.uiVal;
        } else if (value.vt == VT_UI4) {
            result = value.ulVal;
        }
    }
    PropVariantClear(&value);
    return result;
}

// Still image or animated GIF, letterboxed to 128x64 with ordered dithering. GIF frames are composed
// with their disposal methods and played in real time; frames the display cannot keep up with are skipped.
class ImageAnimation final : public IdleAnimation {
public:
    explicit ImageAnimation(std::wstring path) : path_(std::move(path)) {}

    bool Load(const std::atomic<bool>& cancel) override;

    DWORD Render(ULONGLONG elapsedMs, OledBitmap& frame) override {
        if (frames_.size() == 1) {
            // A still image moves by a pixel every half minute.
            static constexpr int kOffsets[][2] = {{0, 0}, {1, 0},   {1, 1},  {0, 1}, {-1, 1},
                                                  {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
            const auto& offset = kOffsets[(elapsedMs / kDriftMs) % std::size(kOffsets)];
            Unpack(frames_[0], frame, offset[0], offset[1]);
            return static_cast<DWORD>(kDriftMs - elapsedMs % kDriftMs);
        }
        const ULONGLONG time = elapsedMs % ends_.back();
        const size_t index = static_cast<size_t>(std::upper_bound(ends_.begin(), ends_.end(), time) - ends_.begin());
        Unpack(frames_[index], frame, 0, 0);
        return static_cast<DWORD>(ends_[index] - time);
    }

private:
    using Packed = std::array<uint8_t, kOledWidth * kOledHeight / 8>;  // Row-major bits.
    static constexpr ULONGLONG kDriftMs = 30000;
    static constexpr size_t kMaxFrames = 12000;      // About 12 MB of frames.
    static constexpr UINT kMaxCanvas = 1920;         // Largest GIF canvas side.
    // Larger still images are refused before decoding; decoders may hold the whole image in memory.
    static constexpr UINT64 kMaxStillPixels = 100'000'000;
    // Pixels decoded for one GIF at most (about a minute of work); later frames are left out.
    static constexpr UINT64 kMaxDecodedPixels = 4'000'000'000;
    static constexpr UINT kStillLimitX = 4 * kOledWidth;  // Still images are scaled down to this first.
    static constexpr UINT kStillLimitY = 4 * kOledHeight;
    static constexpr int kSamples = 4;               // Per axis, per display pixel.

    void PrepareSampling(UINT width, UINT height);
    Packed Quantize(const std::vector<uint32_t>& canvas, UINT width) const;
    static void Unpack(const Packed& packed, OledBitmap& frame, int dx, int dy) {
        for (int y = 0; y < kOledHeight; ++y) {
            for (int x = 0; x < kOledWidth; ++x) {
                const int bit = y * kOledWidth + x;
                if (packed[bit / 8] & (0x80 >> (bit % 8))) {
                    frame.Set(x + dx, y + dy);
                }
            }
        }
    }

    std::wstring path_;
    std::vector<Packed> frames_;
    std::vector<ULONGLONG> ends_;  // Cumulative end time of each frame.
    // Letterboxed placement and source sample coordinates for every display pixel.
    int outLeft_ = 0;
    int outTop_ = 0;
    int outWidth_ = 0;
    int outHeight_ = 0;
    std::vector<UINT> sampleX_;
    std::vector<UINT> sampleY_;
};

void ImageAnimation::PrepareSampling(UINT width, UINT height) {
    const double scale = std::min(static_cast<double>(kOledWidth) / width, static_cast<double>(kOledHeight) / height);
    outWidth_ = std::clamp(static_cast<int>(std::lround(width * scale)), 1, kOledWidth);
    outHeight_ = std::clamp(static_cast<int>(std::lround(height * scale)), 1, kOledHeight);
    outLeft_ = (kOledWidth - outWidth_) / 2;
    outTop_ = (kOledHeight - outHeight_) / 2;
    sampleX_.resize(static_cast<size_t>(outWidth_) * kSamples);
    sampleY_.resize(static_cast<size_t>(outHeight_) * kSamples);
    for (size_t i = 0; i < sampleX_.size(); ++i) {
        sampleX_[i] = std::min(width - 1, static_cast<UINT>((i + 0.5) / kSamples / scale));
    }
    for (size_t i = 0; i < sampleY_.size(); ++i) {
        sampleY_[i] = std::min(height - 1, static_cast<UINT>((i + 0.5) / kSamples / scale));
    }
}

ImageAnimation::Packed ImageAnimation::Quantize(const std::vector<uint32_t>& canvas, UINT width) const {
    static constexpr int kBayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    Packed packed{};
    for (int y = 0; y < outHeight_; ++y) {
        for (int x = 0; x < outWidth_; ++x) {
            int sum = 0;
            for (int sy = 0; sy < kSamples; ++sy) {
                const uint32_t* row = canvas.data() + static_cast<size_t>(sampleY_[y * kSamples + sy]) * width;
                for (int sx = 0; sx < kSamples; ++sx) {
                    // Premultiplied BGRA, so transparent areas count as black.
                    const uint32_t color = row[sampleX_[x * kSamples + sx]];
                    sum += (((color >> 16) & 0xFF) * 77 + ((color >> 8) & 0xFF) * 150 + (color & 0xFF) * 29) >> 8;
                }
            }
            const int luminance = sum / (kSamples * kSamples);
            const int dx = outLeft_ + x;
            const int dy = outTop_ + y;
            // On when luminance / 255 exceeds the dither threshold (b + 0.5) / 16.
            if (luminance * 32 > (2 * kBayer[dy & 3][dx & 3] + 1) * 255) {
                const int bit = dy * kOledWidth + dx;
                packed[bit / 8] |= static_cast<uint8_t>(0x80 >> (bit % 8));
            }
        }
    }
    return packed;
}

bool ImageAnimation::Load(const std::atomic<bool>& cancel) {
    if (!frames_.empty()) {
        return true;
    }
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) ||
        FAILED(factory->CreateDecoderFromFilename(path_.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
                                                  &decoder))) {
        Log("idle image: cannot open %s", Utf8(path_).c_str());
        return false;
    }
    GUID container{};
    decoder->GetContainerFormat(&container);
    const bool gif = container == GUID_ContainerFormatGif;
    UINT count = 0;
    decoder->GetFrameCount(&count);
    if (!gif) {
        count = std::min(count, 1u);
    }

    UINT width = 0;
    UINT height = 0;
    if (gif) {
        ComPtr<IWICMetadataQueryReader> reader;
        decoder->GetMetadataQueryReader(&reader);
        width = MetadataUInt(reader.Get(), L"/logscrdesc/Width", 0);
        height = MetadataUInt(reader.Get(), L"/logscrdesc/Height", 0);
    }
    std::vector<uint32_t> canvas;
    std::vector<uint32_t> saved;
    std::vector<uint32_t> pixels;
    UINT64 decoded = 0;
    for (UINT index = 0; index < count && !cancel; ++index) {
        if (frames_.size() >= kMaxFrames || decoded >= kMaxDecodedPixels) {
            Log("idle image: only the first %zu frames are used", frames_.size());
            break;
        }
        ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(decoder->GetFrame(index, &frame))) {
            break;
        }
        UINT frameWidth = 0;
        UINT frameHeight = 0;
        frame->GetSize(&frameWidth, &frameHeight);
        // GIF frames are clipped to a canvas of at most kMaxCanvas, so larger ones only come from broken
        // or hostile files, which must not make it allocate gigabytes.
        if (frameWidth == 0 || frameHeight == 0 || (gif && (frameWidth > kMaxCanvas || frameHeight > kMaxCanvas)) ||
            (!gif && static_cast<UINT64>(frameWidth) * frameHeight > kMaxStillPixels)) {
            Log("idle image: frame %u has an unsupported size %ux%u", index, frameWidth, frameHeight);
            if (index == 0) {
                return false;
            }
            break;
        }
        ComPtr<IWICBitmapSource> source = frame;
        UINT left = 0;
        UINT top = 0;
        UINT delayMs = 0;
        UINT disposal = 0;
        if (gif) {
            ComPtr<IWICMetadataQueryReader> reader;
            frame->GetMetadataQueryReader(&reader);
            left = MetadataUInt(reader.Get(), L"/imgdesc/Left", 0);
            top = MetadataUInt(reader.Get(), L"/imgdesc/Top", 0);
            // Delays are in 1/100 s; 0 and 1 are played as 100 ms, as browsers do.
            const UINT delay = MetadataUInt(reader.Get(), L"/grctlext/Delay", 10);
            delayMs = (delay < 2 ? 10 : delay) * 10;
            disposal = MetadataUInt(reader.Get(), L"/grctlext/Disposal", 0);
        } else if (frameWidth > kStillLimitX || frameHeight > kStillLimitY) {
            const double scale = std::min(static_cast<double>(kStillLimitX) / frameWidth,
                                          static_cast<double>(kStillLimitY) / frameHeight);
            const UINT scaledWidth = std::max(1u, static_cast<UINT>(frameWidth * scale));
            const UINT scaledHeight = std::max(1u, static_cast<UINT>(frameHeight * scale));
            ComPtr<IWICBitmapScaler> scaler;
            if (FAILED(factory->CreateBitmapScaler(&scaler)) ||
                FAILED(scaler->Initialize(frame.Get(), scaledWidth, scaledHeight, WICBitmapInterpolationModeFant))) {
                break;
            }
            source = scaler;
            frameWidth = scaledWidth;
            frameHeight = scaledHeight;
        }
        if (index == 0) {
            if (width == 0 || height == 0 || !gif) {
                width = frameWidth;
                height = frameHeight;
            }
            if (width == 0 || height == 0 || width > kMaxCanvas || height > kMaxCanvas) {
                Log("idle image: unsupported size %ux%u", width, height);
                return false;
            }
            canvas.assign(static_cast<size_t>(width) * height, 0);
            PrepareSampling(width, height);
        }

        ComPtr<IWICFormatConverter> converter;
        pixels.resize(static_cast<size_t>(frameWidth) * frameHeight);
        if (FAILED(factory->CreateFormatConverter(&converter)) ||
            FAILED(converter->Initialize(source.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr,
                                         0.0, WICBitmapPaletteTypeMedianCut)) ||
            FAILED(converter->CopyPixels(nullptr, frameWidth * 4, static_cast<UINT>(pixels.size() * 4),
                                         reinterpret_cast<BYTE*>(pixels.data())))) {
            break;
        }
        if (disposal == 3) {
            saved = canvas;
        }
        for (UINT y = 0; y < frameHeight && top + y < height; ++y) {
            for (UINT x = 0; x < frameWidth && left + x < width; ++x) {
                const uint32_t color = pixels[static_cast<size_t>(y) * frameWidth + x];
                // GIF transparency is all or nothing: transparent pixels keep the previous frame.
                if (!gif || (color >> 24) != 0) {
                    canvas[static_cast<size_t>(top + y) * width + left + x] = color;
                }
            }
        }
        decoded += static_cast<UINT64>(frameWidth) * frameHeight;
        frames_.push_back(Quantize(canvas, width));
        ends_.push_back((ends_.empty() ? 0 : ends_.back()) + delayMs);
        if (disposal == 2) {
            for (UINT y = top; y < std::min(height, top + frameHeight); ++y) {
                std::fill_n(canvas.begin() + static_cast<size_t>(y) * width + left,
                            std::min(frameWidth, width - std::min(width, left)), 0u);
            }
        } else if (disposal == 3) {
            canvas.swap(saved);
        }
    }
    if (cancel || frames_.empty()) {
        frames_.clear();
        ends_.clear();
        return false;
    }
    if (ends_.back() == 0) {
        ends_.back() = 1;
    }
    Log("idle image: %zu frame(s), %ux%u", frames_.size(), width, height);
    return true;
}

}  // namespace

std::shared_ptr<IdleAnimation> CreateIdleAnimation(IdleAnimationKind kind, const std::wstring& imagePath) {
    switch (kind) {
    case IdleAnimationKind::Stars: return std::make_shared<Starfield>();
    case IdleAnimationKind::Clock: return std::make_shared<DriftingClock>();
    case IdleAnimationKind::Life: return std::make_shared<Life>();
    case IdleAnimationKind::Blank: return std::make_shared<BlankScreen>();
    case IdleAnimationKind::Image:
        return imagePath.empty() ? nullptr : std::make_shared<ImageAnimation>(imagePath);
    case IdleAnimationKind::Off: break;
    }
    return nullptr;
}
