#include "visualizer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

namespace {

constexpr size_t kFftSize = 4096;        // About 12 Hz per bin at 48 kHz; narrower bass bands are interpolated.
constexpr int kBars = 32;
constexpr int kBarPitch = kOledWidth / kBars;  // A bar three pixels wide and a gap of one.
constexpr int kBarWidth = kBarPitch - 1;
constexpr double kLowestHz = 20.0;
constexpr double kHighestHz = 20000.0;
constexpr double kFloorDb = -62.0;       // An empty bar.
constexpr double kCeilingDb = -8.0;      // A full bar; music rarely puts more into one band.
constexpr double kTiltDbPerOctave = 3.0; // Music has less energy up high; the tilt keeps the right side lively.
constexpr float kRise = 0.65f;           // Share of the way up a bar goes in one frame.
constexpr float kFallPerSecond = 90.0f;  // Pixels a bar falls per second.
constexpr float kPeakHoldMs = 450.0f;
constexpr float kPeakFallPerSecond = 40.0f;
constexpr DWORD kFrameMs = 33;           // The GameDAC takes about 60 ms for a whole frame anyway.

using Complex = std::complex<float>;

// In-place radix-2 FFT.
void Fft(std::vector<Complex>& data) {
    const size_t n = data.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(data[i], data[j]);
        }
    }
    for (size_t length = 2; length <= n; length <<= 1) {
        const float angle = -2.0f * std::numbers::pi_v<float> / static_cast<float>(length);
        const Complex step(std::cos(angle), std::sin(angle));
        for (size_t start = 0; start < n; start += length) {
            Complex w(1.0f, 0.0f);
            for (size_t k = 0; k < length / 2; ++k) {
                const Complex even = data[start + k];
                const Complex odd = data[start + k + length / 2] * w;
                data[start + k] = even + odd;
                data[start + k + length / 2] = even - odd;
                w *= step;
            }
        }
    }
}

class Visualizer final : public IdleAnimation {
public:
    explicit Visualizer(std::shared_ptr<const AudioTap> tap) : tap_(std::move(tap)) {
        window_.resize(kFftSize);
        for (size_t i = 0; i < kFftSize; ++i) {
            window_[i] = 0.5f - 0.5f * std::cos(2.0f * std::numbers::pi_v<float> * static_cast<float>(i) /
                                                 static_cast<float>(kFftSize - 1));
        }
        samples_.resize(kFftSize);
        spectrum_.resize(kFftSize);
    }

    DWORD Render(ULONGLONG elapsedMs, OledBitmap& frame) override {
        // The animation's clock starts again each time it comes back on the display.
        const float seconds = elapsedMs > lastMs_ ? static_cast<float>(elapsedMs - lastMs_) / 1000.0f : 0.0f;
        lastMs_ = elapsedMs;
        const int rate = tap_->Latest(samples_.data(), kFftSize);
        std::array<float, kBars> targets{};
        if (rate > 0) {
            for (size_t i = 0; i < kFftSize; ++i) {
                spectrum_[i] = Complex(samples_[i] * window_[i], 0.0f);
            }
            Fft(spectrum_);
            // A sine at full scale reads 1: the window halves the amplitude, half the energy sits in the mirror.
            const float scale = 4.0f / static_cast<float>(kFftSize);
            for (size_t bin = 0; bin < kFftSize / 2; ++bin) {
                amplitudes_[bin] = std::abs(spectrum_[bin]) * scale;
            }
            const double binHz = static_cast<double>(rate) / kFftSize;
            for (int bar = 0; bar < kBars; ++bar) {
                const double low = kLowestHz * std::pow(kHighestHz / kLowestHz, static_cast<double>(bar) / kBars);
                const double high = kLowestHz * std::pow(kHighestHz / kLowestHz, static_cast<double>(bar + 1) / kBars);
                const double db = 20.0 * std::log10(BandAmplitude(low, high, binHz) + 1e-9) +
                                  kTiltDbPerOctave * std::log2(std::sqrt(low * high) / 1000.0);
                targets[bar] = static_cast<float>(
                    std::clamp((db - kFloorDb) / (kCeilingDb - kFloorDb), 0.0, 1.0) * kOledHeight);
            }
        }
        for (int bar = 0; bar < kBars; ++bar) {
            float& height = heights_[bar];
            if (targets[bar] > height) {
                height += (targets[bar] - height) * kRise;
            } else {
                height = std::max(targets[bar], height - kFallPerSecond * seconds);
            }
            // The peak mark waits a moment at the highest point, then sinks to the bar.
            if (height >= peaks_[bar]) {
                peaks_[bar] = height;
                peakHeld_[bar] = 0.0f;
            } else if ((peakHeld_[bar] += seconds * 1000.0f) > kPeakHoldMs) {
                peaks_[bar] = std::max(height, peaks_[bar] - kPeakFallPerSecond * seconds);
            }
            const int left = bar * kBarPitch;
            const int top = kOledHeight - static_cast<int>(std::lround(height));
            for (int y = std::max(0, top); y < kOledHeight; ++y) {
                for (int x = 0; x < kBarWidth; ++x) {
                    frame.Set(left + x, y);
                }
            }
            const int peak = kOledHeight - 1 - static_cast<int>(std::lround(peaks_[bar]));
            if (peaks_[bar] >= 1.0f && peak < top - 1) {
                for (int x = 0; x < kBarWidth; ++x) {
                    frame.Set(left + x, std::max(0, peak));
                }
            }
        }
        return kFrameMs;
    }

private:
    // The loudest bin within the band; a band narrower than a bin reads between its neighbours.
    float BandAmplitude(double low, double high, double binHz) const {
        const size_t last = kFftSize / 2 - 1;
        const size_t first = std::clamp<size_t>(static_cast<size_t>(std::ceil(low / binHz)), 1, last);
        const size_t end = std::clamp<size_t>(static_cast<size_t>(std::floor(high / binHz)), 1, last);
        if (first <= end) {
            return *std::max_element(amplitudes_.begin() + static_cast<std::ptrdiff_t>(first),
                                     amplitudes_.begin() + static_cast<std::ptrdiff_t>(end) + 1);
        }
        const double center = std::sqrt(low * high) / binHz;
        const size_t below = std::clamp<size_t>(static_cast<size_t>(center), 1, last - 1);
        const float share = static_cast<float>(std::clamp(center - static_cast<double>(below), 0.0, 1.0));
        return amplitudes_[below] * (1.0f - share) + amplitudes_[below + 1] * share;
    }

    std::shared_ptr<const AudioTap> tap_;
    std::vector<float> window_;
    std::vector<float> samples_;
    std::vector<Complex> spectrum_;
    std::array<float, kFftSize / 2> amplitudes_{};
    std::array<float, kBars> heights_{};
    std::array<float, kBars> peaks_{};
    std::array<float, kBars> peakHeld_{};
    ULONGLONG lastMs_ = 0;
};

}  // namespace

std::shared_ptr<IdleAnimation> CreateVisualizer(std::shared_ptr<const AudioTap> tap) {
    return std::make_shared<Visualizer>(std::move(tap));
}
