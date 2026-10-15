#include "cef_content_blocker.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <optional>
#include <system_error>
#include <utility>

#include "content_blocker_policy.h"
#include "include/base/cef_bind.h"
#include "include/base/cef_callback.h"
#include "include/cef_request.h"
#include "include/cef_response.h"
#include "include/cef_task.h"
#include "include/cef_urlrequest.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"
#include "island_version.h"

namespace island {
namespace {

constexpr std::size_t kMaxListBytes = 32U * 1024U * 1024U;
constexpr std::string_view kListUrls[] = {
    "https://easylist.to/easylist/easylist.txt",
    "https://easylist.to/easylist/easyprivacy.txt",
};

bool IsAllowedListUrl(std::string_view url) {
    return url.rfind("https://easylist.to/", 0) == 0 ||
           url.rfind("https://easylist-downloads.adblockplus.org/", 0) == 0;
}

std::optional<std::string> RedirectUrl(const CefRefPtr<CefResponse>& response) {
    if (response == nullptr) {
        return std::nullopt;
    }
    std::string url = response->GetHeaderByName("Location").ToString();
    return IsAllowedListUrl(url) ? std::optional<std::string>(std::move(url)) : std::nullopt;
}

bool ReadFile(const std::filesystem::path& path, std::string* output) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return false;
    }
    output->assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return !input.bad() && !output->empty() && output->size() <= kMaxListBytes;
}

bool WriteFileAtomically(const std::filesystem::path& path, std::string_view body) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return false;
    }
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output.write(body.data(), static_cast<std::streamsize>(body.size()));
        if (!output) {
            std::filesystem::remove(temporary, error);
            return false;
        }
    }
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return false;
    }
    return true;
}

std::int64_t UnixSecondsForFile(const std::filesystem::path& path) {
    std::error_code error;
    const auto file_time = std::filesystem::last_write_time(path, error);
    if (error) {
        return 0;
    }
    const auto system_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        file_time - std::filesystem::file_time_type::clock::now() +
        std::chrono::system_clock::now());
    return std::chrono::duration_cast<std::chrono::seconds>(system_time.time_since_epoch()).count();
}

}  // namespace

class CefContentBlocker::FetchClient final : public CefURLRequestClient {
  public:
    FetchClient(CefRefPtr<CefContentBlocker> owner, int list)
        : owner_(std::move(owner)), list_(list) {}

    void Start(std::string url) { Load(std::move(url)); }

    void Cancel() {
        cancelled_ = true;
        if (request_ != nullptr) {
            request_->Cancel();
            request_ = nullptr;
        }
    }

    void OnRequestComplete(CefRefPtr<CefURLRequest> request) override {
        CEF_REQUIRE_UI_THREAD();
        if (cancelled_ || request_ == nullptr) {
            return;
        }
        request_ = nullptr;
        const CefRefPtr<CefResponse> response = request->GetResponse();
        const int status = response != nullptr ? response->GetStatus() : 0;
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            const std::optional<std::string> redirect = RedirectUrl(response);
            if (!redirect.has_value() || ++redirects_ > 5) {
                Finish(false, "EasyList redirected outside its download hosts");
            } else {
                body_.clear();
                Load(*redirect);
            }
            return;
        }
        if (overflowed_) {
            Finish(false, "A filter list was larger than 32 MiB");
        } else if (status < 200 || status > 299 || request->GetRequestStatus() != UR_SUCCESS) {
            Finish(false, status == 0 ? "Couldn't download the filter lists"
                                      : "Filter list HTTP " + std::to_string(status));
        } else if (body_.empty()) {
            Finish(false, "A downloaded filter list was empty");
        } else {
            Finish(true, {});
        }
    }

    void OnUploadProgress(CefRefPtr<CefURLRequest>, int64_t, int64_t) override {}
    void OnDownloadProgress(CefRefPtr<CefURLRequest>, int64_t, int64_t) override {}

    void OnDownloadData(CefRefPtr<CefURLRequest> request, const void* data,
                        size_t data_length) override {
        if (cancelled_ || request_ == nullptr || overflowed_) {
            return;
        }
        if (data_length > kMaxListBytes - std::min(body_.size(), kMaxListBytes)) {
            overflowed_ = true;
            request->Cancel();
            return;
        }
        body_.append(static_cast<const char*>(data), data_length);
    }

    bool GetAuthCredentials(bool, const CefString&, int, const CefString&, const CefString&,
                            CefRefPtr<CefAuthCallback>) override {
        return false;
    }

  private:
    void Load(std::string url) {
        if (!IsAllowedListUrl(url)) {
            Finish(false, "Refused an unexpected filter-list URL");
            return;
        }
        CefRefPtr<CefRequest> request = CefRequest::Create();
        request->SetURL(url);
        request->SetMethod("GET");
        CefRequest::HeaderMap headers;
        headers.emplace("User-Agent", "Island/" ISLAND_VERSION_STRING);
        headers.emplace("Accept", "text/plain");
        request->SetHeaderMap(headers);
        request->SetFlags(UR_FLAG_SKIP_CACHE | UR_FLAG_STOP_ON_REDIRECT);
        request_ = CefURLRequest::Create(request, this, nullptr);
        if (request_ == nullptr) {
            Finish(false, "Couldn't start the filter-list download");
        }
    }

    void Finish(bool ok, std::string error) {
        if (cancelled_ || owner_ == nullptr) {
            return;
        }
        CefRefPtr<CefContentBlocker> owner = owner_;
        owner_ = nullptr;
        owner->OnListFetched(list_, ok, std::move(body_), std::move(error));
    }

    CefRefPtr<CefContentBlocker> owner_;
    CefRefPtr<CefURLRequest> request_;
    std::string body_;
    int list_ = 0;
    int redirects_ = 0;
    bool overflowed_ = false;
    bool cancelled_ = false;

    IMPLEMENT_REFCOUNTING(FetchClient);
    DISALLOW_COPY_AND_ASSIGN(FetchClient);
};

CefContentBlocker::CefContentBlocker(std::filesystem::path profile_directory,
                                     ChangeCallback on_change)
    : profile_directory_(std::move(profile_directory)), on_change_(std::move(on_change)) {
    status_.compiled_in = ContentBlocker::CompiledIn();
    std::atomic_store(&allowlist_, std::make_shared<const std::vector<std::string>>());
}

CefContentBlocker::~CefContentBlocker() { Stop(); }

void CefContentBlocker::Start() {
    CEF_REQUIRE_UI_THREAD();
    if (stopped_ || !ContentBlocker::CompiledIn()) {
        if (!ContentBlocker::CompiledIn()) {
            std::lock_guard lock(status_mutex_);
            status_.error = "Built without Rust content-blocker support";
        }
        NotifyChanged();
        return;
    }
    {
        std::lock_guard lock(status_mutex_);
        status_.updating = true;
        status_.error.clear();
    }
    CefPostTask(TID_FILE_USER_BLOCKING,
                CefCreateClosureTask(base::BindOnce(&CefContentBlocker::LoadCache,
                                                    CefRefPtr<CefContentBlocker>(this))));
}

void CefContentBlocker::LoadCache() {
    const ContentBlockerCachePaths paths = ContentBlockerCacheForProfile(profile_directory_);
    std::string ads;
    std::string trackers;
    std::shared_ptr<ContentBlocker> engine;
    std::string error;
    std::int64_t updated = 0;
    if (ReadFile(paths.easylist, &ads) && ReadFile(paths.easyprivacy, &trackers)) {
        engine = ContentBlocker::Create(ads, trackers);
        updated =
            std::min(UnixSecondsForFile(paths.easylist), UnixSecondsForFile(paths.easyprivacy));
        if (engine == nullptr || !engine->available()) {
            error = "Couldn't compile the cached filter lists";
        }
    }
    const std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    const std::optional<std::chrono::system_clock::time_point> updated_time =
        updated > 0 ? std::optional<std::chrono::system_clock::time_point>(
                          std::chrono::system_clock::time_point{std::chrono::seconds(updated)})
                    : std::nullopt;
    const ContentBlockerStartupPolicy policy = DecideContentBlockerStartupPolicy(
        engine != nullptr && engine->available(), updated_time, now);
    CefPostTask(TID_UI, CefCreateClosureTask(base::BindOnce(
                            &CefContentBlocker::OnCacheLoaded, CefRefPtr<CefContentBlocker>(this),
                            std::move(engine), updated, policy.refresh, std::move(error))));
}

void CefContentBlocker::OnCacheLoaded(std::shared_ptr<ContentBlocker> engine,
                                      std::int64_t last_updated, bool refresh_due,
                                      std::string error) {
    CEF_REQUIRE_UI_THREAD();
    if (stopped_) {
        return;
    }
    if (engine != nullptr && engine->available()) {
        std::atomic_store(&engine_, engine);
        std::lock_guard lock(status_mutex_);
        status_.ready = true;
        status_.ads_rule_count = engine->ads_rule_count();
        status_.tracker_rule_count = engine->tracker_rule_count();
        status_.last_updated = last_updated;
    }
    if (!error.empty()) {
        std::lock_guard lock(status_mutex_);
        status_.error = std::move(error);
    }
    if (refresh_due) {
        StartFetch();
    } else {
        std::lock_guard lock(status_mutex_);
        status_.updating = false;
    }
    NotifyChanged();
}

void CefContentBlocker::UpdateNow() {
    CEF_REQUIRE_UI_THREAD();
    if (!stopped_ && ContentBlocker::CompiledIn()) {
        StartFetch();
    }
}

void CefContentBlocker::StartFetch() {
    CEF_REQUIRE_UI_THREAD();
    if (stopped_ || fetches_[0] != nullptr || fetches_[1] != nullptr) {
        return;
    }
    fetched_[0] = false;
    fetched_[1] = false;
    fetched_lists_[0].clear();
    fetched_lists_[1].clear();
    {
        std::lock_guard lock(status_mutex_);
        status_.updating = true;
        status_.error.clear();
    }
    for (int list = 0; list < 2; ++list) {
        fetches_[list] = new FetchClient(this, list);
        fetches_[list]->Start(std::string(kListUrls[list]));
    }
    NotifyChanged();
}

void CefContentBlocker::OnListFetched(int list, bool ok, std::string body, std::string error) {
    CEF_REQUIRE_UI_THREAD();
    if (stopped_ || list < 0 || list > 1) {
        return;
    }
    fetches_[list] = nullptr;
    if (!ok) {
        const int other = 1 - list;
        if (fetches_[other] != nullptr) {
            fetches_[other]->Cancel();
            fetches_[other] = nullptr;
        }
        {
            std::lock_guard lock(status_mutex_);
            status_.updating = false;
            status_.error = std::move(error);
        }
        NotifyChanged();
        return;
    }
    fetched_[list] = true;
    fetched_lists_[list] = std::move(body);
    if (fetched_[0] && fetched_[1]) {
        CompileAndStore(std::move(fetched_lists_[0]), std::move(fetched_lists_[1]));
    }
}

void CefContentBlocker::CompileAndStore(std::string ads, std::string trackers) {
    CefPostTask(
        TID_FILE_USER_BLOCKING,
        CefCreateClosureTask(base::BindOnce(
            [](CefRefPtr<CefContentBlocker> self, std::string ads_text, std::string tracker_text) {
                const ContentBlockerCachePaths paths =
                    ContentBlockerCacheForProfile(self->profile_directory_);
                std::string error;
                std::shared_ptr<ContentBlocker> engine =
                    ContentBlocker::Create(ads_text, tracker_text);
                if (engine == nullptr || !engine->available()) {
                    error = "Couldn't compile the downloaded filter lists";
                } else if (!WriteFileAtomically(paths.easylist, ads_text) ||
                           !WriteFileAtomically(paths.easyprivacy, tracker_text)) {
                    error = "Couldn't cache the filter lists";
                    engine.reset();
                }
                const std::int64_t updated =
                    error.empty() ? std::chrono::duration_cast<std::chrono::seconds>(
                                        std::chrono::system_clock::now().time_since_epoch())
                                        .count()
                                  : 0;
                CefPostTask(TID_UI, CefCreateClosureTask(base::BindOnce(
                                        &CefContentBlocker::OnFreshEngine, self, std::move(engine),
                                        updated, std::move(error))));
            },
            CefRefPtr<CefContentBlocker>(this), std::move(ads), std::move(trackers))));
}

void CefContentBlocker::OnFreshEngine(std::shared_ptr<ContentBlocker> engine,
                                      std::int64_t last_updated, std::string error) {
    CEF_REQUIRE_UI_THREAD();
    if (stopped_) {
        return;
    }
    if (engine != nullptr && engine->available()) {
        std::atomic_store(&engine_, engine);
    }
    {
        std::lock_guard lock(status_mutex_);
        status_.updating = false;
        if (engine != nullptr && engine->available()) {
            status_.ready = true;
            status_.ads_rule_count = engine->ads_rule_count();
            status_.tracker_rule_count = engine->tracker_rule_count();
            status_.last_updated = last_updated;
        }
        status_.error = std::move(error);
    }
    NotifyChanged();
}

void CefContentBlocker::Stop() {
    if (stopped_) {
        return;
    }
    stopped_ = true;
    on_change_ = nullptr;
    for (CefRefPtr<FetchClient>& fetch : fetches_) {
        if (fetch != nullptr) {
            fetch->Cancel();
            fetch = nullptr;
        }
    }
}

void CefContentBlocker::SetPolicy(bool block_ads, bool block_trackers,
                                  std::vector<std::string> allowlist) {
    block_ads_.store(block_ads);
    block_trackers_.store(block_trackers);
    std::atomic_store(&allowlist_,
                      std::make_shared<const std::vector<std::string>>(std::move(allowlist)));
}

ContentBlockerResult CefContentBlocker::Check(std::string_view url, std::string_view source_url,
                                              std::string_view request_type) const {
    const std::shared_ptr<ContentBlocker> engine = std::atomic_load(&engine_);
    if (engine == nullptr || !engine->available()) {
        return {};
    }
    const std::shared_ptr<const std::vector<std::string>> allowlist = std::atomic_load(&allowlist_);
    if (allowlist != nullptr && IsSiteAllowlisted(source_url, *allowlist)) {
        return {};
    }
    ContentBlockerCategories categories = ContentBlockerCategories::kNone;
    if (block_ads_.load()) {
        categories = categories | ContentBlockerCategories::kAds;
    }
    if (block_trackers_.load()) {
        categories = categories | ContentBlockerCategories::kTrackers;
    }
    return engine->Check(url, source_url, request_type, categories);
}

ContentBlockerStatus CefContentBlocker::status() const {
    std::lock_guard lock(status_mutex_);
    return status_;
}

void CefContentBlocker::NotifyChanged() {
    if (!stopped_ && on_change_) {
        on_change_();
    }
}

}  // namespace island
