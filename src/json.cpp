#include "json.h"

#include <windows.h>

#include <cstdlib>
#include <cstring>
#include <string>

namespace {

constexpr int kMaxDepth = 64;

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    bool Value(JsonValue& out, int depth) {
        if (depth > kMaxDepth) {
            return false;
        }
        SkipSpace();
        if (pos_ >= text_.size()) {
            return false;
        }
        const char c = text_[pos_];
        if (c == '{') {
            return Object(out, depth);
        }
        if (c == '[') {
            return Array(out, depth);
        }
        if (c == '"') {
            out.type = JsonValue::Type::String;
            return String(out.string);
        }
        if (Literal("true")) {
            out.type = JsonValue::Type::Bool;
            out.boolean = true;
            return true;
        }
        if (Literal("false")) {
            out.type = JsonValue::Type::Bool;
            return true;
        }
        if (Literal("null")) {
            out.type = JsonValue::Type::Null;
            return true;
        }
        return Number(out);
    }

    bool AtEnd() {
        SkipSpace();
        return pos_ == text_.size();
    }

private:
    void SkipSpace() {
        while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\r' ||
                                       text_[pos_] == '\n')) {
            ++pos_;
        }
    }

    bool Literal(std::string_view word) {
        if (text_.substr(pos_, word.size()) == word) {
            pos_ += word.size();
            return true;
        }
        return false;
    }

    bool Number(JsonValue& out) {
        const size_t start = pos_;
        while (pos_ < text_.size() && (std::strchr("+-0123456789.eE", text_[pos_]) && text_[pos_] != '\0')) {
            ++pos_;
        }
        if (pos_ == start) {
            return false;
        }
        const std::string digits(text_.substr(start, pos_ - start));
        char* end = nullptr;
        out.number = std::strtod(digits.c_str(), &end);
        out.type = JsonValue::Type::Number;
        return end && *end == '\0';
    }

    static void AppendUtf8(std::string& out, unsigned code) {
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (code >> 18));
            out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
    }

    bool Hex4(unsigned& code) {
        if (pos_ + 4 > text_.size()) {
            return false;
        }
        code = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[pos_++];
            code <<= 4;
            if (c >= '0' && c <= '9') {
                code |= static_cast<unsigned>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                code |= static_cast<unsigned>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                code |= static_cast<unsigned>(c - 'A' + 10);
            } else {
                return false;
            }
        }
        return true;
    }

    bool String(std::wstring& out) {
        ++pos_;  // Opening quote.
        std::string utf8;
        while (pos_ < text_.size()) {
            const char c = text_[pos_++];
            if (c == '"') {
                out.clear();
                if (!utf8.empty()) {
                    const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                                                           nullptr, 0);
                    out.resize(length);
                    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), length);
                }
                return true;
            }
            if (c != '\\') {
                utf8 += c;
                continue;
            }
            if (pos_ >= text_.size()) {
                return false;
            }
            const char escape = text_[pos_++];
            switch (escape) {
            case '"': utf8 += '"'; break;
            case '\\': utf8 += '\\'; break;
            case '/': utf8 += '/'; break;
            case 'b': utf8 += '\b'; break;
            case 'f': utf8 += '\f'; break;
            case 'n': utf8 += '\n'; break;
            case 'r': utf8 += '\r'; break;
            case 't': utf8 += '\t'; break;
            case 'u': {
                unsigned code = 0;
                if (!Hex4(code)) {
                    return false;
                }
                // Surrogate pairs arrive as two escapes.
                if (code >= 0xD800 && code < 0xDC00 && text_.substr(pos_, 2) == "\\u") {
                    pos_ += 2;
                    unsigned low = 0;
                    if (!Hex4(low)) {
                        return false;
                    }
                    code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                }
                AppendUtf8(utf8, code);
                break;
            }
            default: return false;
            }
        }
        return false;
    }

    bool Array(JsonValue& out, int depth) {
        ++pos_;
        out.type = JsonValue::Type::Array;
        SkipSpace();
        if (pos_ < text_.size() && text_[pos_] == ']') {
            ++pos_;
            return true;
        }
        for (;;) {
            JsonValue item;
            if (!Value(item, depth + 1)) {
                return false;
            }
            out.array.push_back(std::move(item));
            SkipSpace();
            if (pos_ >= text_.size()) {
                return false;
            }
            if (text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (text_[pos_] == ']') {
                ++pos_;
                return true;
            }
            return false;
        }
    }

    bool Object(JsonValue& out, int depth) {
        ++pos_;
        out.type = JsonValue::Type::Object;
        SkipSpace();
        if (pos_ < text_.size() && text_[pos_] == '}') {
            ++pos_;
            return true;
        }
        for (;;) {
            SkipSpace();
            if (pos_ >= text_.size() || text_[pos_] != '"') {
                return false;
            }
            std::wstring key;
            if (!String(key)) {
                return false;
            }
            SkipSpace();
            if (pos_ >= text_.size() || text_[pos_] != ':') {
                return false;
            }
            ++pos_;
            JsonValue value;
            if (!Value(value, depth + 1)) {
                return false;
            }
            out.object.emplace_back(std::move(key), std::move(value));
            SkipSpace();
            if (pos_ >= text_.size()) {
                return false;
            }
            if (text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (text_[pos_] == '}') {
                ++pos_;
                return true;
            }
            return false;
        }
    }

    std::string_view text_;
    size_t pos_ = 0;
};

}  // namespace

const JsonValue* JsonValue::Find(std::wstring_view key) const {
    if (type != Type::Object) {
        return nullptr;
    }
    for (const auto& [name, value] : object) {
        if (name == key) {
            return &value;
        }
    }
    return nullptr;
}

std::optional<JsonValue> ParseJson(std::string_view utf8) {
    // A UTF-8 byte order mark is allowed in front of the document.
    if (utf8.substr(0, 3) == "\xEF\xBB\xBF") {
        utf8.remove_prefix(3);
    }
    Parser parser(utf8);
    JsonValue value;
    if (!parser.Value(value, 0) || !parser.AtEnd()) {
        return std::nullopt;
    }
    return value;
}
