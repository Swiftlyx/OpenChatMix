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

    bool operator==(const AddOnCard&) const = default;
};

// Turns the add-ons' GameSense requests into screens. Each event starts its screen: its displays in turn, a number
// of times or while the event lasts. Discord's events show the screens of the Discord add-on's settings; other
// programs show the screens they bind for SteelSeries displays.
class AddOnScreens {
public:
    void Handle(const GameSenseRequest& request, const std::vector<AddOnSettings>& addOns, ULONGLONG now);
    // The display to show now; next is when that changes by itself, 0 for no such time.
    std::optional<AddOnCard> Current(ULONGLONG now, ULONGLONG& next) const;
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
    };

    void Start(std::wstring key, AddOnEventScreen screen, std::map<std::wstring, std::wstring> fields, ULONGLONG now);
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
};

// Draws one display on the 128x64 display.
std::shared_ptr<IdleAnimation> CreateAddOnScreen(const AddOnCard& card);
