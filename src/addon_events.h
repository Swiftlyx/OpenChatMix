#pragma once

#include <map>
#include <span>
#include <string>

#include "i18n.h"
#include "settings.h"

// An event of GG's Discord helper that an add-on screen can show, as SteelSeries GG lists them.
struct AddOnEventSpec {
    const wchar_t* event;
    Text name;
    bool hasEnd;               // The event lasts, so its screen can repeat until it ends.
    const wchar_t* fields[2];  // Its fields, written {field} in a display's heading and text.
};

std::span<const AddOnEventSpec> DiscordEvents();
const AddOnEventSpec* FindDiscordEvent(const std::wstring& event);
AddOnEventScreen DefaultDiscordScreen(const std::wstring& event);
// The screen the add-on shows for the event: the one the user made, or the default.
AddOnEventScreen ScreenFor(const AddOnSettings& addOn, const std::wstring& event);
// The text with each {field} replaced by its value; unknown fields stay as they are.
std::wstring FillTemplate(const std::wstring& text, const std::map<std::wstring, std::wstring>& fields);
