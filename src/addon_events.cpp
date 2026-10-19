#include "addon_events.h"

#include <algorithm>
#include <iterator>

#include "addon_icons.h"

namespace {

constexpr AddOnEventSpec kDiscordEvents[] = {
    {L"NOTIFICATION", Text::EventMessage, false, {L"title", L"message"}},
    {L"USER_TALKING_ANY", Text::EventVoiceActivity, true, {L"user", L"channel"}},
    {L"USER_TALKING_SELF", Text::EventYourVoice, true, {L"user", L"channel"}},
    {L"USER_MUTED_SELF", Text::EventMuted, true, {L"user", L"channel"}},
    {L"USER_JOINED_VOICE", Text::EventUserJoined, false, {L"user", L"channel"}},
    {L"USER_LEFT_VOICE", Text::EventUserLeft, false, {L"user", L"channel"}},
    {L"SELF_JOINED_VOICE", Text::EventYouJoined, false, {L"user", L"channel"}},
    {L"SELF_LEFT_VOICE", Text::EventYouLeft, false, {L"user", L"channel"}},
};

AddOnEventScreen Screen(bool enabled, bool whileOn, int icon, std::wstring heading, std::wstring text, int lengthMs) {
    AddOnEventScreen screen;
    screen.enabled = enabled;
    screen.whileOn = whileOn;
    screen.displays.push_back({icon, std::move(heading), std::move(text), {}, lengthMs});
    return screen;
}

}  // namespace

std::span<const AddOnEventSpec> DiscordEvents() {
    return kDiscordEvents;
}

const AddOnEventSpec* FindDiscordEvent(const std::wstring& event) {
    const auto found = std::find_if(std::begin(kDiscordEvents), std::end(kDiscordEvents),
                                    [&](const AddOnEventSpec& spec) { return event == spec.event; });
    return found == std::end(kDiscordEvents) ? nullptr : &*found;
}

AddOnEventScreen DefaultDiscordScreen(const std::wstring& event) {
    // As SteelSeries GG shows them by default; joining and leaving stay off there too.
    if (event == L"NOTIFICATION") {
        return Screen(true, false, kIconMessage, L"{title}", L"{message}", 5000);
    }
    if (event == L"USER_TALKING_ANY") {
        return Screen(true, true, kIconTalking, L"{user}", L"{channel}", 3000);
    }
    if (event == L"USER_TALKING_SELF") {
        return Screen(false, true, kIconTalking, L"{user}", L"{channel}", 3000);
    }
    if (event == L"USER_MUTED_SELF") {
        return Screen(true, false, kIconMicOff, T(Text::CardMuted), L"{channel}", 3000);
    }
    if (event == L"USER_JOINED_VOICE") {
        return Screen(false, false, kIconJoin, L"{user}", T(Text::CardJoins), 3000);
    }
    if (event == L"USER_LEFT_VOICE") {
        return Screen(false, false, kIconLeave, L"{user}", T(Text::CardLeaves), 3000);
    }
    if (event == L"SELF_JOINED_VOICE") {
        return Screen(false, false, kIconJoin, L"{channel}", T(Text::CardSelfJoined), 3000);
    }
    if (event == L"SELF_LEFT_VOICE") {
        return Screen(false, false, kIconLeave, L"{channel}", T(Text::CardSelfLeft), 3000);
    }
    return Screen(false, false, kIconNone, {}, {}, 3000);
}

AddOnEventScreen ScreenFor(const AddOnSettings& addOn, const std::wstring& event) {
    const auto found = addOn.screens.find(event);
    return found == addOn.screens.end() ? DefaultDiscordScreen(event) : found->second;
}

std::wstring FillTemplate(const std::wstring& text, const std::map<std::wstring, std::wstring>& fields) {
    std::wstring filled;
    for (size_t at = 0; at < text.size();) {
        const size_t open = text.find(L'{', at);
        const size_t close = open == std::wstring::npos ? std::wstring::npos : text.find(L'}', open + 1);
        if (close == std::wstring::npos) {
            filled += text.substr(at);
            break;
        }
        filled += text.substr(at, open - at);
        const auto field = fields.find(text.substr(open + 1, close - open - 1));
        filled += field == fields.end() ? text.substr(open, close - open + 1) : field->second;
        at = close + 1;
    }
    return filled;
}
