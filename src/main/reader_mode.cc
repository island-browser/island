#include "reader_mode.h"

#include <algorithm>
#include <array>
#include <utility>

#include "local_pages_html.h"

namespace island {
namespace {

constexpr std::array<std::string_view, 3> kWidths = {"narrow", "medium", "wide"};
constexpr std::array<std::string_view, 4> kThemes = {"auto", "light", "sepia", "dark"};

constexpr std::string_view kDataPlaceholder = "__ISLAND_READER_DATA__";

// JSON placed inside <script type="application/json"> must not be able to close the element or
// open a comment, and U+2028/2029 are line terminators in older parsers.
std::string EscapeForScript(std::string_view json_text) {
    std::string out;
    out.reserve(json_text.size() + json_text.size() / 16);
    for (std::size_t i = 0; i < json_text.size(); ++i) {
        const char c = json_text[i];
        if (c == '<') {
            out += "\\u003c";
        } else if (c == '>') {
            out += "\\u003e";
        } else if (c == '&') {
            out += "\\u0026";
        } else if (static_cast<unsigned char>(c) == 0xE2 && i + 2 < json_text.size() &&
                   static_cast<unsigned char>(json_text[i + 1]) == 0x80 &&
                   (static_cast<unsigned char>(json_text[i + 2]) == 0xA8 ||
                    static_cast<unsigned char>(json_text[i + 2]) == 0xA9)) {
            out += static_cast<unsigned char>(json_text[i + 2]) == 0xA8 ? "\\u2028" : "\\u2029";
            i += 2;
        } else {
            out += c;
        }
    }
    return out;
}

}  // namespace

bool IsReaderWidth(std::string_view value) noexcept {
    return std::find(kWidths.begin(), kWidths.end(), value) != kWidths.end();
}

bool IsReaderTheme(std::string_view value) noexcept {
    return std::find(kThemes.begin(), kThemes.end(), value) != kThemes.end();
}

ReaderSettings ReaderSettings::Normalized(int size, std::string_view width,
                                          std::string_view theme) {
    ReaderSettings settings;
    settings.size = std::clamp(size, kMinSize, kMaxSize);
    if (size <= 0) {
        settings.size = kDefaultSize;
    }
    if (IsReaderWidth(width)) {
        settings.width = std::string(width);
    }
    if (IsReaderTheme(theme)) {
        settings.theme = std::string(theme);
    }
    return settings;
}

std::string ReaderExtractionExpression() {
    std::string expression(ReaderExtractScript());
    expression += "\n;JSON.stringify(islandExtractArticle(document, window));";
    return expression;
}

std::optional<ReaderArticle> ParseReaderArticle(std::string_view extraction_json) {
    std::optional<json::Value> parsed = json::Parse(extraction_json);
    if (!parsed || !parsed->IsObject() || !parsed->BoolOr("ok", false)) {
        return std::nullopt;
    }
    json::Value* content = nullptr;
    for (auto& [key, member] : parsed->object_val) {
        if (key == "content") {
            content = &member;
        }
    }
    if (content == nullptr || !content->IsArray() || content->array_val.empty()) {
        return std::nullopt;
    }
    // Fit the document: bisect the number of top-level blocks kept.
    const auto fits = [&](std::size_t keep) {
        json::Value probe = *parsed;
        for (auto& [key, member] : probe.object_val) {
            if (key == "content") {
                member.array_val.resize(keep);
            }
        }
        return json::Serialize(probe).size() <= kMaxReaderPageBytes;
    };
    const std::size_t total = content->array_val.size();
    if (!fits(total)) {
        std::size_t low = 0;
        std::size_t high = total;  // fits(low) is true by construction; fits(high) is false
        while (high - low > 1) {
            const std::size_t mid = low + (high - low) / 2;
            (fits(mid) ? low : high) = mid;
        }
        if (low == 0) {
            return std::nullopt;
        }
        content->array_val.resize(low);
        parsed->Set("truncated", json::Value::Bool(true));
    }
    return ReaderArticle{.value = std::move(*parsed)};
}

std::string BuildReaderHtml(const ReaderArticle* article, std::string_view error,
                            std::string_view token, const ReaderSettings& settings,
                            bool app_dark) {
    json::Value data = json::Value::MakeObject();
    data.Set("token", json::Value::String(std::string(token)));
    data.Set("article", article != nullptr ? article->value : json::Value::Null());
    data.Set("error", json::Value::String(std::string(error)));
    data.Set("settings",
             json::Value::MakeObject()
                 .Set("size", json::Value::Int(settings.size))
                 .Set("width", json::Value::String(settings.width))
                 .Set("theme", json::Value::String(settings.theme)));
    data.Set("app_theme", json::Value::String(app_dark ? "dark" : "light"));

    std::string html(ReaderPageHtml());
    const std::size_t at = html.find(kDataPlaceholder);
    if (at != std::string::npos) {
        html.replace(at, kDataPlaceholder.size(), EscapeForScript(json::Serialize(data)));
    }
    return html;
}

std::string Base64Encode(std::string_view bytes) {
    static constexpr std::string_view kAlphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
        const unsigned value = (static_cast<unsigned char>(bytes[i]) << 16) |
                               (static_cast<unsigned char>(bytes[i + 1]) << 8) |
                               static_cast<unsigned char>(bytes[i + 2]);
        out += kAlphabet[(value >> 18) & 0x3F];
        out += kAlphabet[(value >> 12) & 0x3F];
        out += kAlphabet[(value >> 6) & 0x3F];
        out += kAlphabet[value & 0x3F];
    }
    if (i + 1 == bytes.size()) {
        const unsigned value = static_cast<unsigned char>(bytes[i]) << 16;
        out += kAlphabet[(value >> 18) & 0x3F];
        out += kAlphabet[(value >> 12) & 0x3F];
        out += "==";
    } else if (i + 2 == bytes.size()) {
        const unsigned value = (static_cast<unsigned char>(bytes[i]) << 16) |
                               (static_cast<unsigned char>(bytes[i + 1]) << 8);
        out += kAlphabet[(value >> 18) & 0x3F];
        out += kAlphabet[(value >> 12) & 0x3F];
        out += kAlphabet[(value >> 6) & 0x3F];
        out += '=';
    }
    return out;
}

std::string ReaderDataUrl(std::string_view html) {
    return "data:text/html;charset=utf-8;base64," + Base64Encode(html);
}

bool IsReaderEligibleUrl(std::string_view url) noexcept {
    return url.rfind("https://", 0) == 0 || url.rfind("http://", 0) == 0;
}

}  // namespace island
