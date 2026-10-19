#pragma once

#include <cmath>

#include "design.h"

namespace ui {

// Advances a damped spring of unit mass by dt with the exact solution, so any frame time is stable.
inline void StepSpring(float& value, float& velocity, float target, design::Spring spring, float dt) {
    const float omega = std::sqrt(spring.stiffness);
    const float zeta = spring.damping;
    const float x0 = value - target;
    const float v0 = velocity;
    if (zeta < 1.0f) {
        const float omegaD = omega * std::sqrt(1.0f - zeta * zeta);
        const float decay = std::exp(-zeta * omega * dt);
        const float c = std::cos(omegaD * dt);
        const float s = std::sin(omegaD * dt);
        value = target + decay * (x0 * c + (v0 + zeta * omega * x0) / omegaD * s);
        velocity = decay * (v0 * c - (zeta * omega * v0 + omega * omega * x0) / omegaD * s);
    } else {
        const float decay = std::exp(-omega * dt);
        const float b = v0 + omega * x0;
        value = target + (x0 + b * dt) * decay;
        velocity = (v0 - omega * b * dt) * decay;
    }
}

}  // namespace ui
