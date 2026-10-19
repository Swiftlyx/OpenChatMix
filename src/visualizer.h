#pragma once

#include <memory>

#include "audio_tap.h"
#include "oled.h"

// Bars across the whole display that follow the sound the tap hears, like a spectrum analyzer: low notes on the
// left, high on the right, each bar falling slowly with a peak mark above it.
std::shared_ptr<IdleAnimation> CreateVisualizer(std::shared_ptr<const AudioTap> tap);
