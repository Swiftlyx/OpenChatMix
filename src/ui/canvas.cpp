#include "canvas.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace ui {
namespace {

struct StyleSpec {
    float size;
    DWRITE_FONT_WEIGHT weight;
    bool display;  // Uses the display optical size of Segoe UI Variable.
};

constexpr StyleSpec kStyles[] = {
    {20.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, true},   // TitleLarge
    {16.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, true},   // TitleMedium
    {14.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, false},  // TitleSmall
    {14.5f, DWRITE_FONT_WEIGHT_NORMAL, false},     // BodyLarge
    {13.0f, DWRITE_FONT_WEIGHT_NORMAL, false},     // BodyMedium
    {12.0f, DWRITE_FONT_WEIGHT_NORMAL, false},     // BodySmall
    {13.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, false},  // LabelLarge
    {12.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, false},  // LabelMedium
    {10.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, false},  // LabelSmall
    {30.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, true},   // Number
};
static_assert(std::size(kStyles) == static_cast<size_t>(TextStyle::Count));

constexpr wchar_t kLocale[] = L"uk-UA";

DWRITE_TEXT_ALIGNMENT ToDWrite(TextAlign align) {
    switch (align) {
    case TextAlign::Center: return DWRITE_TEXT_ALIGNMENT_CENTER;
    case TextAlign::Trailing: return DWRITE_TEXT_ALIGNMENT_TRAILING;
    case TextAlign::Leading: break;
    }
    return DWRITE_TEXT_ALIGNMENT_LEADING;
}

}  // namespace

bool Canvas::Init() {
    // The last text format is created last, so it marks a complete initialization.
    if (formats_.back()) {
        return true;
    }
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d_.ReleaseAndGetAddressOf())) ||
        FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(dwrite_.ReleaseAndGetAddressOf()))) ||
        FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(wic_.ReleaseAndGetAddressOf())))) {
        Shutdown();
        return false;
    }

    const std::wstring textFamily = ResolveFamily({L"Segoe UI Variable Text", L"Segoe UI"});
    const std::wstring displayFamily = ResolveFamily({L"Segoe UI Variable Display", L"Segoe UI"});
    iconFamily_ = ResolveFamily({L"Segoe Fluent Icons", L"Segoe MDL2 Assets"});

    for (size_t i = 0; i < formats_.size(); ++i) {
        const StyleSpec& spec = kStyles[i];
        const std::wstring& family = spec.display ? displayFamily : textFamily;
        if (FAILED(dwrite_->CreateTextFormat(family.c_str(), nullptr, spec.weight, DWRITE_FONT_STYLE_NORMAL,
                                             DWRITE_FONT_STRETCH_NORMAL, spec.size, kLocale, &formats_[i]))) {
            Shutdown();
            return false;
        }
        formats_[i]->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        // Offset of the ink of capitals and digits from the center of a centered line.
        ComPtr<IDWriteTextLayout> probe;
        constexpr float kProbeHeight = 1000.0f;
        if (SUCCEEDED(dwrite_->CreateTextLayout(L"H0", 2, formats_[i].Get(), 1000.0f, kProbeHeight, &probe))) {
            DWRITE_OVERHANG_METRICS overhang{};
            if (SUCCEEDED(probe->GetOverhangMetrics(&overhang))) {
                const float inkTop = -overhang.top;
                const float inkBottom = kProbeHeight + overhang.bottom;
                textShift_[i] = std::round(((inkTop + inkBottom) / 2.0f - kProbeHeight / 2.0f) * 2.0f) / 2.0f;
            }
        }
        ComPtr<IDWriteInlineObject> ellipsis;
        dwrite_->CreateEllipsisTrimmingSign(formats_[i].Get(), &ellipsis);
        const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        formats_[i]->SetTrimming(&trimming, ellipsis.Get());
    }
    return true;
}

std::wstring Canvas::ResolveFamily(std::initializer_list<const wchar_t*> candidates) {
    ComPtr<IDWriteFontCollection> fonts;
    dwrite_->GetSystemFontCollection(&fonts);
    for (const wchar_t* name : candidates) {
        UINT32 index = 0;
        BOOL exists = FALSE;
        if (fonts && SUCCEEDED(fonts->FindFamilyName(name, &index, &exists)) && exists) {
            return name;
        }
    }
    return *(candidates.end() - 1);
}

namespace {

// Software rendering keeps GPU driver allocations out of an app that is idle most of the time.
D2D1_RENDER_TARGET_PROPERTIES SoftwareTargetProperties(float dpi) {
    return D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                                        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                                        dpi, dpi);
}

}  // namespace

void Canvas::Shutdown() {
    Detach();
    roundStroke_.Reset();
    glyphFormats_.clear();
    for (auto& format : formats_) {
        format.Reset();
    }
    wic_.Reset();
    dwrite_.Reset();
    d2d_.Reset();
}

bool Canvas::Attach(HWND window, float dpi) {
    Detach();
    if (!Init()) {
        return false;
    }
    RECT client{};
    GetClientRect(window, &client);
    const D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProperties = D2D1::HwndRenderTargetProperties(
        window, D2D1::SizeU(static_cast<UINT32>(client.right), static_cast<UINT32>(client.bottom)));
    if (FAILED(d2d_->CreateHwndRenderTarget(SoftwareTargetProperties(dpi), hwndProperties, &hwndTarget_))) {
        return false;
    }
    target_ = hwndTarget_;
    // ClearType needs an opaque background; the surfaces here are translucent.
    target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    target_->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &brush_);
    return true;
}

bool Canvas::AttachDC(float dpi) {
    Detach();
    const D2D1_RENDER_TARGET_PROPERTIES properties = SoftwareTargetProperties(dpi);
    if (!Init() || FAILED(d2d_->CreateDCRenderTarget(&properties, &dcTarget_))) {
        return false;
    }
    target_ = dcTarget_;
    target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    target_->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &brush_);
    return true;
}

bool Canvas::BindDC(HDC dc, const RECT& rect) {
    return dcTarget_ && SUCCEEDED(dcTarget_->BindDC(dc, &rect));
}

void Canvas::Detach() {
    clipLayer_.Reset();
    brush_.Reset();
    target_.Reset();
    hwndTarget_.Reset();
    dcTarget_.Reset();
}

void Canvas::Resize(UINT width, UINT height) {
    if (hwndTarget_) {
        hwndTarget_->Resize(D2D1::SizeU(width, height));
    }
}

void Canvas::Begin() {
    target_->BeginDraw();
    target_->SetTransform(D2D1::Matrix3x2F::Identity());
}

HRESULT Canvas::End() {
    return target_->EndDraw();
}

void Canvas::Clear(D2D1_COLOR_F color) {
    target_->Clear(color);
}

void Canvas::FillRect(const D2D1_RECT_F& rect, D2D1_COLOR_F color) {
    brush_->SetColor(color);
    target_->FillRectangle(rect, brush_.Get());
}

void Canvas::FillRound(const D2D1_RECT_F& rect, float radius, D2D1_COLOR_F color) {
    radius = std::min({radius, (rect.right - rect.left) / 2, (rect.bottom - rect.top) / 2});
    if (radius <= 0.0f) {
        return;
    }
    brush_->SetColor(color);
    target_->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush_.Get());
}

void Canvas::StrokeRound(const D2D1_RECT_F& rect, float radius, D2D1_COLOR_F color, float width) {
    radius = std::min({radius, (rect.right - rect.left) / 2, (rect.bottom - rect.top) / 2});
    brush_->SetColor(color);
    target_->DrawRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush_.Get(), width);
}

void Canvas::FillShape(const D2D1_RECT_F& rect, const Corners& corners, D2D1_COLOR_F color) {
    const float limit = std::min(rect.right - rect.left, rect.bottom - rect.top) / 2.0f;
    if (limit <= 0.0f) {
        return;
    }
    const float tl = std::clamp(corners.topLeft, 0.0f, limit);
    const float tr = std::clamp(corners.topRight, 0.0f, limit);
    const float br = std::clamp(corners.bottomRight, 0.0f, limit);
    const float bl = std::clamp(corners.bottomLeft, 0.0f, limit);
    if (tl == tr && tr == br && br == bl) {
        FillRound(rect, tl, color);
        if (tl <= 0.0f) {
            FillRect(rect, color);
        }
        return;
    }
    ComPtr<ID2D1PathGeometry> path;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(d2d_->CreatePathGeometry(&path)) || FAILED(path->Open(&sink))) {
        return;
    }
    // Each corner is a quarter circle drawn as a cubic Bézier (handles at 0.5523 of the radius), from
    // where the straight edge ends, around the rectangle's corner point, to where the next edge starts.
    constexpr float kHandle = 0.5523f;
    auto corner = [&](D2D1_POINT_2F from, D2D1_POINT_2F cornerPoint, D2D1_POINT_2F to) {
        if (from.x == to.x && from.y == to.y) {
            return;
        }
        sink->AddBezier({{from.x + (cornerPoint.x - from.x) * kHandle, from.y + (cornerPoint.y - from.y) * kHandle},
                         {to.x + (cornerPoint.x - to.x) * kHandle, to.y + (cornerPoint.y - to.y) * kHandle},
                         to});
    };
    const float l = rect.left, t = rect.top, r = rect.right, b = rect.bottom;
    sink->BeginFigure({l + tl, t}, D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine({r - tr, t});
    corner({r - tr, t}, {r, t}, {r, t + tr});
    sink->AddLine({r, b - br});
    corner({r, b - br}, {r, b}, {r - br, b});
    sink->AddLine({l + bl, b});
    corner({l + bl, b}, {l, b}, {l, b - bl});
    sink->AddLine({l, t + tl});
    corner({l, t + tl}, {l, t}, {l + tl, t});
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    if (FAILED(sink->Close())) {
        return;
    }
    brush_->SetColor(color);
    target_->FillGeometry(path.Get(), brush_.Get());
}

void Canvas::FillCircle(D2D1_POINT_2F center, float radius, D2D1_COLOR_F color) {
    brush_->SetColor(color);
    target_->FillEllipse(D2D1::Ellipse(center, radius, radius), brush_.Get());
}

void Canvas::StrokeCircle(D2D1_POINT_2F center, float radius, D2D1_COLOR_F color, float width) {
    brush_->SetColor(color);
    target_->DrawEllipse(D2D1::Ellipse(center, radius, radius), brush_.Get(), width);
}

void Canvas::Line(D2D1_POINT_2F from, D2D1_POINT_2F to, D2D1_COLOR_F color, float width) {
    brush_->SetColor(color);
    target_->DrawLine(from, to, brush_.Get(), width);
}

namespace {

ComPtr<ID2D1PathGeometry> MakePath(ID2D1Factory* factory, std::span<const D2D1_POINT_2F> points, bool closed) {
    ComPtr<ID2D1PathGeometry> path;
    ComPtr<ID2D1GeometrySink> sink;
    if (points.size() < 2 || FAILED(factory->CreatePathGeometry(&path)) || FAILED(path->Open(&sink))) {
        return nullptr;
    }
    sink->BeginFigure(points[0], closed ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddLines(points.data() + 1, static_cast<UINT32>(points.size() - 1));
    sink->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
    return SUCCEEDED(sink->Close()) ? path : nullptr;
}

}  // namespace

void Canvas::Polyline(std::span<const D2D1_POINT_2F> points, D2D1_COLOR_F color, float width) {
    if (ComPtr<ID2D1PathGeometry> path = MakePath(d2d_.Get(), points, false)) {
        if (!roundStroke_) {
            d2d_->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                                D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND),
                                    nullptr, 0, &roundStroke_);
        }
        brush_->SetColor(color);
        target_->DrawGeometry(path.Get(), brush_.Get(), width, roundStroke_.Get());
    }
}

void Canvas::FillPolygon(std::span<const D2D1_POINT_2F> points, D2D1_COLOR_F color) {
    if (ComPtr<ID2D1PathGeometry> path = MakePath(d2d_.Get(), points, true)) {
        brush_->SetColor(color);
        target_->FillGeometry(path.Get(), brush_.Get());
    }
}

IDWriteTextFormat* Canvas::Format(TextStyle style, TextAlign align, bool wrap) {
    IDWriteTextFormat* format = formats_[static_cast<size_t>(style)].Get();
    format->SetTextAlignment(ToDWrite(align));
    format->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    return format;
}

void Canvas::Text(std::wstring_view text, const D2D1_RECT_F& rect, TextStyle style, D2D1_COLOR_F color,
                  TextAlign align, bool wrap) {
    if (text.empty()) {
        return;
    }
    brush_->SetColor(color);
    // The line box holds room for descenders; centering it puts capitals and digits a bit low.
    const float shift = textShift_[static_cast<size_t>(style)];
    const D2D1_RECT_F shifted{rect.left, rect.top - shift, rect.right, rect.bottom - shift};
    target_->DrawText(text.data(), static_cast<UINT32>(text.size()), Format(style, align, wrap), shifted, brush_.Get(),
                      D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

ComPtr<ID2D1LinearGradientBrush> Canvas::Gradient(float left, float right, float y,
                                                  std::span<const D2D1_COLOR_F> colors) {
    if (colors.size() < 2) {
        return nullptr;
    }
    std::vector<D2D1_GRADIENT_STOP> stops(colors.size());
    for (size_t i = 0; i < colors.size(); ++i) {
        stops[i] = {static_cast<float>(i) / (colors.size() - 1), colors[i]};
    }
    ComPtr<ID2D1GradientStopCollection> collection;
    ComPtr<ID2D1LinearGradientBrush> gradient;
    if (FAILED(target_->CreateGradientStopCollection(stops.data(), static_cast<UINT32>(stops.size()), D2D1_GAMMA_2_2,
                                                   D2D1_EXTEND_MODE_CLAMP, &collection)) ||
        FAILED(target_->CreateLinearGradientBrush({{left, y}, {right, y}}, collection.Get(), &gradient))) {
        return nullptr;
    }
    return gradient;
}

void Canvas::FillRoundGradient(const D2D1_RECT_F& rect, float radius, std::span<const D2D1_COLOR_F> colors) {
    if (const auto gradient = Gradient(rect.left, rect.right, (rect.top + rect.bottom) / 2.0f, colors)) {
        target_->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), gradient.Get());
    }
}

void Canvas::StrokeCircleGradient(D2D1_POINT_2F center, float radius, std::span<const D2D1_COLOR_F> colors,
                                  float width) {
    if (const auto gradient = Gradient(center.x - radius, center.x + radius, center.y, colors)) {
        target_->DrawEllipse(D2D1::Ellipse(center, radius, radius), gradient.Get(), width);
    }
}

float Canvas::Measure(std::wstring_view text, TextStyle style) {
    ComPtr<IDWriteTextLayout> layout;
    if (text.empty() || FAILED(dwrite_->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()),
                                                         Format(style, TextAlign::Leading, false), 10000.0f,
                                                         100.0f, &layout))) {
        return 0.0f;
    }
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    return metrics.widthIncludingTrailingWhitespace;
}

float Canvas::MeasureHeight(std::wstring_view text, TextStyle style, float width) {
    ComPtr<IDWriteTextLayout> layout;
    if (text.empty() || FAILED(dwrite_->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()),
                                                         Format(style, TextAlign::Leading, true), width, 10000.0f,
                                                         &layout))) {
        return 0.0f;
    }
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    return metrics.height;
}

IDWriteTextFormat* Canvas::GlyphFormat(float size) {
    const int key = static_cast<int>(std::lround(size * 4));
    auto& format = glyphFormats_[key];
    if (!format) {
        dwrite_->CreateTextFormat(iconFamily_.c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                  DWRITE_FONT_STRETCH_NORMAL, size, kLocale, &format);
        if (format) {
            format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
    }
    return format.Get();
}

void Canvas::Glyph(wchar_t glyph, D2D1_POINT_2F center, float size, D2D1_COLOR_F color) {
    IDWriteTextFormat* format = GlyphFormat(size);
    if (!format) {
        return;
    }
    brush_->SetColor(color);
    const D2D1_RECT_F rect{center.x - size, center.y - size, center.x + size, center.y + size};
    target_->DrawText(&glyph, 1, format, rect, brush_.Get());
}

void Canvas::Bitmap(ID2D1Bitmap* bitmap, const D2D1_RECT_F& rect) {
    target_->DrawBitmap(bitmap, rect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}

ComPtr<ID2D1Bitmap> Canvas::BitmapFromIcon(HICON icon) {
    ComPtr<IWICBitmap> source;
    ComPtr<IWICFormatConverter> converter;
    ComPtr<ID2D1Bitmap> bitmap;
    if (!target_ || FAILED(wic_->CreateBitmapFromHICON(icon, &source)) ||
        FAILED(wic_->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(source.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr,
                                     0.0, WICBitmapPaletteTypeCustom)) ||
        FAILED(target_->CreateBitmapFromWicBitmap(converter.Get(), nullptr, &bitmap))) {
        return nullptr;
    }
    return bitmap;
}

void Canvas::PushClip(const D2D1_RECT_F& rect) {
    target_->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
}

void Canvas::PopClip() {
    target_->PopAxisAlignedClip();
}

void Canvas::PushRoundClip(const D2D1_RECT_F& rect, float radius) {
    ComPtr<ID2D1RoundedRectangleGeometry> geometry;
    d2d_->CreateRoundedRectangleGeometry(D2D1::RoundedRect(rect, radius, radius), &geometry);
    if (!clipLayer_) {
        target_->CreateLayer(&clipLayer_);
    }
    target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), geometry.Get()), clipLayer_.Get());
}

void Canvas::PopRoundClip() {
    target_->PopLayer();
}

}  // namespace ui
