#pragma once

#include <memory>
#include <string>

#include "oled.h"
#include "settings.h"

// Built-in idle screens for the 128x64 OLED and image / GIF playback. Every built-in keeps its lit
// pixels moving so no part of the panel stays on for long.
std::shared_ptr<IdleAnimation> CreateIdleAnimation(IdleAnimationKind kind, const std::wstring& imagePath);
