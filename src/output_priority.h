#pragma once

#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <vector>

// Which output or microphone to switch to after devices came or went, or none to leave it as it is.
// connected: the connected devices, most preferred first. known: the devices connected at the last
// check (empty on the first). last: the device in use at the last check. order: the saved order;
// only a device placed in it counts as preferred when it reconnects.
inline std::optional<std::wstring> ChooseDevice(const std::vector<std::wstring>& connected,
                                                const std::set<std::wstring>& known, const std::wstring& last,
                                                const std::vector<std::wstring>& order, bool switchOnLoss,
                                                bool returnToPreferred) {
    if (connected.empty() || known.empty()) {
        return std::nullopt;
    }
    const std::wstring& top = connected.front();
    const bool lost = !last.empty() && std::find(connected.begin(), connected.end(), last) == connected.end();
    const bool returned = !known.count(top) && std::find(order.begin(), order.end(), top) != order.end();
    if ((lost && switchOnLoss) || (returned && returnToPreferred)) {
        return top;
    }
    return std::nullopt;
}
