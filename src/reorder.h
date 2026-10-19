#pragma once

#include <algorithm>
#include <cstddef>
#include <iterator>

// Moves the item at from to the place to, shifting the items between by one: what dropping a
// dragged row does. Works on any container with random-access iterators.
template <typename Container>
void MoveItem(Container& items, size_t from, size_t to) {
    const auto begin = std::begin(items);
    if (from < to) {
        std::rotate(begin + from, begin + from + 1, begin + to + 1);
    } else if (to < from) {
        std::rotate(begin + to, begin + from, begin + from + 1);
    }
}
