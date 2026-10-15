#ifndef ISLAND_CEF_CONTENT_BLOCKER_H_
#define ISLAND_CEF_CONTENT_BLOCKER_H_

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "content_blocker.h"
#include "include/cef_base.h"

namespace island {

struct ContentBlockerStatus {
    bool compiled_in = false;
    bool ready = false;
    bool updating = false;
    std::uint32_t ads_rule_count = 0;
    std::uint32_t tracker_rule_count = 0;
    std::int64_t last_updated = 0;
    std::string error;
};

// Owns asynchronous EasyList/EasyPrivacy cache loading, refresh downloads,
// background compilation, and the atomically published immutable engine.
class CefContentBlocker final : public CefBaseRefCounted {
  public:
    using ChangeCallback = std::function<void()>;

    CefContentBlocker(std::filesystem::path profile_directory, ChangeCallback on_change);

    void Start();
    void UpdateNow();
    void Stop();
    void SetPolicy(bool block_ads, bool block_trackers, std::vector<std::string> allowlist);

    [[nodiscard]] ContentBlockerResult Check(std::string_view url, std::string_view source_url,
                                             std::string_view request_type) const;
    [[nodiscard]] ContentBlockerStatus status() const;

    class FetchClient;

  private:
    ~CefContentBlocker() override;

    void LoadCache();
    void OnCacheLoaded(std::shared_ptr<ContentBlocker> engine, std::int64_t last_updated,
                       bool refresh_due, std::string error);
    void StartFetch();
    void OnListFetched(int list, bool ok, std::string body, std::string error);
    void CompileAndStore(std::string ads, std::string trackers);
    void OnFreshEngine(std::shared_ptr<ContentBlocker> engine, std::int64_t last_updated,
                       std::string error);
    void NotifyChanged();

    std::filesystem::path profile_directory_;
    ChangeCallback on_change_;
    mutable std::mutex status_mutex_;
    ContentBlockerStatus status_;
    std::shared_ptr<ContentBlocker> engine_;
    std::atomic<bool> block_ads_{true};
    std::atomic<bool> block_trackers_{true};
    std::shared_ptr<const std::vector<std::string>> allowlist_;
    CefRefPtr<FetchClient> fetches_[2];
    std::string fetched_lists_[2];
    bool fetched_[2] = {false, false};
    bool stopped_ = false;

    IMPLEMENT_REFCOUNTING(CefContentBlocker);
    DISALLOW_COPY_AND_ASSIGN(CefContentBlocker);
};

}  // namespace island

#endif  // ISLAND_CEF_CONTENT_BLOCKER_H_
