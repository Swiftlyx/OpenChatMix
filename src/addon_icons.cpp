#include "addon_icons.h"

#include <iterator>

namespace {

using Rows = const char* const[kAddOnIconSize];

constexpr Rows kMessage = {"#########", "#.......#", "#.#####.#", "#.......#", "#.####..#",
                           "#.......#", "#########", ".##......", "##......."};
constexpr Rows kMention = {"..#####..", ".#.....#.", "#..###..#", "#.#...#.#", "#.#...#.#",
                           "#..###.##", "#........", ".#.....#.", "..#####.."};
constexpr Rows kMic = {"...###...", "..#####..", "..#####..", "..#####..", "..#####..",
                       "#.#####.#", "#..###..#", ".##...##.", "....#...."};
constexpr Rows kHeadset = {"..#####..", ".##...##.", "##.....##", "#.......#", "#.......#",
                           "###...###", "###...###", "###...###", ".#.....#."};
constexpr Rows kUser = {"...###...", "..#####..", "..#####..", "..#####..", "...###...",
                        ".........", ".#######.", "#########", "#########"};
constexpr Rows kJoin = {"....#####", "........#", "..#.....#", "...#....#", "#####...#",
                        "...#....#", "..#.....#", "........#", "....#####"};
constexpr Rows kLeave = {"#####....", "#........", "#.....#..", "#......#.", "#..######",
                         "#......#.", "#.....#..", "#........", "#####...."};
constexpr Rows kBell = {"....#....", "...###...", "..#####..", "..#####..", "..#####..",
                        ".#######.", "#########", ".........", "....#...."};
constexpr Rows kHeart = {".##...##.", "####.####", "#########", "#########", ".#######.",
                         "..#####..", "...###...", "....#....", "........."};
constexpr Rows kStar = {"....#....", "....#....", "...###...", "#########", ".#######.",
                        "..#####..", "..##.##..", ".##...##.", "#.......#"};
constexpr Rows kCheck = {"........#", ".......##", "......##.", "#....##..", "##..##...",
                         ".####....", "..##.....", ".........", "........."};
constexpr Rows kCross = {"##.....##", "###...###", ".###.###.", "..#####..", "...###...",
                         "..#####..", ".###.###.", "###...###", "##.....##"};
constexpr Rows kMusic = {"...######", "...######", "...#....#", "...#....#", "...#....#",
                         ".###..###", "####.####", "####.####", ".##...##."};
constexpr Rows kGamepad = {".........", ".#######.", "##.######", "#...##.##", "##.###.#.",
                           "#########", "###...###", "##.....##", "........."};
constexpr Rows kSpeaker = {"....#....", "...##..#.", "..###...#", "#####.#.#", "#####.#.#",
                           "#####.#.#", "..###...#", "...##..#.", "....#...."};
constexpr Rows kAlert = {"...###...", "...###...", "...###...", "...###...", "...###...",
                         "....#....", ".........", "...###...", "...###..."};

// Bar heights of the talking icon, one row per step of its animation.
constexpr int kTalkingBars[][3] = {{3, 7, 5}, {6, 4, 8}, {8, 6, 3}, {4, 9, 6}, {7, 3, 7}};
constexpr ULONGLONG kTalkingStepMs = 140;

const char* const* RowsOf(int icon) {
    switch (icon) {
    case kIconMessage: return kMessage;
    case kIconMention: return kMention;
    case kIconMic:
    case kIconMicOff: return kMic;
    case kIconHeadset: return kHeadset;
    case kIconUser: return kUser;
    case kIconJoin: return kJoin;
    case kIconLeave: return kLeave;
    case kIconBell: return kBell;
    case kIconHeart: return kHeart;
    case kIconStar: return kStar;
    case kIconCheck: return kCheck;
    case kIconCross: return kCross;
    case kIconMusic: return kMusic;
    case kIconGamepad: return kGamepad;
    case kIconSpeaker: return kSpeaker;
    case kIconAlert: return kAlert;
    default: return nullptr;
    }
}

}  // namespace

bool AddOnIconPixel(int icon, int x, int y, ULONGLONG elapsedMs) {
    if (x < 0 || y < 0 || x >= kAddOnIconSize || y >= kAddOnIconSize) {
        return false;
    }
    if (icon == kIconTalking) {
        // Bars two pixels wide with a pixel between them, rising from the bottom.
        const int bar = x / 3;
        if (x % 3 == 2 || bar > 2) {
            return false;
        }
        const int* heights = kTalkingBars[(elapsedMs / kTalkingStepMs) % std::size(kTalkingBars)];
        return kAddOnIconSize - 1 - y < heights[bar];
    }
    if (icon == kIconMicOff) {
        // The microphone with a slash across, cut free from it by a dark edge.
        if (x == y) {
            return true;
        }
        if (x == y + 1) {
            return false;
        }
    }
    const char* const* rows = RowsOf(icon);
    return rows && rows[y][x] == '#';
}
