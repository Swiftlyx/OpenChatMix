#include "icon.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

constexpr int kSamples = 4;  // Supersampling per axis for anti-aliased edges.
constexpr float kPi = 3.14159265f;
constexpr int kArcSegments = 16;  // Polyline steps of a bar bending into half of the ring.

// Proportions of the icon, as fractions of its size.
constexpr float kBarWidth = 0.30f;  // Two standing bars...
constexpr float kBarGap = 0.14f;
constexpr float kBarTop = 0.08f;
constexpr float kBarBottom = 0.92f;
constexpr float kEqualsWidth = 0.26f;   // ...that turn into the lying bars of the equals sign,
constexpr float kSquareInset = 0.12f;   // ...widen into a rounded square
constexpr float kSquareRadius = 0.20f;
constexpr float kSquareCloseAt = 0.75f; // Share of the morph after which the two halves touch.
constexpr float kRingOuter = 0.42f;     // ...or bend into the two halves of a ring.
constexpr float kRingWidth = 0.15f;
constexpr float kBlendBand = 0.36f;     // Where the square and the ring blend Game into Chat.
constexpr float kBatteryLeft = 0.04f;     // The battery lies on its side, as in Material 3 Expressive:
constexpr float kBatteryRight = 0.86f;    // a rounded body...
constexpr float kBatteryTop = 0.24f;
constexpr float kBatteryBottom = 0.76f;
constexpr float kBatteryRadius = 0.17f;
constexpr float kCapRight = 0.97f;        // ...the terminal on the right...
constexpr float kCapTop = 0.38f;
constexpr float kCapBottom = 0.62f;
constexpr float kEmptyOpacity = 0.40f;    // ...and the empty part in the same color, faded.
constexpr float kBatteryCloseAt = 0.45f;  // Share of the morph after which the halves of the body touch,
constexpr float kDetailFrom = 0.50f;      // ...and the terminal, the charge and the bolt appear.
// Lightning bolt while charging, as fractions of the body.
constexpr float kBolt[][2] = {{0.57f, 0.06f}, {0.35f, 0.56f}, {0.49f, 0.56f}, {0.43f, 0.94f},
                              {0.67f, 0.42f}, {0.53f, 0.42f}};

float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

struct Box {
    float left, top, right, bottom;
};

Box Lerp(const Box& a, const Box& b, float t) {
    return {Lerp(a.left, b.left, t), Lerp(a.top, b.top, t), Lerp(a.right, b.right, t), Lerp(a.bottom, b.bottom, t)};
}

// Inside test for a rounded rectangle whose left and right corners may have radii of their own.
bool InsideBox(const Box& box, float x, float y, float leftRadius, float rightRadius) {
    if (box.bottom <= box.top || x < box.left || x > box.right || y < box.top || y > box.bottom) {
        return false;
    }
    const float side = x < (box.left + box.right) / 2.0f ? leftRadius : rightRadius;
    const float radius = std::min({side, (box.right - box.left) / 2.0f, (box.bottom - box.top) / 2.0f});
    const float nearestX = std::clamp(x, box.left + radius, box.right - radius);
    const float nearestY = std::clamp(y, box.top + radius, box.bottom - radius);
    const float dx = x - nearestX;
    const float dy = y - nearestY;
    return dx * dx + dy * dy <= radius * radius;
}

// Squared distance from (x, y) to the segment from (ax, ay) to (bx, by).
float SegmentDistance2(float x, float y, float ax, float ay, float bx, float by) {
    const float vx = bx - ax;
    const float vy = by - ay;
    const float length2 = vx * vx + vy * vy;
    const float t = length2 > 0.0f ? std::clamp(((x - ax) * vx + (y - ay) * vy) / length2, 0.0f, 1.0f) : 0.0f;
    const float dx = x - (ax + vx * t);
    const float dy = y - (ay + vy * t);
    return dx * dx + dy * dy;
}

// Even-odd inside test for a polygon given in fractions of the icon size.
template <size_t N>
bool InsidePolygon(const float (&points)[N][2], float size, float x, float y) {
    bool inside = false;
    for (size_t i = 0, j = N - 1; i < N; j = i++) {
        const float xi = points[i][0] * size;
        const float yi = points[i][1] * size;
        const float xj = points[j][0] * size;
        const float yj = points[j][1] * size;
        if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) {
            inside = !inside;
        }
    }
    return inside;
}

// What covers a point: nothing, or a color made of Game, Chat and the battery color.
struct Paint {
    bool inside = false;
    float chat = 0.0f;     // Share of Chat against Game.
    float battery = 0.0f;  // Share of the battery color against both.
    float opacity = 1.0f;
};

Paint SideColor(float chatShare) {
    return {chatShare >= 0.0f, chatShare, 0.0f};
}

// The icon at one moment: the bar levels and how far the bars have turned into the mark.
class LevelShape {
public:
    LevelShape(float size, float game, float chat, BalancedIcon mark, float morph, BatteryMark battery)
        : s_(size), mark_(mark), t_(mark == BalancedIcon::Bars ? 0.0f : std::clamp(morph, 0.0f, 1.0f)),
          battery_(battery) {
        levels_[0] = std::clamp(game, 0.0f, 100.0f) / 100.0f;
        levels_[1] = std::clamp(chat, 0.0f, 100.0f) / 100.0f;
        battery_.charge = std::clamp(battery_.charge, 0.0f, 1.0f);
    }

    Paint ColorAt(float x, float y) const {
        switch (mark_) {
        case BalancedIcon::Equals: return SideColor(EqualsAt(x, y));
        case BalancedIcon::Square: return SideColor(SquareAt(x, y));
        case BalancedIcon::Ring: return SideColor(RingAt(x, y));
        case BalancedIcon::Battery: return BatteryAt(x, y);
        case BalancedIcon::Bars: break;
        }
        return SideColor(BarsAt(x, y, kBarWidth * s_));
    }

private:
    // Center line of a standing bar of the given width, as a stroke with round ends: x, and the top
    // and bottom of the line. At 0% the bar is a dot.
    struct Stroke {
        float x, top, bottom;
    };
    Stroke BarStroke(int side, float width) const {
        const float gap = kBarGap * s_;
        const float x = s_ / 2.0f + (side == 0 ? -1.0f : 1.0f) * (width + gap) / 2.0f;
        const float bottom = kBarBottom * s_ - width / 2.0f;
        const float travel = (kBarBottom - kBarTop) * s_ - width;
        return {x, bottom - travel * levels_[side], bottom};
    }

    float BarsAt(float x, float y, float width) const {
        for (int side = 0; side < 2; ++side) {
            const Stroke bar = BarStroke(side, width);
            if (SegmentDistance2(x, y, bar.x, bar.top, bar.x, bar.bottom) <= width * width / 4.0f) {
                return static_cast<float>(side);
            }
        }
        return -1.0f;
    }

    // The square and the ring are one shape, so the colors blend smoothly where they meet; while the
    // bars turn into them each side keeps its own color until the shape is complete.
    float Blend(int side, float x) const {
        const float band = kBlendBand * s_;
        const float u = std::clamp((x - s_ / 2.0f + band / 2.0f) / band, 0.0f, 1.0f);
        return Lerp(static_cast<float>(side), u * u * (3.0f - 2.0f * u), t_);
    }

    // The bars turn a quarter clockwise about the center, Game to the top, and thin a little.
    float EqualsAt(float x, float y) const {
        const float angle = t_ * kPi / 2.0f;
        const float c = std::cos(angle);
        const float sn = std::sin(angle);
        const float dx = x - s_ / 2.0f;
        const float dy = y - s_ / 2.0f;
        return BarsAt(s_ / 2.0f + dx * c + dy * sn, s_ / 2.0f - dx * sn + dy * c, Lerp(kBarWidth, kEqualsWidth, t_) * s_);
    }

    // The bars widen toward each other until they close into a rounded square, each half square on
    // the inside and round on the outside.
    float SquareAt(float x, float y) const {
        const float width = kBarWidth * s_;
        const float inset = kSquareInset * s_;
        const float outer = Lerp(width / 2.0f, kSquareRadius * s_, t_);
        const float inner = Lerp(width / 2.0f, 0.0f, t_);
        // The inner edges meet a little before the rest of the shape settles, so no seam of the
        // background shows between the halves while they close.
        const float closing = std::min(1.0f, t_ / kSquareCloseAt);
        for (int side = 0; side < 2; ++side) {
            const Stroke bar = BarStroke(side, width);
            const Box from{bar.x - width / 2.0f, bar.top - width / 2.0f, bar.x + width / 2.0f, bar.bottom + width / 2.0f};
            const Box to = side == 0 ? Box{inset, inset, s_ / 2.0f, s_ - inset} : Box{s_ / 2.0f, inset, s_ - inset, s_ - inset};
            Box box = Lerp(from, to, t_);
            if (side == 0) {
                box.right = Lerp(from.right, to.right, closing);
            } else {
                box.left = Lerp(from.left, to.left, closing);
            }
            if (InsideBox(box, x, y, side == 0 ? outer : inner, side == 0 ? inner : outer)) {
                return Blend(side, x);
            }
        }
        return -1.0f;
    }

    // Each bar bends into half of the ring, Game the left half and Chat the right one; the halves meet
    // at the top and the bottom.
    float RingAt(float x, float y) const {
        const float barWidth = kBarWidth * s_;
        const float outer = kRingOuter * s_;
        const float ringWidth = std::max(1.6f, kRingWidth * s_);
        const float radius = outer - ringWidth / 2.0f;
        const float width = Lerp(barWidth, ringWidth, t_);
        const float limit = width * width / 4.0f;
        for (int side = 0; side < 2; ++side) {
            const Stroke bar = BarStroke(side, barWidth);
            const float turn = side == 0 ? -1.0f : 1.0f;
            float previousX = 0.0f;
            float previousY = 0.0f;
            for (int step = 0; step <= kArcSegments; ++step) {
                const float u = static_cast<float>(step) / kArcSegments;
                const float angle = -kPi / 2.0f + turn * u * kPi;
                const float px = Lerp(bar.x, s_ / 2.0f + radius * std::cos(angle), t_);
                const float py = Lerp(Lerp(bar.top, bar.bottom, u), s_ / 2.0f + radius * std::sin(angle), t_);
                if (step > 0 && SegmentDistance2(x, y, previousX, previousY, px, py) <= limit) {
                    return Blend(side, x);
                }
                previousX = px;
                previousY = py;
            }
        }
        return -1.0f;
    }

    // The bars close into the two halves of the battery body as they do into the square; then the
    // terminal grows on the right and the part past the charge fades. The colors turn into the
    // battery color on the way. While charging, a bolt shows in the opposite tone of what lies under it.
    Paint BatteryAt(float x, float y) const {
        const float width = kBarWidth * s_;
        const Box body{kBatteryLeft * s_, kBatteryTop * s_, kBatteryRight * s_, kBatteryBottom * s_};
        const float middle = (body.left + body.right) / 2.0f;
        const float radius = kBatteryRadius * s_;
        const float outer = Lerp(width / 2.0f, radius, t_);
        const float inner = Lerp(width / 2.0f, 0.0f, t_);
        const float closing = std::min(1.0f, t_ / kBatteryCloseAt);
        int side = -1;
        for (int candidate = 0; candidate < 2 && side < 0; ++candidate) {
            const Stroke bar = BarStroke(candidate, width);
            const Box from{bar.x - width / 2.0f, bar.top - width / 2.0f, bar.x + width / 2.0f, bar.bottom + width / 2.0f};
            const Box to = candidate == 0 ? Box{body.left, body.top, middle, body.bottom}
                                          : Box{middle, body.top, body.right, body.bottom};
            Box box = Lerp(from, to, t_);
            if (candidate == 0) {
                box.right = Lerp(from.right, to.right, closing);
            } else {
                box.left = Lerp(from.left, to.left, closing);
            }
            if (InsideBox(box, x, y, candidate == 0 ? outer : inner, candidate == 0 ? inner : outer)) {
                side = candidate;
            }
        }
        const float u = std::clamp((t_ - kDetailFrom) / (1.0f - kDetailFrom), 0.0f, 1.0f);
        const float grow = u * u * (3.0f - 2.0f * u);
        const Box cap{body.right - 1.0f, kCapTop * s_, body.right + (kCapRight * s_ - body.right) * grow, kCapBottom * s_};
        const bool onCap = grow > 0.0f && InsideBox(cap, x, y, 0.0f, 0.06f * s_);
        if (side < 0 && !onCap) {
            return {};
        }
        Paint paint{true, side < 0 ? 0.5f : Blend(side, x), t_};
        bool charged = side >= 0 ? x <= body.left + (body.right - body.left) * battery_.charge : battery_.charge >= 1.0f;
        if (battery_.charging && side >= 0 && grow > 0.0f) {
            // The bolt grows from the middle of the body: the point is tested scaled back toward it.
            const float centerY = (body.top + body.bottom) / 2.0f;
            const float bx = (middle + (x - middle) / grow - body.left) / (body.right - body.left);
            const float by = (centerY + (y - centerY) / grow - body.top) / (body.bottom - body.top);
            if (InsidePolygon(kBolt, 1.0f, bx, by)) {
                charged = !charged;
            }
        }
        paint.opacity = charged ? 1.0f : Lerp(1.0f, kEmptyOpacity, grow);
        return paint;
    }

    float s_;
    BalancedIcon mark_;
    float t_;
    BatteryMark battery_;
    float levels_[2];
};

}  // namespace

HICON CreateLevelIcon(int size, float game, float chat, const LevelIconColors& colors, BalancedIcon mark,
                      float morph, BatteryMark battery) {
    size = std::max(size, 8);

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;  // Top-down rows.
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP color = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!color) {
        return nullptr;
    }

    const LevelShape shape(static_cast<float>(size), game, chat, mark, morph, battery);
    const uint32_t rgb[3] = {colors.game & 0xFFFFFFu, colors.chat & 0xFFFFFFu, colors.battery & 0xFFFFFFu};

    auto* pixels = static_cast<uint32_t*>(bits);
    for (int py = 0; py < size; ++py) {
        for (int px = 0; px < size; ++px) {
            float coverage[3] = {};
            for (int sy = 0; sy < kSamples; ++sy) {
                for (int sx = 0; sx < kSamples; ++sx) {
                    const float x = px + (sx + 0.5f) / kSamples;
                    const float y = py + (sy + 0.5f) / kSamples;
                    if (const Paint paint = shape.ColorAt(x, y); paint.inside) {
                        coverage[0] += (1.0f - paint.chat) * (1.0f - paint.battery) * paint.opacity;
                        coverage[1] += paint.chat * (1.0f - paint.battery) * paint.opacity;
                        coverage[2] += paint.battery * paint.opacity;
                    }
                }
            }
            const float total = coverage[0] + coverage[1] + coverage[2];
            if (total <= 0.0f) {
                pixels[py * size + px] = 0u;
                continue;
            }
            // Straight (not premultiplied) alpha, as icon images use; where the colors meet they mix.
            auto channel = [&](int shift) {
                const float mixed = (((rgb[0] >> shift) & 0xFF) * coverage[0] + ((rgb[1] >> shift) & 0xFF) * coverage[1] +
                                     ((rgb[2] >> shift) & 0xFF) * coverage[2]) /
                                    total;
                return static_cast<uint32_t>(std::lround(mixed)) << shift;
            };
            const auto alpha = static_cast<uint32_t>(std::lround(255.0f * total / (kSamples * kSamples)));
            pixels[py * size + px] = (alpha << 24) | channel(16) | channel(8) | channel(0);
        }
    }
    GdiFlush();

    // Monochrome rows are WORD-aligned; an all-zero mask defers transparency to the alpha channel.
    std::vector<uint8_t> maskBits(static_cast<size_t>((size + 15) / 16) * 2 * size, 0);
    HBITMAP mask = CreateBitmap(size, size, 1, 1, maskBits.data());

    HICON icon = nullptr;
    if (mask) {
        ICONINFO iconInfo{TRUE, 0, 0, mask, color};
        icon = CreateIconIndirect(&iconInfo);
        DeleteObject(mask);
    }
    DeleteObject(color);
    return icon;
}
