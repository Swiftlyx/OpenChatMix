#pragma once

#include <windows.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "gamesense_server.h"
#include "oled.h"
#include "settings.h"

// One display of an add-on screen as it goes on the headset display, its fields filled in.
struct AddOnCard {
    int icon = 0;  // One of the add-on icons.
    std::wstring heading;
    std::wstring text;
    std::wstring image;  // Shown instead of the icon, heading and text.
    std::shared_ptr<const OledBitmap> bitmap;  // A whole frame the program sent as pixels; shown instead of the rest.

    bool operator==(const AddOnCard&) const = default;
};

// Turns the add-ons' GameSense requests into screens. Each event starts its screen: its displays in turn, a number
// of times or while the event lasts. Discord's events show the screens of the Discord add-on's settings; other
// programs show the screens they bind for SteelSeries displays, text or bitmaps.
//
// A program may bind an event's screen for the display's idle time instead, with "openchatmix-show": "idle" in the
// handler: its latest display then shows in the screensaver's place, and in the visualizer's while the visualizer is
// off, as long as the program keeps sending it.
class AddOnScreens {
public:
    void Handle(const GameSenseRequest& request, const std::vector<AddOnSettings>& addOns, ULONGLONG now);
    // The display to show now; next is when that changes by itself, 0 for no such time.
    std::optional<AddOnCard> Current(ULONGLONG now, ULONGLONG& next) const;
    // The idle screen while its program keeps it up to date; none when it stopped sending.
    std::optional<AddOnCard> IdleCard(ULONGLONG now) const;
    // Shows a screen once with the given fields, e.g. while it is being set up.
    void Preview(AddOnEventScreen screen, std::map<std::wstring, std::wstring> fields, ULONGLONG now);
    void Clear();
    // Your own Discord name, once an event told it; empty before.
    const std::wstring& SelfName() const { return self_; }

private:
    struct Run {
        std::wstring key;  // "GAME/EVENT"; a new run of the same event replaces the old one.
        AddOnEventScreen screen;
        std::map<std::wstring, std::wstring> fields;
        ULONGLONG start = 0;
        std::vector<std::shared_ptr<const OledBitmap>> bitmaps;  // By display; null for a display of text.
    };

    void Start(std::wstring key, AddOnEventScreen screen, std::map<std::wstring, std::wstring> fields, ULONGLONG now,
               std::vector<std::shared_ptr<const OledBitmap>> bitmaps = {});
    // The event ended: a screen that lasts with it stops; one that runs a number of times finishes.
    void End(const std::wstring& key);
    void HandleEvent(const std::wstring& game, const std::wstring& event, const JsonValue& data,
                     const std::vector<AddOnSettings>& addOns, ULONGLONG now);
    void HandleDiscord(const std::wstring& event, const JsonValue& data, const AddOnSettings* addOn, ULONGLONG now);
    void HandleBound(const std::wstring& key, const JsonValue& handler, const JsonValue& data, ULONGLONG now);

    std::vector<Run> runs_;  // Oldest first.
    std::wstring self_;
    ULONGLONG discordStart_ = 0;  // The helper reports its state when it starts; that is not news.
    std::map<std::wstring, JsonValue> handlers_;  // Screen handlers by "GAME/EVENT".
    std::wstring idleKey_;  // "GAME/EVENT" of the idle screen.
    AddOnCard idle_;
    ULONGLONG idleUpdated_ = 0;
};

// Draws one display on the 128x64 display.
std::shared_ptr<IdleAnimation> CreateAddOnScreen(const AddOnCard& card);

// Plays the add-on's idle screen: the display thread draws the latest card it was given, so new frames show
// without starting the animation again.
class AddOnIdleScreen final : public IdleAnimation {
public:
    // Any thread.
    void Show(const AddOnCard& card);
    DWORD Render(ULONGLONG elapsedMs, OledBitmap& frame) override;

private:
    SRWLOCK lock_ = SRWLOCK_INIT;
    AddOnCard card_;        // Under lock_.
    uint64_t version_ = 0;  // Under lock_; counts cards given.
    // Display thread only.
    uint64_t drawnVersion_ = 0;
    AddOnCard drawn_;
    std::shared_ptr<IdleAnimation> text_;  // The drawn card when it is not a bitmap.
    ULONGLONG cardStart_ = 0;
    ULONGLONG lastElapsed_ = 0;
};
