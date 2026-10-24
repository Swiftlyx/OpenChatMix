// Level meters: the Game and Chat volumes with what plays on them, and the processed microphone.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "panel.h"
#include "panel_internal.h"

namespace ui {

using namespace detail;

namespace {

constexpr float kMeterFall = 0.86f;       // Per tick of kMeterIntervalMs: about 39 dB a second.
constexpr ULONGLONG kPeakHoldMs = 1200;   // How long the microphone's peak mark stays.
constexpr float kMeterFloorDb = -60.0f;   // Left end of the microphone scale.
constexpr float kSpeechDb = -12.0f;       // Above this the microphone meter turns tertiary...
constexpr float kHotDb = -3.0f;           // ...and above this, error: close to distortion.
constexpr float kTrackHeight = 12.0f;     // Volume slider track.
constexpr float kHandleHeight = 28.0f;
constexpr float kHandleGap = 4.0f;        // Between the handle and the track on each side.
constexpr float kMicBarHeight = 8.0f;

float ToDb(float sample) {
    return 20.0f * std::log10(std::max(sample, 1e-6f));
}

// Position of a level on the microphone scale, 0 at the floor and 1 at 0 dBFS.
float ScaleOf(float db) {
    return std::clamp((db - kMeterFloorDb) / -kMeterFloorDb, 0.0f, 1.0f);
}

}  // namespace

void Panel::TickMeters() {
    const bool groups = page_ == Page::Mixer || page_ == Page::Group;
    const bool mic = page_ == Page::Microphone && settings_.mic.enabled && settings_.mic.monitor;
    if (!groups && !mic) {
        if (meters_.game > 0.0f || meters_.chat > 0.0f || meters_.mic > 0.0f || meters_.media > 0.0f) {
            meters_ = {};
            micHold_ = 0.0f;
        }
        return;
    }
    // Peaks show at once and fall back slowly, as meters do.
    const Meters peaks = host_.ReadMeters();
    auto follow = [](float shown, float peak) {
        const float fallen = shown * kMeterFall;
        return std::max(std::clamp(peak, 0.0f, 1.0f), fallen < 0.001f ? 0.0f : fallen);
    };
    const Meters next{follow(meters_.game, peaks.game), follow(meters_.chat, peaks.chat), follow(meters_.mic, peaks.mic),
                      follow(meters_.media, peaks.media)};
    const ULONGLONG now = GetTickCount64();
    float hold = micHold_;
    if (next.mic >= hold) {
        hold = next.mic;
        micHoldTick_ = now;
    } else if (now - micHoldTick_ > kPeakHoldMs) {
        hold = std::max(next.mic, hold * kMeterFall);
    }
    // Compared where they are drawn: the microphone on its dB scale, where a small linear step near
    // the floor is still a visible move, so a falling meter never stalls between redraws.
    auto onScale = [](float sample) { return ScaleOf(ToDb(sample)); };
    const bool changed = std::fabs(next.game - meters_.game) > 0.002f || std::fabs(next.chat - meters_.chat) > 0.002f ||
                         std::fabs(next.media - meters_.media) > 0.002f ||
                         std::fabs(onScale(next.mic) - onScale(meters_.mic)) > 0.002f ||
                         std::fabs(onScale(hold) - onScale(micHold_)) > 0.002f;
    meters_ = next;
    micHold_ = hold;
    if (changed) {
        Invalidate();
    }
}

void Panel::DrawVolumeSlider(uint32_t id, float left, float right, float centerY, int volume, float meter,
                             D2D1_COLOR_F accent, D2D1_COLOR_F container, std::function<void(int)> set) {
    const Palette& p = P();
    const float width = right - left;
    const float x = left + width * std::clamp(volume, 0, 100) / 100.0f;
    const float half = kTrackHeight / 2.0f;
    // Inactive track, the volume as the active track, and inside it what plays right now.
    if (x + kHandleGap < right) {
        canvas_.FillRound({x + kHandleGap, centerY - half, right, centerY + half}, half, p.surfaceContainerHighest);
    }
    if (x - kHandleGap > left) {
        const float activeRight = x - kHandleGap;
        canvas_.FillRound({left, centerY - half, activeRight, centerY + half}, half, container);
        const float level = std::min(left + width * std::clamp(meter, 0.0f, 1.0f), activeRight);
        if (level - left > 1.0f) {
            canvas_.FillRound({left, centerY - half, level, centerY + half}, half, accent);
        }
    }
    // The handle narrows while held, like the other Material 3 Expressive sliders here.
    const float handle = Animate(AnimKey(id, 1), pressedId_ == id ? 2.0f : 4.0f, design::kSpringFastSpatial);
    canvas_.FillRound({x - handle / 2.0f, centerY - kHandleHeight / 2.0f, x + handle / 2.0f, centerY + kHandleHeight / 2.0f},
                      handle / 2.0f, accent);

    Hit hit;
    hit.rect = {left - design::kSpace2, centerY - kHandleHeight / 2.0f, right + design::kSpace2,
                centerY + kHandleHeight / 2.0f};
    hit.id = id;
    hit.drag = [left, width, set](float pointerX, float) {
        set(static_cast<int>(std::lround(std::clamp((pointerX - left) / width, 0.0f, 1.0f) * 100.0f)));
    };
    hit.wheel = [volume, set](int notches) { set(std::clamp(volume + notches * 5, 0, 100)); };
    hit.doubleClick = [set] { set(100); };
    AddHit(std::move(hit));
}

Panel::CardRow Panel::MicLevelItem() {
    const float hintHeight = HintHeight(T(Text::MicLevelHint));
    return {78.0f + hintHeight, [this, hintHeight](float top) {
                const Palette& p = P();
                const float holdDb = ToDb(micHold_);
                wchar_t text[24];
                if (micHold_ < 0.001f) {
                    swprintf_s(text, L"—");
                } else {
                    swprintf_s(text, L"%s%.1f dB", holdDb < -0.05f ? L"−" : L"", std::fabs(holdDb));
                }
                canvas_.Text(T(Text::MicLevel), {kInset, top + design::kHintBottomGap, kWidth - 120.0f, top + 34.0f},
                             TextStyle::BodyLarge, p.onSurface);
                canvas_.Text(text, {kWidth - 120.0f, top + design::kHintBottomGap, kWidth - kInset, top + 34.0f},
                             TextStyle::LabelLarge, holdDb > kHotDb && micHold_ >= 0.001f ? p.error : p.onSurfaceVariant,
                             TextAlign::Trailing);

                // A dB scale up to 0 dBFS: speech zone, loud zone and the last few dB before distortion.
                const float left = kInset;
                const float width = kWidth - 2.0f * kInset;
                const float barTop = top + 42.0f;
                const D2D1_RECT_F bar{left, barTop, left + width, barTop + kMicBarHeight};
                canvas_.FillRound(bar, design::kRadiusExtraSmall / 2.0f, p.surfaceContainerHighest);
                // Readings arrive every kMeterIntervalMs; a fast effects spring carries the bar and the
                // peak mark between them at the display's frame rate.
                const float level =
                    Animate(AnimKey(kIdMicLevel, 0), ScaleOf(ToDb(meters_.mic)), design::kSpringFastEffects);
                const float holdShown = Animate(AnimKey(kIdMicLevel, 1),
                                                micHold_ >= 0.001f ? ScaleOf(holdDb) : 0.0f, design::kSpringFastEffects);
                struct Zone {
                    float from;
                    float to;
                    D2D1_COLOR_F color;
                };
                const Zone zones[] = {{0.0f, ScaleOf(kSpeechDb), p.primary},
                                      {ScaleOf(kSpeechDb), ScaleOf(kHotDb), p.tertiary},
                                      {ScaleOf(kHotDb), 1.0f, p.error}};
                for (const Zone& zone : zones) {
                    const float end = std::min(level, zone.to);
                    if (end > zone.from) {
                        canvas_.FillRound({left + width * zone.from, bar.top, left + width * end, bar.bottom},
                                          design::kRadiusExtraSmall / 2.0f, zone.color);
                    }
                }
                if (holdShown > 0.001f) {
                    const float holdX = left + width * holdShown;
                    canvas_.FillRect({holdX - 1.0f, bar.top - 2.0f, holdX + 1.0f, bar.bottom + 2.0f}, p.onSurface);
                }
                // Marks every 12 dB.
                for (int db = -48; db <= 0; db += 12) {
                    const float markX = left + width * ScaleOf(static_cast<float>(db));
                    const std::wstring label = db == 0 ? L"0 dB" : L"−" + std::to_wstring(-db);
                    canvas_.Text(label, {markX - 24.0f, bar.bottom + 3.0f, markX + (db == 0 ? 0.0f : 24.0f), bar.bottom + 19.0f},
                                 TextStyle::LabelSmall, p.onSurfaceVariant,
                                 db == 0 ? TextAlign::Trailing : TextAlign::Center);
                }
                DrawHint(T(Text::MicLevelHint), top + 78.0f, hintHeight);
            }};
}

}  // namespace ui
