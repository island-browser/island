#ifndef ISLAND_CONTENT_BLOCKER_H_
#define ISLAND_CONTENT_BLOCKER_H_

#include <cstdint>
#include <memory>
#include <string_view>

namespace island {

enum class ContentBlockerCategories : std::uint8_t {
    kNone = 0,
    kAds = 1 << 0,
    kTrackers = 1 << 1,
};

[[nodiscard]] constexpr ContentBlockerCategories operator|(ContentBlockerCategories first,
                                                           ContentBlockerCategories second) {
    return static_cast<ContentBlockerCategories>(static_cast<std::uint8_t>(first) |
                                                 static_cast<std::uint8_t>(second));
}

struct ContentBlockerResult {
    bool blocked = false;
    bool exception = false;
    bool important = false;
    bool matched_ads = false;
    bool matched_trackers = false;
    bool error = false;
};

// Immutable RAII owner of Brave adblock-rust's compiled filter engines. The
// stub build has the same API and always allows requests.
class ContentBlocker {
  public:
    static std::shared_ptr<ContentBlocker> Create(std::string_view ads, std::string_view trackers);

    ContentBlocker(const ContentBlocker&) = delete;
    ContentBlocker& operator=(const ContentBlocker&) = delete;
    ~ContentBlocker();

    [[nodiscard]] ContentBlockerResult Check(std::string_view url, std::string_view source_url,
                                             std::string_view request_type,
                                             ContentBlockerCategories categories) const;
    [[nodiscard]] std::uint32_t ads_rule_count() const noexcept { return ads_rule_count_; }
    [[nodiscard]] std::uint32_t tracker_rule_count() const noexcept { return tracker_rule_count_; }
    [[nodiscard]] bool available() const noexcept { return handle_ != nullptr; }
    [[nodiscard]] static bool CompiledIn() noexcept;

  private:
    ContentBlocker(void* handle, std::uint32_t ads_rule_count, std::uint32_t tracker_rule_count);

    void* handle_ = nullptr;
    std::uint32_t ads_rule_count_ = 0;
    std::uint32_t tracker_rule_count_ = 0;
};

}  // namespace island

#endif  // ISLAND_CONTENT_BLOCKER_H_
