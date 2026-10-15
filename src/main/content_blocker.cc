#include "content_blocker.h"

#include <string>

namespace island {
namespace {

#if defined(ISLAND_ENABLE_CONTENT_BLOCKER)
constexpr std::uint32_t kResultBlocked = 1U << 0;
constexpr std::uint32_t kResultException = 1U << 1;
constexpr std::uint32_t kResultImportant = 1U << 2;
constexpr std::uint32_t kResultAds = 1U << 3;
constexpr std::uint32_t kResultTrackers = 1U << 4;
constexpr std::uint32_t kResultError = 1U << 31;

extern "C" {
void* island_adblock_engine_create(const char* ads, const char* trackers);
std::uint32_t island_adblock_engine_check(const void* engine, const char* url,
                                          const char* source_url, const char* request_type,
                                          std::uint8_t category_mask);
std::uint32_t island_adblock_engine_ads_rule_count(const void* engine);
std::uint32_t island_adblock_engine_tracker_rule_count(const void* engine);
void island_adblock_engine_destroy(void* engine);
}
#endif

}  // namespace

ContentBlocker::ContentBlocker(void* handle, std::uint32_t ads_rule_count,
                               std::uint32_t tracker_rule_count)
    : handle_(handle), ads_rule_count_(ads_rule_count), tracker_rule_count_(tracker_rule_count) {}

ContentBlocker::~ContentBlocker() {
#if defined(ISLAND_ENABLE_CONTENT_BLOCKER)
    if (handle_ != nullptr) {
        island_adblock_engine_destroy(handle_);
    }
#endif
}

std::shared_ptr<ContentBlocker> ContentBlocker::Create(std::string_view ads,
                                                       std::string_view trackers) {
#if defined(ISLAND_ENABLE_CONTENT_BLOCKER)
    // The C ABI is deliberately NUL-terminated; downloaded text containing a
    // NUL is rejected instead of silently compiling a prefix.
    if (ads.find('\0') != std::string_view::npos || trackers.find('\0') != std::string_view::npos) {
        return nullptr;
    }
    const std::string ads_text(ads);
    const std::string tracker_text(trackers);
    void* handle = island_adblock_engine_create(ads_text.c_str(), tracker_text.c_str());
    if (handle == nullptr) {
        return nullptr;
    }
    return std::shared_ptr<ContentBlocker>(
        new ContentBlocker(handle, island_adblock_engine_ads_rule_count(handle),
                           island_adblock_engine_tracker_rule_count(handle)));
#else
    static_cast<void>(ads);
    static_cast<void>(trackers);
    return std::shared_ptr<ContentBlocker>(new ContentBlocker(nullptr, 0, 0));
#endif
}

ContentBlockerResult ContentBlocker::Check(std::string_view url, std::string_view source_url,
                                           std::string_view request_type,
                                           ContentBlockerCategories categories) const {
#if defined(ISLAND_ENABLE_CONTENT_BLOCKER)
    if (handle_ == nullptr || categories == ContentBlockerCategories::kNone ||
        url.find('\0') != std::string_view::npos ||
        source_url.find('\0') != std::string_view::npos ||
        request_type.find('\0') != std::string_view::npos) {
        return {};
    }
    const std::string request_url(url);
    const std::string source(source_url);
    const std::string type(request_type);
    const std::uint32_t flags =
        island_adblock_engine_check(handle_, request_url.c_str(), source.c_str(), type.c_str(),
                                    static_cast<std::uint8_t>(categories));
    return {
        .blocked = (flags & kResultBlocked) != 0,
        .exception = (flags & kResultException) != 0,
        .important = (flags & kResultImportant) != 0,
        .matched_ads = (flags & kResultAds) != 0,
        .matched_trackers = (flags & kResultTrackers) != 0,
        .error = (flags & kResultError) != 0,
    };
#else
    static_cast<void>(url);
    static_cast<void>(source_url);
    static_cast<void>(request_type);
    static_cast<void>(categories);
    return {};
#endif
}

bool ContentBlocker::CompiledIn() noexcept {
#if defined(ISLAND_ENABLE_CONTENT_BLOCKER)
    return true;
#else
    return false;
#endif
}

}  // namespace island
