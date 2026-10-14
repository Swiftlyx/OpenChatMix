#include "icon.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

constexpr int kSamples = 4;  // Supersampling per axis for anti-aliased edges.

struct Pill {
    float left, top, right, bottom;
    int color;  // 0 Game, 1 Chat.
};

// Inside test for a fully rounded rectangle (a capsule, or a circle when it is square).
bool Inside(const Pill& pill, float x, float y) {
    if (pill.bottom <= pill.top || x < pill.left || x > pill.right || y < pill.top || y > pill.bottom) {
        return false;
    }
    const float radius = std::min(pill.right - pill.left, pill.bottom - pill.top) / 2.0f;
    const float nearestX = std::clamp(x, pill.left + radius, pill.right - radius);
    const float nearestY = std::clamp(y, pill.top + radius, pill.bottom - radius);
    const float dx = x - nearestX;
    const float dy = y - nearestY;
    return dx * dx + dy * dy <= radius * radius;
}

}  // namespace

HICON CreateLevelIcon(int size, int game, int chat, const LevelIconColors& colors) {
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

    // Two bars standing on a common baseline; each is as tall as its level, down to a dot at 0%.
    const float s = static_cast<float>(size);
    const float width = s * 0.30f;
    const float gap = s * 0.14f;
    const float left = (s - 2.0f * width - gap) / 2.0f;
    const float top = s * 0.08f;
    const float bottom = s * 0.92f;
    auto barTop = [&](int level) { return bottom - width - (bottom - top - width) * std::clamp(level, 0, 100) / 100.0f; };
    const float right = left + width + gap;
    const Pill pills[] = {{left, barTop(game), left + width, bottom, 0}, {right, barTop(chat), right + width, bottom, 1}};
    const uint32_t rgb[2] = {colors.game & 0xFFFFFFu, colors.chat & 0xFFFFFFu};

    auto* pixels = static_cast<uint32_t*>(bits);
    for (int py = 0; py < size; ++py) {
        for (int px = 0; px < size; ++px) {
            int coverage[2] = {};
            for (int sy = 0; sy < kSamples; ++sy) {
                for (int sx = 0; sx < kSamples; ++sx) {
                    const float x = px + (sx + 0.5f) / kSamples;
                    const float y = py + (sy + 0.5f) / kSamples;
                    for (const Pill& pill : pills) {
                        if (Inside(pill, x, y)) {
                            ++coverage[pill.color];
                            break;
                        }
                    }
                }
            }
            const int total = coverage[0] + coverage[1];
            if (total == 0) {
                pixels[py * size + px] = 0u;
                continue;
            }
            // Straight (not premultiplied) alpha, as icon images use; edges where the colors meet mix them.
            auto channel = [&](int shift) {
                const uint32_t mixed = (((rgb[0] >> shift) & 0xFF) * coverage[0] + ((rgb[1] >> shift) & 0xFF) * coverage[1]) /
                                       total;
                return mixed << shift;
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
