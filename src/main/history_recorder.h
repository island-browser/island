#ifndef ISLAND_HISTORY_RECORDER_H_
#define ISLAND_HISTORY_RECORDER_H_

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace island {

struct HistoryResult {
    std::string url;
    std::string title;
    std::uint64_t visited_at_ms = 0;
    std::size_t visit_count = 0;
    double score = 0.0;

    bool operator==(const HistoryResult&) const = default;
};

// Owns Island's on-device history SearchIndex on one background sequence.
// Every Ingest, Query and Flush runs on that sequence because SearchIndex S0
// permits concurrent readers but no reader/writer overlap. The UI only queues
// work and receives query/clear callbacks.
class HistoryRecorder final {
  public:
    struct Options {
        std::filesystem::path segment_path;
        std::size_t max_entries = 50'000;
        std::size_t flush_every_ingests = 50;
        std::chrono::milliseconds flush_interval = std::chrono::seconds(60);
    };

    using QueryCallback = std::function<void(std::vector<HistoryResult>)>;
    using ClearCallback = std::function<void(bool)>;

    explicit HistoryRecorder(Options options);
    ~HistoryRecorder();

    HistoryRecorder(const HistoryRecorder&) = delete;
    HistoryRecorder& operator=(const HistoryRecorder&) = delete;

    // Returns false without queueing anything when the URL is not an http(s)
    // page, carries credentials, or belongs to a private browsing context.
    bool RecordNavigation(std::string url, std::string title, std::uint64_t visited_at_ms,
                          bool private_mode = false);

    // Re-indexes a title that arrived after the commit without counting a new
    // visit. Unknown/non-web URLs are ignored.
    bool UpdateTitle(std::string url, std::string title);

    void QueryAsync(std::string query, std::uint64_t query_now_ms, std::size_t max_results,
                    QueryCallback callback);
    void ClearAsync(ClearCallback callback = {});

    // Stops accepting work and asks the worker to flush after all queued work.
    // The flush itself stays off the caller's thread. Destruction joins it.
    void BeginShutdown();

    // Deterministic seams for the CEF-free GoogleTests.
    void WaitForIdleForTest();
    [[nodiscard]] std::vector<HistoryResult> QueryForTest(std::string query,
                                                          std::uint64_t query_now_ms,
                                                          std::size_t max_results = 8);
    [[nodiscard]] std::size_t EntryCountForTest();

    [[nodiscard]] static std::string NormalizeHistoryUrl(std::string_view url);

    // Public only so the out-of-line worker helpers can keep the header free
    // of SearchIndex; callers should treat these as implementation details.
    struct Visit;
    struct WorkerState;

  private:
    using Task = std::function<void(WorkerState&)>;

    bool Enqueue(Task task);
    void Run();

    Options options_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Task> tasks_;
    bool stopping_ = false;
    std::thread worker_;
};

}  // namespace island

#endif  // ISLAND_HISTORY_RECORDER_H_
