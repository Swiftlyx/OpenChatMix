#pragma once

#include <algorithm>
#include <cmath>
#include <cstdlib>

// ChatMix balance as one number in [-100, 100]: negative lowers Chat (favours Game), positive lowers
// Game, by that many percent. A headset dial has kBalanceSteps positions per side, so with such a
// headset the balance keeps to them; without one every percent is a position of its own.
constexpr int kBalanceRange = 100;
constexpr int kBalanceSteps = 12;  // GameDAC dial resolution per side.
// Wheel and hotkey steps without a headset dial: 10%.
constexpr int kCoarseSteps = 10;

struct Levels {
    int game = 100;  // 0-100
    int chat = 100;  // 0-100

    bool operator==(const Levels&) const = default;
};

inline Levels ClampLevels(int game, int chat) {
    return {std::clamp(game, 0, 100), std::clamp(chat, 0, 100)};
}

inline int BalanceFromLevels(Levels levels) {
    return std::clamp(levels.chat - levels.game, -kBalanceRange, kBalanceRange);
}

inline Levels LevelsFromBalance(int balance) {
    balance = std::clamp(balance, -kBalanceRange, kBalanceRange);
    Levels levels;
    if (balance > 0) {
        levels.game = 100 - balance;
    } else if (balance < 0) {
        levels.chat = 100 + balance;
    }
    return levels;
}

// Position index of a grid with `steps` positions per side. Positions round up as the GameDAC does
// (100 - ceil(100 * k / 12) for its dial), so a 12-step grid matches what the headset applies.
inline int BalanceAt(int index, int steps) {
    index = std::clamp(index, -steps, steps);
    const int reduced = (kBalanceRange * std::abs(index) + steps - 1) / steps;
    return index < 0 ? -reduced : reduced;
}

// Index of the grid position nearest to balance.
inline int GridIndex(int balance, int steps) {
    return static_cast<int>(std::lround(balance * steps / static_cast<double>(kBalanceRange)));
}

inline int SnapBalance(int balance, int steps) {
    return BalanceAt(GridIndex(balance, steps), steps);
}

// One position further toward direction (-1 Game, +1 Chat) on a grid.
inline int StepBalance(int balance, int direction, int steps) {
    return BalanceAt(GridIndex(balance, steps) + direction, steps);
}
