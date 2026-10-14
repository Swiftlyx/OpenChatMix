#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Minimal JSON reader for preset files and SteelSeries Sonar data. Strings are returned as UTF-16.
struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::wstring string;
    std::vector<JsonValue> array;
    std::vector<std::pair<std::wstring, JsonValue>> object;  // In document order.

    // Member of an object; null when missing or not an object.
    const JsonValue* Find(std::wstring_view key) const;
    double NumberOr(double fallback) const { return type == Type::Number ? number : fallback; }
    bool BoolOr(bool fallback) const { return type == Type::Bool ? boolean : fallback; }
};

std::optional<JsonValue> ParseJson(std::string_view utf8);
