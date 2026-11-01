#include "addon_screens.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwchar>

#include "addon_events.h"
#include "addon_icons.h"
#include "idle_animations.h"

namespace {

constexpr wchar_t kDiscordGame[] = L"DISCORD";
constexpr ULONGLONG kStartGraceMs = 4000;   // The state the Discord helper reports when it starts.
constexpr int kMinDisplayMs = 500;
constexpr int kAppDefaultMs = 4000;         // A program's display without a length of its own.
constexpr int kAppLongestMs = 10000;        // Displays meant to stay are refreshed by the program meanwhile.
constexpr ULONGLONG kPreviewLastingMs = 6000;  // A preview of a screen that lasts with its event.
constexpr ULONGLONG kScrollDelayMs = 1500;  // A long text waits this long before it scrolls.
constexpr int kScrollPixelsPerSecond = 14;
constexpr ULONGLONG kTalkingFrameMs = 140;
constexpr wchar_t kFace[] = L"Tahoma";
constexpr int kFontPixels = 11;
constexpr const wchar_t* kFrameFields[] = {L"title", L"message", L"user", L"channel"};
constexpr wchar_t kOwnScreen[] = L"screened-128x64";
// Sizes of the GameSense bitmaps, after the display's own; smaller ones show centered.
constexpr const wchar_t* kBitmapKeys[] = {L"image-data-128x64", L"image-data-128x52", L"image-data-128x48",
                                          L"image-data-128x40", L"image-data-128x36"};
constexpr ULONGLONG kIdleStaleMs = 10000;  // An idle screen not sent again for this long is gone.
constexpr DWORD kIdlePollMs = 100;         // How soon a new frame of the idle screen shows.

std::wstring JsonText(const JsonValue* field) {
    if (!field) {
        return {};
    }
    if (field->type == JsonValue::Type::String) {
        return field->string;
    }
    if (field->type == JsonValue::Type::Number) {
        const double number = field->number;
        return number == std::floor(number) ? std::to_wstring(static_cast<long long>(number)) : std::to_wstring(number);
    }
    return {};
}

std::wstring FrameText(const JsonValue& data, const std::wstring& key) {
    const JsonValue* frame = data.Find(L"frame");
    return JsonText(frame ? frame->Find(key) : nullptr);
}

double ValueOf(const JsonValue& data) {
    const JsonValue* value = data.Find(L"value");
    if (!value) {
        return 0.0;
    }
    if (value->type == JsonValue::Type::Bool) {
        return value->boolean ? 1.0 : 0.0;
    }
    return value->NumberOr(0.0);
}

std::wstring StringOf(const JsonValue& object, const wchar_t* key) {
    const JsonValue* field = object.Find(key);
    return field && field->type == JsonValue::Type::String ? field->string : std::wstring{};
}

const AddOnSettings* DiscordAddOn(const std::vector<AddOnSettings>& addOns) {
    const auto found = std::find_if(addOns.begin(), addOns.end(), [](const AddOnSettings& addOn) {
        return addOn.enabled && AddOnKindOf(addOn.path) == AddOnKind::Discord;
    });
    return found == addOns.end() ? nullptr : &*found;
}

// The size of a GameSense bitmap from its key or device type, e.g. "image-data-128x40" or "screened-128x40".
bool BitmapSize(const std::wstring& name, int& width, int& height) {
    const size_t dash = name.find_last_of(L'-');
    if (dash == std::wstring::npos || swscanf_s(name.c_str() + dash + 1, L"%dx%d", &width, &height) != 2) {
        return false;
    }
    return width > 0 && width <= kOledWidth && height > 0 && height <= kOledHeight;
}

// Pixels packed one bit each, row by row from the top left, the most significant bit first; a smaller bitmap
// shows centered. Null when there are too few bytes.
std::shared_ptr<const OledBitmap> UnpackBitmap(const JsonValue& bytes, int width, int height) {
    const size_t needed = (static_cast<size_t>(width) * height + 7) / 8;
    if (bytes.type != JsonValue::Type::Array || bytes.array.size() < needed) {
        return nullptr;
    }
    auto bitmap = std::make_shared<OledBitmap>();
    const int left = (kOledWidth - width) / 2;
    const int top = (kOledHeight - height) / 2;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t bit = static_cast<size_t>(y) * width + x;
            const auto byte = static_cast<unsigned>(bytes.array[bit / 8].NumberOr(0.0));
            if (byte & (0x80u >> (bit % 8))) {
                bitmap->Set(left + x, top + y);
            }
        }
    }
    return bitmap;
}

// A bitmap frame of a handler: the event's own image of the handler's size or another size when it sent one,
// else the handler's fixed image.
std::shared_ptr<const OledBitmap> FrameBitmap(const JsonValue& frame, const JsonValue& data, const std::wstring& type) {
    int width = kOledWidth;
    int height = kOledHeight;
    const bool sized = BitmapSize(type, width, height);
    if (const JsonValue* sent = data.Find(L"frame")) {
        std::vector<std::wstring> keys;
        if (sized) {
            keys.push_back(L"image-data-" + std::to_wstring(width) + L"x" + std::to_wstring(height));
        }
        keys.insert(keys.end(), std::begin(kBitmapKeys), std::end(kBitmapKeys));
        for (const std::wstring& key : keys) {
            int w = 0;
            int h = 0;
            if (const JsonValue* bytes = sent->Find(key); bytes && BitmapSize(key, w, h)) {
                if (std::shared_ptr<const OledBitmap> bitmap = UnpackBitmap(*bytes, w, h)) {
                    return bitmap;
                }
            }
        }
    }
    const JsonValue* fixed = frame.Find(L"image-data");
    return fixed ? UnpackBitmap(*fixed, width, height) : nullptr;
}

ULONGLONG DisplayLength(const AddOnDisplay& display) {
    return static_cast<ULONGLONG>(std::max(display.lengthMs, kMinDisplayMs));
}

ULONGLONG CycleLength(const AddOnEventScreen& screen) {
    ULONGLONG cycle = 0;
    for (const AddOnDisplay& display : screen.displays) {
        cycle += DisplayLength(display);
    }
    return cycle;
}

// A line of text in 1-bit pixels, as tall as the font's line.
struct TextBits {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;

    void DrawTo(OledBitmap& frame, int left, int top, int clipTop, int clipBottom) const {
        for (int y = 0; y < height; ++y) {
            const int row = top + y;
            if (row < clipTop || row >= clipBottom) {
                continue;
            }
            for (int x = 0; x < width; ++x) {
                if (pixels[y * width + x]) {
                    frame.Set(left + x, row);
                }
            }
        }
    }
};

// GDI text without antialiasing; GDI's font linking covers scripts and symbols the face lacks.
class PixelFont {
public:
    PixelFont(int pixelHeight, int weight) {
        dc_ = CreateCompatibleDC(nullptr);
        font_ = CreateFontW(-pixelHeight, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY, DEFAULT_PITCH, kFace);
        if (dc_ && font_) {
            previous_ = SelectObject(dc_, font_);
            TEXTMETRICW metrics{};
            GetTextMetricsW(dc_, &metrics);
            height_ = metrics.tmHeight;
        }
    }
    ~PixelFont() {
        if (dc_) {
            if (previous_) {
                SelectObject(dc_, previous_);
            }
            DeleteDC(dc_);
        }
        if (font_) {
            DeleteObject(font_);
        }
    }
    PixelFont(const PixelFont&) = delete;
    PixelFont& operator=(const PixelFont&) = delete;

    int Height() const { return height_; }

    int Width(const std::wstring& text) const {
        SIZE size{};
        if (!dc_ || text.empty() || !GetTextExtentPoint32W(dc_, text.c_str(), static_cast<int>(text.size()), &size)) {
            return 0;
        }
        return size.cx;
    }

    TextBits Render(const std::wstring& text) const {
        TextBits bits;
        const int width = Width(text);
        if (width <= 0 || height_ <= 0) {
            return bits;
        }
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height_;  // Top-down rows.
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* data = nullptr;
        HBITMAP bitmap = CreateDIBSection(dc_, &info, DIB_RGB_COLORS, &data, nullptr, 0);
        if (!bitmap) {
            return bits;
        }
        HGDIOBJ previous = SelectObject(dc_, bitmap);
        std::memset(data, 0, static_cast<size_t>(width) * height_ * 4);
        SetBkMode(dc_, TRANSPARENT);
        SetTextColor(dc_, RGB(255, 255, 255));
        TextOutW(dc_, 0, 0, text.c_str(), static_cast<int>(text.size()));
        GdiFlush();
        const auto* source = static_cast<const uint32_t*>(data);
        bits.width = width;
        bits.height = height_;
        bits.pixels.resize(static_cast<size_t>(width) * height_);
        for (size_t i = 0; i < bits.pixels.size(); ++i) {
            bits.pixels[i] = (source[i] & 0xFF) > 127 ? 1 : 0;
        }
        SelectObject(dc_, previous);
        DeleteObject(bitmap);
        return bits;
    }

    // The text cut to fit width, with an ellipsis when it had to be cut.
    std::wstring Fit(const std::wstring& text, int width) const {
        if (Width(text) <= width) {
            return text;
        }
        size_t low = 0;
        size_t high = text.size();
        while (low < high) {
            const size_t middle = (low + high + 1) / 2;
            if (Width(Cut(text, middle) + L"…") <= width) {
                low = middle;
            } else {
                high = middle - 1;
            }
        }
        return Cut(text, low) + L"…";
    }

    // Lines of at most width pixels, broken between words where possible; line breaks in the text stay.
    std::vector<std::wstring> Wrap(const std::wstring& text, int width) const {
        std::vector<std::wstring> lines;
        size_t start = 0;
        while (start <= text.size()) {
            size_t end = text.find(L'\n', start);
            if (end == std::wstring::npos) {
                end = text.size();
            }
            WrapParagraph(text.substr(start, end - start), width, lines);
            start = end + 1;
        }
        return lines;
    }

private:
    // A prefix of count characters that does not split a surrogate pair.
    static std::wstring Cut(const std::wstring& text, size_t count) {
        if (count > 0 && count < text.size() && IS_HIGH_SURROGATE(text[count - 1])) {
            --count;
        }
        return text.substr(0, count);
    }

    void WrapParagraph(const std::wstring& paragraph, int width, std::vector<std::wstring>& lines) const {
        std::wstring line;
        size_t at = 0;
        while (at < paragraph.size()) {
            size_t next = paragraph.find(L' ', at);
            if (next == std::wstring::npos) {
                next = paragraph.size();
            }
            std::wstring word = paragraph.substr(at, next - at);
            at = next + 1;
            const std::wstring candidate = line.empty() ? word : line + L" " + word;
            if (Width(candidate) <= width) {
                line = candidate;
                continue;
            }
            if (!line.empty()) {
                lines.push_back(line);
                line.clear();
            }
            // A word longer than the line is broken where the line ends.
            while (Width(word) > width) {
                size_t fits = 1;
                while (fits < word.size() && Width(Cut(word, fits + 1)) <= width) {
                    ++fits;
                }
                const std::wstring head = Cut(word, fits);
                if (head.empty()) {
                    break;
                }
                lines.push_back(head);
                word.erase(0, head.size());
            }
            line = word;
        }
        if (!line.empty() || paragraph.empty()) {
            lines.push_back(line);
        }
    }

    HDC dc_ = nullptr;
    HFONT font_ = nullptr;
    HGDIOBJ previous_ = nullptr;
    int height_ = 0;
};

class AddOnScreen final : public IdleAnimation {
public:
    explicit AddOnScreen(AddOnCard card) : card_(std::move(card)) {}

    bool Load(const std::atomic<bool>& cancel) override {
        if (card_.bitmap) {
            return true;
        }
        if (!card_.image.empty()) {
            image_ = CreateIdleAnimation(IdleAnimationKind::Image, card_.image);
            return image_ && image_->Load(cancel);
        }
        const PixelFont bold(kFontPixels, FW_BOLD);
        const PixelFont regular(kFontPixels, FW_NORMAL);
        const int iconWidth = card_.icon ? kAddOnIconSize + 3 : 0;
        lineHeight_ = std::max(1, regular.Height());
        if (!card_.heading.empty()) {
            // The heading beside the icon, the text below them across the whole width.
            headingLeft_ = 1 + iconWidth;
            heading_ = bold.Render(bold.Fit(card_.heading, kOledWidth - headingLeft_ - 1));
            iconTop_ = std::max(0, (bold.Height() - kAddOnIconSize) / 2);
            textTop_ = std::max(bold.Height(), kAddOnIconSize + 1) + 1;
            textLeft_ = 1;
        } else {
            // Without a heading the text sits beside the icon, centered on the display while it fits.
            textLeft_ = 1 + iconWidth;
        }
        for (const std::wstring& line : regular.Wrap(card_.text, kOledWidth - textLeft_ - 1)) {
            text_.push_back(regular.Render(line));
        }
        if (card_.heading.empty()) {
            const int height = static_cast<int>(text_.size()) * lineHeight_;
            textTop_ = std::max(0, (kOledHeight - height) / 2);
            iconTop_ = std::clamp(textTop_ + (std::min(height, lineHeight_) - kAddOnIconSize) / 2, 0,
                                  kOledHeight - kAddOnIconSize);
        }
        return true;
    }

    DWORD Render(ULONGLONG elapsedMs, OledBitmap& frame) override {
        if (card_.bitmap) {
            frame = *card_.bitmap;
            return 500;
        }
        if (image_) {
            return image_->Render(elapsedMs, frame);
        }
        for (int y = 0; y < kAddOnIconSize; ++y) {
            for (int x = 0; x < kAddOnIconSize; ++x) {
                if (AddOnIconPixel(card_.icon, x, y, elapsedMs)) {
                    frame.Set(1 + x, iconTop_ + y);
                }
            }
        }
        heading_.DrawTo(frame, headingLeft_, 0, 0, kOledHeight);
        // A text taller than the display moves up after a pause until its end shows, then stays.
        const int overflow = static_cast<int>(text_.size()) * lineHeight_ - (kOledHeight - textTop_);
        int offset = 0;
        if (overflow > 0 && elapsedMs > kScrollDelayMs) {
            offset = std::min(overflow, static_cast<int>((elapsedMs - kScrollDelayMs) * kScrollPixelsPerSecond / 1000));
        }
        for (size_t i = 0; i < text_.size(); ++i) {
            text_[i].DrawTo(frame, textLeft_, textTop_ + static_cast<int>(i) * lineHeight_ - offset, textTop_,
                            kOledHeight);
        }
        if (card_.icon == kIconTalking) {
            return static_cast<DWORD>(kTalkingFrameMs);
        }
        return overflow > 0 && offset < overflow ? 40 : 500;
    }

private:
    AddOnCard card_;
    std::shared_ptr<IdleAnimation> image_;
    TextBits heading_;
    std::vector<TextBits> text_;
    int headingLeft_ = 1;
    int textLeft_ = 1;
    int textTop_ = 0;
    int iconTop_ = 0;
    int lineHeight_ = 13;
};

}  // namespace

void AddOnScreens::Handle(const GameSenseRequest& request, const std::vector<AddOnSettings>& addOns, ULONGLONG now) {
    const JsonValue& body = request.body;
    const std::wstring game = StringOf(body, L"game");
    if (game.empty()) {
        return;
    }
    // Screens that are over make room.
    std::erase_if(runs_, [&](const Run& run) {
        return !run.screen.whileOn && now - run.start >= CycleLength(run.screen) * run.screen.times;
    });
    if (request.endpoint == L"game_event") {
        const JsonValue* data = body.Find(L"data");
        HandleEvent(game, StringOf(body, L"event"), data ? *data : JsonValue{}, addOns, now);
    } else if (request.endpoint == L"multiple_game_events") {
        if (const JsonValue* events = body.Find(L"events"); events && events->type == JsonValue::Type::Array) {
            for (const JsonValue& event : events->array) {
                const JsonValue* data = event.Find(L"data");
                HandleEvent(game, StringOf(event, L"event"), data ? *data : JsonValue{}, addOns, now);
            }
        }
    } else if (request.endpoint == L"bind_game_event") {
        const std::wstring key = game + L"/" + StringOf(body, L"event");
        if (const JsonValue* handlers = body.Find(L"handlers"); handlers && handlers->type == JsonValue::Type::Array) {
            // Of several screens bound for displays of different sizes, the one of this display's size wins.
            const JsonValue* chosen = nullptr;
            for (const JsonValue& handler : handlers->array) {
                const std::wstring type = StringOf(handler, L"device-type");
                if (type.starts_with(L"screened") &&
                    (!chosen || type == kOwnScreen || StringOf(*chosen, L"device-type") != kOwnScreen)) {
                    chosen = &handler;
                }
            }
            if (chosen) {
                handlers_[key] = *chosen;
            }
        }
    } else if (request.endpoint == L"remove_game" || request.endpoint == L"stop_game") {
        const std::wstring prefix = game + L"/";
        std::erase_if(handlers_, [&](const auto& entry) { return entry.first.starts_with(prefix); });
        std::erase_if(runs_, [&](const Run& run) { return run.key.starts_with(prefix); });
        if (idleKey_.starts_with(prefix)) {
            idleKey_.clear();
            idleUpdated_ = 0;
        }
    }
}

void AddOnScreens::HandleEvent(const std::wstring& game, const std::wstring& event, const JsonValue& data,
                               const std::vector<AddOnSettings>& addOns, ULONGLONG now) {
    if (game == kDiscordGame) {
        HandleDiscord(event, data, DiscordAddOn(addOns), now);
        return;
    }
    const std::wstring key = game + L"/" + event;
    if (const auto handler = handlers_.find(key); handler != handlers_.end()) {
        HandleBound(key, handler->second, data, now);
    }
}

void AddOnScreens::HandleDiscord(const std::wstring& event, const JsonValue& data, const AddOnSettings* addOn,
                                 ULONGLONG now) {
    const bool on = ValueOf(data) != 0.0;
    std::map<std::wstring, std::wstring> fields;
    for (const wchar_t* name : kFrameFields) {
        if (std::wstring value = FrameText(data, name); !value.empty()) {
            fields[name] = std::move(value);
        }
    }
    // The helper first resets every event when it starts.
    if (event == L"NOTIFICATION" && !on) {
        discordStart_ = now;
        return;
    }
    if (!discordStart_) {
        discordStart_ = now;
    }
    const std::wstring key = std::wstring(kDiscordGame) + L"/" + event;
    if (!on) {
        End(key);
        return;
    }
    const std::wstring user = fields.count(L"user") ? fields[L"user"] : std::wstring{};
    if ((event == L"USER_TALKING_SELF" || event.starts_with(L"SELF_")) && !user.empty()) {
        self_ = user;
    }
    const AddOnEventSpec* spec = FindDiscordEvent(event);
    if (!spec) {
        return;
    }
    // Your own voice has an event of its own; the one for anyone talking leaves you out.
    if (event == L"USER_TALKING_ANY" && !self_.empty() && user == self_) {
        return;
    }
    if (event == L"USER_TALKING_SELF") {
        std::erase_if(runs_, [&](const Run& run) {
            return run.key == L"DISCORD/USER_TALKING_ANY" && run.fields.count(L"user") &&
                   run.fields.at(L"user") == self_;
        });
    }
    AddOnEventScreen screen = addOn ? ScreenFor(*addOn, event) : DefaultDiscordScreen(event);
    if (!screen.enabled || screen.displays.empty()) {
        return;
    }
    // What the helper reports when it starts is the state so far, not something that just happened; only a screen
    // that lasts with its event (e.g. being muted) shows for it.
    if (event != L"NOTIFICATION" && !screen.whileOn && now - discordStart_ < kStartGraceMs) {
        return;
    }
    Start(key, std::move(screen), std::move(fields), now);
}

void AddOnScreens::HandleBound(const std::wstring& key, const JsonValue& handler, const JsonValue& data,
                               ULONGLONG now) {
    const JsonValue* datas = handler.Find(L"datas");
    if (!datas || datas->type != JsonValue::Type::Array) {
        return;
    }
    // Either frames, or ranges of the value that hold frames each.
    const double value = ValueOf(data);
    const JsonValue* frames = datas;
    for (const JsonValue& entry : datas->array) {
        const JsonValue* low = entry.Find(L"low");
        const JsonValue* high = entry.Find(L"high");
        if (!low || !high) {
            continue;
        }
        frames = nullptr;
        if (value >= low->NumberOr(0.0) && value <= high->NumberOr(0.0)) {
            frames = entry.Find(L"datas");
            break;
        }
    }
    AddOnEventScreen screen;
    std::vector<std::shared_ptr<const OledBitmap>> bitmaps;
    auto lengthOf = [](const JsonValue& frame) {
        const JsonValue* length = frame.Find(L"length-millis");
        const int millis = length ? static_cast<int>(length->NumberOr(0.0)) : 0;
        return millis > 0 ? std::min(millis, kAppLongestMs) : kAppDefaultMs;
    };
    if (frames && frames->type == JsonValue::Type::Array) {
        for (const JsonValue& frame : frames->array) {
            if (const JsonValue* hasText = frame.Find(L"has-text"); hasText && !hasText->BoolOr(true)) {
                if (std::shared_ptr<const OledBitmap> bitmap =
                        FrameBitmap(frame, data, StringOf(handler, L"device-type"))) {
                    AddOnDisplay display;
                    display.lengthMs = lengthOf(frame);
                    screen.displays.push_back(std::move(display));
                    bitmaps.push_back(std::move(bitmap));
                }
                continue;
            }
            std::vector<std::wstring> texts;
            const JsonValue* lines = frame.Find(L"lines");
            const std::vector<JsonValue> single{frame};
            for (const JsonValue& line : lines && lines->type == JsonValue::Type::Array ? lines->array : single) {
                const JsonValue* hasText = line.Find(L"has-text");
                if (!hasText || !hasText->BoolOr(false)) {
                    continue;
                }
                std::wstring content;
                const std::wstring field = StringOf(line, L"context-frame-key");
                const std::wstring arg = StringOf(line, L"arg");
                if (!field.empty()) {
                    content = FrameText(data, field);
                } else if (arg.starts_with(L"(") && arg.find(L": context-frame)") != std::wstring::npos) {
                    // The form "(key: context-frame)" reads a field of the event's frame.
                    content = FrameText(data, arg.substr(1, arg.find(L':') - 1));
                } else {
                    content = value == std::floor(value) ? std::to_wstring(static_cast<long long>(value))
                                                         : std::to_wstring(value);
                }
                texts.push_back(StringOf(line, L"prefix") + content + StringOf(line, L"suffix"));
            }
            if (texts.empty()) {
                continue;
            }
            AddOnDisplay display;
            display.heading = texts.size() > 1 ? texts.front() : std::wstring{};
            for (size_t i = texts.size() > 1 ? 1 : 0; i < texts.size(); ++i) {
                display.text += (display.text.empty() ? L"" : L"\n") + texts[i];
            }
            display.lengthMs = lengthOf(frame);
            screen.displays.push_back(std::move(display));
            bitmaps.push_back(nullptr);
        }
    }
    if (StringOf(handler, L"openchatmix-show") == L"idle") {
        // Only the first display: the program sends the next one when it changes.
        if (screen.displays.empty()) {
            if (idleKey_ == key) {
                idleKey_.clear();
                idleUpdated_ = 0;
            }
            return;
        }
        const AddOnDisplay& display = screen.displays.front();
        idleKey_ = key;
        idle_ = AddOnCard{display.icon, display.heading, display.text, {}, bitmaps.front()};
        idleUpdated_ = now;
        return;
    }
    if (screen.displays.empty()) {
        End(key);
        std::erase_if(runs_, [&](const Run& run) { return run.key == key; });
        return;
    }
    Start(key, std::move(screen), {}, now, std::move(bitmaps));
}

void AddOnScreens::Start(std::wstring key, AddOnEventScreen screen, std::map<std::wstring, std::wstring> fields,
                         ULONGLONG now, std::vector<std::shared_ptr<const OledBitmap>> bitmaps) {
    std::erase_if(runs_, [&](const Run& run) { return run.key == key; });
    screen.times = std::max(1, screen.times);
    runs_.push_back({std::move(key), std::move(screen), std::move(fields), now, std::move(bitmaps)});
}

void AddOnScreens::End(const std::wstring& key) {
    std::erase_if(runs_, [&](const Run& run) { return run.key == key && run.screen.whileOn; });
}

void AddOnScreens::Preview(AddOnEventScreen screen, std::map<std::wstring, std::wstring> fields, ULONGLONG now) {
    if (screen.displays.empty()) {
        return;
    }
    // A screen that lasts with its event runs for a few seconds here, as no event ends it.
    if (screen.whileOn) {
        const ULONGLONG cycle = std::max<ULONGLONG>(1, CycleLength(screen));
        screen.whileOn = false;
        screen.times = static_cast<int>(std::max<ULONGLONG>(1, (kPreviewLastingMs + cycle - 1) / cycle));
    }
    screen.enabled = true;
    Start(L"preview", std::move(screen), std::move(fields), now);
}

std::optional<AddOnCard> AddOnScreens::Current(ULONGLONG now, ULONGLONG& next) const {
    next = 0;
    auto show = [&](const Run& run) -> std::optional<AddOnCard> {
        const std::vector<AddOnDisplay>& displays = run.screen.displays;
        const ULONGLONG cycle = CycleLength(run.screen);
        if (displays.empty() || cycle == 0) {
            return std::nullopt;
        }
        const ULONGLONG elapsed = now - run.start;
        const ULONGLONG total = cycle * static_cast<ULONGLONG>(run.screen.times);
        if (!run.screen.whileOn && elapsed >= total) {
            return std::nullopt;
        }
        ULONGLONG into = elapsed % cycle;
        size_t index = 0;
        while (into >= DisplayLength(displays[index])) {
            into -= DisplayLength(displays[index]);
            ++index;
        }
        // A lasting screen of one display stays as it is until its event ends.
        if (!(run.screen.whileOn && displays.size() == 1)) {
            next = now + DisplayLength(displays[index]) - into;
        }
        const AddOnDisplay& display = displays[index];
        return AddOnCard{display.icon, FillTemplate(display.heading, run.fields), FillTemplate(display.text, run.fields),
                         display.image, index < run.bitmaps.size() ? run.bitmaps[index] : nullptr};
    };
    // Screens that end on their own go first, the newest of them; then those that last while their event does.
    for (bool lasting : {false, true}) {
        for (auto run = runs_.rbegin(); run != runs_.rend(); ++run) {
            if (run->screen.whileOn != lasting) {
                continue;
            }
            if (std::optional<AddOnCard> card = show(*run)) {
                return card;
            }
        }
    }
    return std::nullopt;
}

std::optional<AddOnCard> AddOnScreens::IdleCard(ULONGLONG now) const {
    if (idleKey_.empty() || now - idleUpdated_ >= kIdleStaleMs) {
        return std::nullopt;
    }
    return idle_;
}

void AddOnScreens::Clear() {
    *this = AddOnScreens{};
}

std::shared_ptr<IdleAnimation> CreateAddOnScreen(const AddOnCard& card) {
    return std::make_shared<AddOnScreen>(card);
}

void AddOnIdleScreen::Show(const AddOnCard& card) {
    AcquireSRWLockExclusive(&lock_);
    if (!(card == card_)) {
        card_ = card;
        ++version_;
    }
    ReleaseSRWLockExclusive(&lock_);
}

DWORD AddOnIdleScreen::Render(ULONGLONG elapsedMs, OledBitmap& frame) {
    bool changed = false;
    AcquireSRWLockShared(&lock_);
    if (version_ != drawnVersion_) {
        drawn_ = card_;
        drawnVersion_ = version_;
        changed = true;
    }
    ReleaseSRWLockShared(&lock_);
    // Played again from the start, e.g. after the visualizer's turn.
    if (elapsedMs < lastElapsed_) {
        cardStart_ = elapsedMs;
    }
    lastElapsed_ = elapsedMs;
    if (changed) {
        cardStart_ = elapsedMs;
        text_.reset();
        if (!drawn_.bitmap) {
            const std::atomic<bool> never{false};
            text_ = CreateAddOnScreen(drawn_);
            if (!text_->Load(never)) {
                text_.reset();
            }
        }
    }
    if (drawn_.bitmap) {
        frame = *drawn_.bitmap;
        return kIdlePollMs;
    }
    return text_ ? std::min(kIdlePollMs, text_->Render(elapsedMs - cardStart_, frame)) : kIdlePollMs;
}
