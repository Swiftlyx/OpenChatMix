#pragma once

#include <windows.h>

#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <array>
#include <initializer_list>
#include <map>
#include <span>
#include <string>
#include <string_view>

namespace ui {

enum class TextStyle {
    TitleLarge,
    TitleMedium,
    TitleSmall,
    BodyLarge,
    BodyMedium,
    BodySmall,
    LabelLarge,
    LabelMedium,
    LabelSmall,
    Number,
    Count,
};

enum class TextAlign { Leading, Center, Trailing };

// Thin Direct2D/DirectWrite wrapper. Coordinates are DIPs; text is single-line with ellipsis
// unless wrap is requested, and always centered vertically in its rectangle.
class Canvas {
public:
    bool Init();
    // Releases factories and fonts; Init() must run again before drawing.
    void Shutdown();
    bool Attach(HWND window, float dpi);
    // Renders into a memory DC (premultiplied 32-bit DIB), e.g. for UpdateLayeredWindow.
    bool AttachDC(float dpi);
    bool BindDC(HDC dc, const RECT& rect);
    void Detach();
    bool IsAttached() const { return target_ != nullptr; }
    void Resize(UINT width, UINT height);

    void Begin();
    HRESULT End();

    void Clear(D2D1_COLOR_F color);
    void FillRect(const D2D1_RECT_F& rect, D2D1_COLOR_F color);
    void FillRound(const D2D1_RECT_F& rect, float radius, D2D1_COLOR_F color);
    void StrokeRound(const D2D1_RECT_F& rect, float radius, D2D1_COLOR_F color, float width);
    void FillCircle(D2D1_POINT_2F center, float radius, D2D1_COLOR_F color);
    void StrokeCircle(D2D1_POINT_2F center, float radius, D2D1_COLOR_F color, float width);
    void Line(D2D1_POINT_2F from, D2D1_POINT_2F to, D2D1_COLOR_F color, float width);
    void Polyline(std::span<const D2D1_POINT_2F> points, D2D1_COLOR_F color, float width);
    void FillPolygon(std::span<const D2D1_POINT_2F> points, D2D1_COLOR_F color);
    void Text(std::wstring_view text, const D2D1_RECT_F& rect, TextStyle style, D2D1_COLOR_F color,
              TextAlign align = TextAlign::Leading, bool wrap = false);
    // Rounded rectangle with a horizontal gradient through evenly spaced colors.
    void FillRoundGradient(const D2D1_RECT_F& rect, float radius, std::span<const D2D1_COLOR_F> colors);
    float Measure(std::wstring_view text, TextStyle style);
    // Height of the text wrapped at width.
    float MeasureHeight(std::wstring_view text, TextStyle style, float width);
    void Glyph(wchar_t glyph, D2D1_POINT_2F center, float size, D2D1_COLOR_F color);
    void Bitmap(ID2D1Bitmap* bitmap, const D2D1_RECT_F& rect);
    Microsoft::WRL::ComPtr<ID2D1Bitmap> BitmapFromIcon(HICON icon);

    void PushClip(const D2D1_RECT_F& rect);
    void PopClip();
    // Clips to a rounded rectangle; not nestable.
    void PushRoundClip(const D2D1_RECT_F& rect, float radius);
    void PopRoundClip();

private:
    IDWriteTextFormat* Format(TextStyle style, TextAlign align, bool wrap);
    IDWriteTextFormat* GlyphFormat(float size);
    std::wstring ResolveFamily(std::initializer_list<const wchar_t*> candidates);

    Microsoft::WRL::ComPtr<ID2D1Factory> d2d_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwrite_;
    Microsoft::WRL::ComPtr<IWICImagingFactory> wic_;
    Microsoft::WRL::ComPtr<ID2D1RenderTarget> target_;
    std::array<float, static_cast<size_t>(TextStyle::Count)> textShift_{};  // DIPs to move centered text up.
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> hwndTarget_;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> dcTarget_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
    Microsoft::WRL::ComPtr<ID2D1Layer> clipLayer_;
    Microsoft::WRL::ComPtr<ID2D1StrokeStyle> roundStroke_;
    std::array<Microsoft::WRL::ComPtr<IDWriteTextFormat>, static_cast<size_t>(TextStyle::Count)> formats_;
    std::map<int, Microsoft::WRL::ComPtr<IDWriteTextFormat>> glyphFormats_;
    std::wstring iconFamily_;
};

}  // namespace ui
