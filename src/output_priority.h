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

// The devices a channel may play on, most preferred first: the connected devices its order lists, in that order, then
// the other connected ones as connected has them. Excluded devices are left out.
inline std::vector<std::wstring> ChannelDevices(const std::vector<std::wstring>& connected,
                                                const std::vector<std::wstring>& order,
                                                const std::vector<std::wstring>& excluded) {
    std::vector<std::wstring> devices;
    for (const std::wstring& id : connected) {
        if (std::find(excluded.begin(), excluded.end(), id) == excluded.end()) {
            devices.push_back(id);
        }
    }
    auto rank = [&order](const std::wstring& id) { return std::find(order.begin(), order.end(), id) - order.begin(); };
    std::stable_sort(devices.begin(), devices.end(),
                     [&rank](const std::wstring& a, const std::wstring& b) { return rank(a) < rank(b); });
    return devices;
}

// Moves the listed device at from to the place to. The listed devices join the order where they are missing, and
// keep the places they hold in it, filled in their new order; devices the list leaves out stay where they were.
inline void MoveInOrder(std::vector<std::wstring>& order, std::vector<std::wstring> listed, size_t from, size_t to) {
    if (from >= listed.size() || to >= listed.size() || from == to) {
        return;
    }
    for (const std::wstring& id : listed) {
        if (std::find(order.begin(), order.end(), id) == order.end()) {
            order.push_back(id);
        }
    }
    const std::wstring moved = listed[from];
    listed.erase(listed.begin() + static_cast<std::ptrdiff_t>(from));
    listed.insert(listed.begin() + static_cast<std::ptrdiff_t>(to), moved);
    size_t next = 0;
    for (std::wstring& id : order) {
        if (std::find(listed.begin(), listed.end(), id) != listed.end()) {
            id = listed[next++];
        }
    }
}
