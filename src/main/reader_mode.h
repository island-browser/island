#ifndef ISLAND_READER_MODE_H_
#define ISLAND_READER_MODE_H_

// Reader view, CEF-free. The window runs the embedded extraction script in the page (DevTools
// Runtime.evaluate), turns its JSON result into Island's own reader document, and loads that
// document into the tab as a data: URL. Nothing here fetches anything: the script and the page
// template are embedded at build time (src/main/reader/).

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "json_util.h"

namespace island {

// The reader's appearance, persisted in prefs. Normalize() snaps unknown values to defaults.
struct ReaderSettings {
    static constexpr int kMinSize = 14;
    static constexpr int kMaxSize = 28;
    static constexpr int kDefaultSize = 18;

    int size = kDefaultSize;
    std::string width = "medium";  // narrow | medium | wide
    std::string theme = "auto";    // auto | light | sepia | dark

    [[nodiscard]] static ReaderSettings Normalized(int size, std::string_view width,
                                                   std::string_view theme);
    [[nodiscard]] bool operator==(const ReaderSettings&) const = default;
};

[[nodiscard]] bool IsReaderWidth(std::string_view value) noexcept;
[[nodiscard]] bool IsReaderTheme(std::string_view value) noexcept;

// The JavaScript expression Runtime.evaluate runs in the page: the extractor followed by a call
// that yields a JSON string.
[[nodiscard]] std::string ReaderExtractionExpression();

// A page's extraction result, validated and trimmed to fit a data: URL.
struct ReaderArticle {
    json::Value value;  // {ok, title, ..., content}
    [[nodiscard]] std::string_view title() const { return value.StringOr("title", ""); }
};

// The data: URL limit for browser-initiated navigations is 2 MiB; stay below it after base64.
inline constexpr std::size_t kMaxReaderPageBytes = 1'400'000;

// Parses the extractor's JSON string. std::nullopt when it is malformed or reports ok:false.
// Drops trailing content (and marks the article truncated) until the page fits.
[[nodiscard]] std::optional<ReaderArticle> ParseReaderArticle(std::string_view extraction_json);

// The reader document. `article` is null for the "no article here" page; `error` is shown then.
// `token` authenticates the page's messages to the window; `app_dark` is the app theme "Auto"
// follows.
[[nodiscard]] std::string BuildReaderHtml(const ReaderArticle* article, std::string_view error,
                                          std::string_view token, const ReaderSettings& settings,
                                          bool app_dark);
// "data:text/html;charset=utf-8;base64,...".
[[nodiscard]] std::string ReaderDataUrl(std::string_view html);
[[nodiscard]] std::string Base64Encode(std::string_view bytes);

// Whether `url` can be shown in reader view (an http or https page).
[[nodiscard]] bool IsReaderEligibleUrl(std::string_view url) noexcept;

}  // namespace island

#endif  // ISLAND_READER_MODE_H_
