#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

// Audio between two devices that run on their own clocks. A capture device and a render device never
// tick at exactly the same rate, so a plain queue slowly fills up or runs dry; cutting it back or
// running out is heard as a click. This queue instead plays its input a hair faster or slower to hold
// the latency at a target, resampling with a cubic Hermite interpolator. Real devices differ by well
// under 0.1%, which no one hears; up to 1% is allowed for odd ones. Single-threaded: the audio thread
// pushes, steers and pops.
class DriftQueue {
public:
    // The most the playback speed is changed, and how strongly latency errors steer it.
    static constexpr double kMaxSpeedChange = 0.01;
    static constexpr double kSteerPerSecond = 0.3;  // 10 ms too much latency → 0.3% faster.
    static constexpr double kSettlePerSecond = 0.1; // A lasting error builds up a lasting correction.
    static constexpr double kSmoothingSeconds = 0.5;

    DriftQueue(int channels, size_t capacityFrames, double rate)
        : samples_(capacityFrames * channels), capacity_(capacityFrames), channels_(channels), rate_(rate) {}

    // Frames not yet played.
    double Size() const { return std::max(0.0, static_cast<double>(size_) - position_); }
    double Speed() const { return speed_; }

    // Copies frames, mapping source channels onto this queue's layout; null data means silence.
    void Push(const float* data, size_t frames, int sourceChannels) {
        for (size_t frame = 0; frame < frames; ++frame) {
            if (size_ == capacity_) {
                Discard(1);
            }
            float* out = &samples_[((head_ + size_) % capacity_) * channels_];
            for (int channel = 0; channel < channels_; ++channel) {
                float value = 0.0f;
                if (data) {
                    const int source = channel < sourceChannels ? channel : (sourceChannels == 1 ? 0 : -1);
                    value = source >= 0 ? data[frame * sourceChannels + source] : 0.0f;
                }
                out[channel] = value;
            }
            ++size_;
        }
    }

    // Adjusts the playback speed toward latencyFrames (what is queued here plus what the device still
    // holds) settling at targetFrames. elapsedSeconds is the time since the last call.
    void Steer(double latencyFrames, double targetFrames, double elapsedSeconds) {
        if (!steered_) {
            smoothed_ = latencyFrames;
            steered_ = true;
        }
        smoothed_ += (latencyFrames - smoothed_) * std::min(1.0, elapsedSeconds / kSmoothingSeconds);
        // Proportional and integral: the integral learns the devices' clock difference, so the latency
        // settles at the target instead of beside it.
        const double error = (smoothed_ - targetFrames) / rate_;
        integral_ = std::clamp(integral_ + error * kSettlePerSecond * elapsedSeconds, -kMaxSpeedChange, kMaxSpeedChange);
        speed_ = 1.0 + std::clamp(error * kSteerPerSecond + integral_, -kMaxSpeedChange, kMaxSpeedChange);
    }

    // Skips audio, e.g. after the system stalled and far too much piled up. Steering restarts.
    void Drop(double frames) {
        position_ += std::min(frames, Size());
        Release();
        steered_ = false;
    }

    // The source stopped delivering, as loopback capture does while no app plays on the device.
    // Steering keeps the learned clock difference instead of chasing the empty queue, which would wind
    // the speed up to its limit, and restarts with the next Steer() call.
    void Hold() {
        Drop(Size());
        speed_ = 1.0 + integral_;
    }

    // Writes up to frames frames with a linear gain ramp; returns how many it had. Each output frame
    // advances the input by Speed().
    size_t Pop(float* out, size_t frames, float fromGain, float toGain) {
        size_t written = 0;
        for (; written < frames; ++written) {
            const size_t index = static_cast<size_t>(position_);
            if (index + 2 >= size_) {
                break;  // The interpolator needs one frame before and two after.
            }
            const float t = static_cast<float>(position_ - index);
            const float gain = fromGain + (toGain - fromGain) * (written + 1) / static_cast<float>(frames);
            for (int channel = 0; channel < channels_; ++channel) {
                const float y0 = At(index == 0 ? 0 : index - 1, channel);
                const float y1 = At(index, channel);
                const float y2 = At(index + 1, channel);
                const float y3 = At(index + 2, channel);
                out[written * channels_ + channel] = Hermite(y0, y1, y2, y3, t) * gain;
            }
            position_ += speed_;
        }
        Release();
        return written;
    }

private:
    static float Hermite(float y0, float y1, float y2, float y3, float t) {
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }

    float At(size_t index, int channel) const { return samples_[((head_ + index) % capacity_) * channels_ + channel]; }

    // Frees what was played, keeping one frame before the position for the interpolator.
    void Release() {
        const size_t used = static_cast<size_t>(position_);
        if (used > 1) {
            Discard(std::min(used - 1, size_));
        }
    }

    void Discard(size_t frames) {
        head_ = (head_ + frames) % capacity_;
        size_ -= frames;
        position_ = std::max(0.0, position_ - static_cast<double>(frames));
    }

    std::vector<float> samples_;
    size_t capacity_;
    int channels_;
    double rate_;
    size_t head_ = 0;
    size_t size_ = 0;
    double position_ = 0.0;  // Read position from head_, in input frames.
    double speed_ = 1.0;
    double smoothed_ = 0.0;
    double integral_ = 0.0;
    bool steered_ = false;
};
