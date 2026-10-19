// Simulates a capture device and a render device on different clocks feeding through DriftQueue,
// to check that the output neither runs dry nor breaks the signal. Not part of the app.
//
//   g++ -O2 -std=c++20 -Isrc tools/drift_sim.cpp -o drift_sim
//   ./drift_sim [clock difference in ppm] [max lateness of 5% of packets in seconds]
//
// Prints dropouts after the first 30 s, the latency range, the final speed and the largest second
// difference of the output (a 997 Hz sine alone gives 0.0083; a click is far above it).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

#include "drift_queue.h"

int main(int argc, char** argv) {
    const double ppm = argc > 1 ? std::atof(argv[1]) : 300.0;
    const double lateMax = argc > 2 ? std::atof(argv[2]) : 0.0;
    const double rate = 48000.0;
    const double inputRate = rate * (1.0 + ppm * 1e-6);
    const int packet = 480;  // Capture packets of 10 ms.
    const int period = 480;  // Render period of 10 ms.
    const double renderTarget = period + packet + rate * 0.006;  // As in audio_bridge.cpp.
    const double latencyTarget = renderTarget + packet / 2.0;
    const double pi = std::acos(-1.0);

    DriftQueue queue(1, 48000, rate);
    std::mt19937 random(1);
    std::uniform_real_distribution<double> chance(0.0, 1.0);
    std::uniform_real_distribution<double> lateness(0.0, lateMax);
    double nextCapture = 0.01;
    double late = 0.0;
    double nextRender = 0.0;
    double padding = 0.0;
    double phase = 0.0;
    int dropouts = 0;
    double latencyMin = 1e9;
    double latencyMax = 0.0;
    std::vector<float> output;
    for (long step = 0; step < 600L * 10000; ++step) {  // Ten minutes in 0.1 ms steps.
        const double t = step * 1e-4;
        padding = std::max(0.0, padding - rate * 1e-4);
        if (t >= nextCapture + late) {
            late = chance(random) < 0.05 ? lateness(random) : 0.0;
            float samples[packet];
            for (float& sample : samples) {
                sample = 0.5f * static_cast<float>(std::sin(phase));
                phase += 2.0 * pi * 997.0 / inputRate;
            }
            queue.Push(samples, packet, 1);
            nextCapture += packet / inputRate;
        }
        if (t >= nextRender) {
            nextRender += period / rate;
            if (padding < 1.0 && t > 30.0) {
                ++dropouts;
            }
            const double latency = queue.Size() + padding;
            queue.Steer(latency, latencyTarget, period / rate);
            if (t > 30.0) {
                latencyMin = std::min(latencyMin, latency);
                latencyMax = std::max(latencyMax, latency);
            }
            const size_t wanted = padding < renderTarget ? static_cast<size_t>(renderTarget - padding) : 0;
            std::vector<float> block(wanted);
            const size_t got = queue.Pop(block.data(), wanted, 1.0f, 1.0f);
            output.insert(output.end(), block.begin(), block.begin() + got);
            padding += got;
        }
    }
    double worst = 0.0;
    for (size_t n = 48000 * 30; n + 2 < output.size(); ++n) {
        worst = std::max(worst, static_cast<double>(std::fabs(output[n] - 2.0f * output[n + 1] + output[n + 2])));
    }
    std::printf("%+.0f ppm, late up to %.0f ms: %d dropouts, latency %.1f..%.1f ms, speed %.5f, worst 2nd diff %.4f\n",
                ppm, lateMax * 1000.0, dropouts, latencyMin / 48.0, latencyMax / 48.0, queue.Speed(), worst);
    return dropouts == 0 ? 0 : 1;
}
