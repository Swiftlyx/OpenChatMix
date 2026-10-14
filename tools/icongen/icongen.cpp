// Renders the OpenChatMix app icon with Direct2D and writes a multi-size .ico (PNG frames)
// plus standalone PNG previews.
//
// Usage: icongen.exe <output directory> [size-strip.png]
// The optional strip shows every icon size on dark and light backgrounds for review.

#include <windows.h>

#include <d2d1.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

constexpr UINT kIconSizes[] = {16, 20, 24, 32, 40, 48, 64, 96, 128, 256};
constexpr UINT kPreviewSizes[] = {256, 512};
constexpr UINT kStripSizes[] = {16, 20, 24, 32, 48, 64, 128};

// Artwork geometry in a 256x256 design space. The headphones are centered on (128, 128):
// they span x 30..226 and y 46..210.
constexpr float kCenterX = 128.0f;
constexpr float kBandY = 132.0f;  // Center of the headband arc.
constexpr float kBandRadius = 72.0f;
constexpr float kBandWidth = 28.0f;
constexpr float kCompactBandWidth = 34.0f;  // Thicker band so it survives at 16-24 px.
constexpr float kCupWidth = 52.0f;
constexpr float kCupTop = kBandY - 8.0f;
constexpr float kCupBottom = kBandY + 78.0f;
constexpr float kCupRadius = 24.0f;
constexpr float kTileRadius = 58.0f;

D2D1_COLOR_F Rgb(uint32_t rgb, float alpha = 1.0f) {
    return {((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, alpha};
}

ComPtr<ID2D1LinearGradientBrush> Gradient(ID2D1RenderTarget* target, D2D1_POINT_2F from, D2D1_POINT_2F to,
                                          D2D1_COLOR_F start, D2D1_COLOR_F end) {
    const D2D1_GRADIENT_STOP stops[] = {{0.0f, start}, {1.0f, end}};
    ComPtr<ID2D1GradientStopCollection> collection;
    target->CreateGradientStopCollection(stops, 2, &collection);
    ComPtr<ID2D1LinearGradientBrush> brush;
    target->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(from, to), collection.Get(), &brush);
    return brush;
}

// A warm dark tile, a headband and two ear cups in the Game (orange) and Chat (blue) colors,
// with a balance slider between the cups at larger sizes.
void DrawIcon(ID2D1Factory* factory, ID2D1RenderTarget* target, UINT size, D2D1_POINT_2F origin) {
    const float scale = size / 256.0f;
    target->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale) * D2D1::Matrix3x2F::Translation(origin.x, origin.y));
    const bool compact = size <= 24;

    auto tile = Gradient(target, {0.0f, 0.0f}, {0.0f, 256.0f}, Rgb(0x3A2A20), Rgb(0x1A120D));
    target->FillRoundedRectangle(D2D1::RoundedRect({0.0f, 0.0f, 256.0f, 256.0f}, kTileRadius, kTileRadius),
                                 tile.Get());

    // The band runs down into both cups so it reads as one connected headset.
    const float bandLeft = kCenterX - kBandRadius;
    const float bandRight = kCenterX + kBandRadius;
    ComPtr<ID2D1PathGeometry> band;
    factory->CreatePathGeometry(&band);
    ComPtr<ID2D1GeometrySink> sink;
    band->Open(&sink);
    sink->BeginFigure({bandLeft, kCupTop + 30.0f}, D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddLine({bandLeft, kBandY});
    sink->AddArc(D2D1::ArcSegment({bandRight, kBandY}, {kBandRadius, kBandRadius}, 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE,
                                  D2D1_ARC_SIZE_SMALL));
    sink->AddLine({bandRight, kCupTop + 30.0f});
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    ComPtr<ID2D1StrokeStyle> round;
    factory->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                           D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND),
                               nullptr, 0, &round);
    ComPtr<ID2D1SolidColorBrush> light;
    target->CreateSolidColorBrush(Rgb(0xF3E3DB), &light);
    target->DrawGeometry(band.Get(), light.Get(), compact ? kCompactBandWidth : kBandWidth, round.Get());

    const D2D1_RECT_F leftCup{bandLeft - kCupWidth / 2.0f, kCupTop, bandLeft + kCupWidth / 2.0f, kCupBottom};
    const D2D1_RECT_F rightCup{bandRight - kCupWidth / 2.0f, kCupTop, bandRight + kCupWidth / 2.0f, kCupBottom};
    auto game = Gradient(target, {0.0f, kCupTop}, {0.0f, kCupBottom}, Rgb(0xFFA463), Rgb(0xFF6F12));
    auto chat = Gradient(target, {0.0f, kCupTop}, {0.0f, kCupBottom}, Rgb(0x8BC7FF), Rgb(0x2A86FF));
    target->FillRoundedRectangle(D2D1::RoundedRect(leftCup, kCupRadius, kCupRadius), game.Get());
    target->FillRoundedRectangle(D2D1::RoundedRect(rightCup, kCupRadius, kCupRadius), chat.Get());

    if (size >= 48) {
        const float sliderY = kCupBottom - 26.0f;
        ComPtr<ID2D1SolidColorBrush> track;
        target->CreateSolidColorBrush(Rgb(0xF3E3DB, 0.35f), &track);
        target->FillRoundedRectangle(
            D2D1::RoundedRect({kCenterX - 26.0f, sliderY - 4.0f, kCenterX + 26.0f, sliderY + 4.0f}, 4.0f, 4.0f),
            track.Get());
        target->FillEllipse(D2D1::Ellipse({kCenterX + 10.0f, sliderY}, 9.0f, 9.0f), light.Get());
    }
}

ComPtr<ID2D1RenderTarget> CreateTarget(ID2D1Factory* factory, IWICBitmap* bitmap) {
    ComPtr<ID2D1RenderTarget> target;
    const auto properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    factory->CreateWicBitmapRenderTarget(bitmap, properties, &target);
    target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    return target;
}

std::vector<BYTE> EncodePng(IWICImagingFactory* wic, IWICBitmap* bitmap) {
    UINT width = 0;
    UINT height = 0;
    bitmap->GetSize(&width, &height);

    // PNG frames in icons carry straight (non-premultiplied) alpha.
    ComPtr<IWICFormatConverter> converter;
    wic->CreateFormatConverter(&converter);
    converter->Initialize(bitmap, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                          WICBitmapPaletteTypeCustom);

    ComPtr<IStream> stream;
    CreateStreamOnHGlobal(nullptr, TRUE, &stream);
    ComPtr<IWICBitmapEncoder> encoder;
    wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    ComPtr<IWICBitmapFrameEncode> frame;
    encoder->CreateNewFrame(&frame, nullptr);
    frame->Initialize(nullptr);
    frame->SetSize(width, height);
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    frame->SetPixelFormat(&format);
    frame->WriteSource(converter.Get(), nullptr);
    frame->Commit();
    encoder->Commit();

    STATSTG stat{};
    stream->Stat(&stat, STATFLAG_NONAME);
    std::vector<BYTE> bytes(static_cast<size_t>(stat.cbSize.QuadPart));
    const LARGE_INTEGER zero{};
    stream->Seek(zero, STREAM_SEEK_SET, nullptr);
    ULONG read = 0;
    stream->Read(bytes.data(), static_cast<ULONG>(bytes.size()), &read);
    return bytes;
}

std::vector<BYTE> RenderIconPng(ID2D1Factory* factory, IWICImagingFactory* wic, UINT size) {
    ComPtr<IWICBitmap> bitmap;
    wic->CreateBitmap(size, size, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap);
    ComPtr<ID2D1RenderTarget> target = CreateTarget(factory, bitmap.Get());
    target->BeginDraw();
    target->Clear({0.0f, 0.0f, 0.0f, 0.0f});
    DrawIcon(factory, target.Get(), size, {0.0f, 0.0f});
    target->EndDraw();
    return EncodePng(wic, bitmap.Get());
}

std::vector<BYTE> RenderStripPng(ID2D1Factory* factory, IWICImagingFactory* wic) {
    constexpr UINT kGap = 24;
    constexpr UINT kRowHeight = 128 + 2 * kGap;
    UINT width = kGap;
    for (const UINT size : kStripSizes) {
        width += size + kGap;
    }
    ComPtr<IWICBitmap> bitmap;
    wic->CreateBitmap(width, 2 * kRowHeight, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap);
    ComPtr<ID2D1RenderTarget> target = CreateTarget(factory, bitmap.Get());
    target->BeginDraw();
    const D2D1_COLOR_F backgrounds[] = {Rgb(0x202020), Rgb(0xF3F3F3)};
    for (UINT row = 0; row < 2; ++row) {
        ComPtr<ID2D1SolidColorBrush> background;
        target->CreateSolidColorBrush(backgrounds[row], &background);
        target->SetTransform(D2D1::Matrix3x2F::Identity());
        target->FillRectangle({0.0f, static_cast<float>(row * kRowHeight), static_cast<float>(width),
                               static_cast<float>((row + 1) * kRowHeight)},
                              background.Get());
        float x = kGap;
        for (const UINT size : kStripSizes) {
            const float y = static_cast<float>(row * kRowHeight + kGap + (128 - size) / 2);
            DrawIcon(factory, target.Get(), size, {x, y});
            x += size + kGap;
        }
    }
    target->EndDraw();
    return EncodePng(wic, bitmap.Get());
}

bool WriteFileBytes(const std::wstring& path, const std::vector<BYTE>& bytes) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const BOOL ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    CloseHandle(file);
    return ok && written == bytes.size();
}

#pragma pack(push, 1)
struct IconDir {
    WORD reserved;
    WORD type;
    WORD count;
};
struct IconDirEntry {
    BYTE width;
    BYTE height;
    BYTE colorCount;
    BYTE reserved;
    WORD planes;
    WORD bitCount;
    DWORD bytesInRes;
    DWORD imageOffset;
};
#pragma pack(pop)

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::fwprintf(stderr, L"usage: icongen <output directory> [size-strip.png]\n");
        return 2;
    }
    const std::wstring directory = argv[1];
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    {
        ComPtr<ID2D1Factory> factory;
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf());
        ComPtr<IWICImagingFactory> wic;
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));

        std::vector<std::vector<BYTE>> frames;
        for (const UINT size : kIconSizes) {
            frames.push_back(RenderIconPng(factory.Get(), wic.Get(), size));
        }
        std::vector<BYTE> icon(sizeof(IconDir) + frames.size() * sizeof(IconDirEntry));
        auto* header = reinterpret_cast<IconDir*>(icon.data());
        *header = {0, 1, static_cast<WORD>(frames.size())};
        DWORD offset = static_cast<DWORD>(icon.size());
        for (size_t i = 0; i < frames.size(); ++i) {
            auto* entry = reinterpret_cast<IconDirEntry*>(icon.data() + sizeof(IconDir) + i * sizeof(IconDirEntry));
            const UINT size = kIconSizes[i];
            // A dimension of 0 means 256 pixels.
            *entry = {static_cast<BYTE>(size >= 256 ? 0 : size), static_cast<BYTE>(size >= 256 ? 0 : size), 0, 0, 1, 32,
                      static_cast<DWORD>(frames[i].size()), offset};
            offset += static_cast<DWORD>(frames[i].size());
        }
        for (const auto& frame : frames) {
            icon.insert(icon.end(), frame.begin(), frame.end());
        }
        if (!WriteFileBytes(directory + L"\\OpenChatMix.ico", icon)) {
            std::fwprintf(stderr, L"cannot write icon\n");
            return 1;
        }
        for (const UINT size : kPreviewSizes) {
            WriteFileBytes(directory + L"\\OpenChatMix-" + std::to_wstring(size) + L".png",
                           RenderIconPng(factory.Get(), wic.Get(), size));
        }
        if (argc > 2) {
            WriteFileBytes(argv[2], RenderStripPng(factory.Get(), wic.Get()));
        }
    }
    CoUninitialize();
    return 0;
}
