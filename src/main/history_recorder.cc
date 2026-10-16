#include "history_recorder.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <future>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>

#include "search/index/search_index.h"
#include "search/types.h"

namespace island {
namespace {

constexpr std::string_view kVisitTag = "island-history-visit";
constexpr std::string_view kTitleTag = "island-history-title";

bool StartsWithAsciiCaseInsensitive(std::string_view text, std::string_view prefix) {
    if (text.size() < prefix.size()) {
        return false;
    }
    for (std::size_t index = 0; index < prefix.size(); ++index) {
        char actual = text[index];
        if (actual >= 'A' && actual <= 'Z') {
            actual = static_cast<char>(actual - 'A' + 'a');
        }
        if (actual != prefix[index]) {
            return false;
        }
    }
    return true;
}

}  // namespace

struct HistoryRecorder::Visit {
    std::string url;
    std::string title;
    std::uint64_t visited_at_ms = 0;
};

struct HistoryRecorder::WorkerState {
    std::unique_ptr<search::SearchIndex> index;
    std::vector<Visit> visits;
    std::size_t physical_documents = 0;
    std::size_t ingests_since_flush = 0;
    bool dirty = false;
};

namespace {

bool IngestVisit(search::SearchIndex& index, const HistoryRecorder::Visit& visit,
                 std::string_view tag) {
    search::DocumentInput input;
    input.url = visit.url;
    input.title = visit.title;
    input.visited_at_ms = visit.visited_at_ms;
    input.partition_tag = std::string(tag);
    return index.Ingest(input).has_value();
}

bool FlushIfDirty(HistoryRecorder::WorkerState& state) {
    if (!state.dirty || state.index == nullptr) {
        return true;
    }
    if (!state.index->Flush().has_value()) {
        return false;
    }
    state.dirty = false;
    state.ingests_since_flush = 0;
    return true;
}

std::vector<HistoryRecorder::Visit> ReadLogicalVisits(search::SearchIndex& index,
                                                      std::size_t max_entries) {
    std::vector<HistoryRecorder::Visit> visits;
    // A visit may have one later title-update document. Scan at most twice the
    // logical cap from the tail so even a segment from an older build cannot
    // turn startup into an unbounded replay.
    const std::uint64_t next = index.next_doc_id();
    const std::uint64_t scan_limit = static_cast<std::uint64_t>(max_entries) * 2;
    const std::uint64_t first = next > scan_limit ? next - scan_limit : 1;
    for (std::uint64_t raw_id = first; raw_id < next; ++raw_id) {
        const std::optional<search::StoredDocument> document =
            index.LoadDocument(static_cast<search::DocId>(raw_id));
        if (!document.has_value()) {
            continue;
        }
        const bool title_update = document->partition_tag == kTitleTag;
        if (title_update) {
            for (auto it = visits.rbegin(); it != visits.rend(); ++it) {
                if (it->url == document->url && it->visited_at_ms == document->visited_at_ms) {
                    it->title = document->title;
                    break;
                }
            }
            continue;
        }
        visits.push_back({.url = document->url,
                          .title = document->title,
                          .visited_at_ms = document->visited_at_ms});
    }
    if (visits.size() > max_entries) {
        visits.erase(visits.begin(), visits.end() - static_cast<std::ptrdiff_t>(max_entries));
    }
    return visits;
}

bool OpenEmptyIndex(const HistoryRecorder::Options& options, HistoryRecorder::WorkerState& state) {
    search::SearchIndex::Options index_options;
    index_options.segment_path = options.segment_path;
    auto opened = search::SearchIndex::Open(std::move(index_options));
    if (!opened.has_value()) {
        return false;
    }
    state.index = std::move(opened.value());
    state.physical_documents = static_cast<std::size_t>(state.index->next_doc_id() - 1);
    return true;
}

bool RebuildIndex(const HistoryRecorder::Options& options, HistoryRecorder::WorkerState& state) {
    // SearchIndex S0 cannot merge a segment that predates its MemTable. Keep a
    // recoverable backup, reopen an empty index, and ingest the bounded logical
    // history so this process's MemTable owns every document it may flush.
    state.index.reset();
    std::error_code error;
    std::filesystem::create_directories(options.segment_path.parent_path(), error);
    if (error) {
        return false;
    }

    std::filesystem::path backup = options.segment_path;
    backup += ".rebuild-backup";
    std::filesystem::remove(backup, error);
    error.clear();
    const bool had_segment = std::filesystem::exists(options.segment_path, error) && !error;
    if (had_segment) {
        std::filesystem::rename(options.segment_path, backup, error);
        if (error) {
            return false;
        }
    }

    state.physical_documents = 0;
    if (!OpenEmptyIndex(options, state)) {
        if (had_segment) {
            std::filesystem::rename(backup, options.segment_path, error);
        }
        return false;
    }
    for (const HistoryRecorder::Visit& visit : state.visits) {
        if (!IngestVisit(*state.index, visit, kVisitTag)) {
            state.index.reset();
            std::filesystem::remove(options.segment_path, error);
            if (had_segment) {
                std::filesystem::rename(backup, options.segment_path, error);
            }
            return OpenEmptyIndex(options, state);
        }
        ++state.physical_documents;
    }
    state.dirty = !state.visits.empty();
    state.ingests_since_flush = state.visits.size();
    if (state.dirty && !FlushIfDirty(state)) {
        return false;
    }
    std::filesystem::remove(backup, error);
    return true;
}

void Initialize(const HistoryRecorder::Options& options, HistoryRecorder::WorkerState& state) {
    std::error_code error;
    std::filesystem::create_directories(options.segment_path.parent_path(), error);
    if (!OpenEmptyIndex(options, state)) {
        return;
    }
    if (!state.index->has_segment()) {
        return;
    }
    state.visits = ReadLogicalVisits(*state.index, options.max_entries);
    static_cast<void>(RebuildIndex(options, state));
}

// Lowercased alphanumeric words of `text`. Query-syntax characters never reach SearchIndex:
// history text is not a search expression.
std::vector<std::string> QueryWords(std::string_view text) {
    std::vector<std::string> words;
    std::string word;
    const auto flush = [&] {
        if (!word.empty()) {
            words.push_back(std::move(word));
            word.clear();
        }
    };
    for (const char ch : text) {
        const auto byte = static_cast<unsigned char>(ch);
        if (std::isalnum(byte) != 0) {
            word.push_back(static_cast<char>(std::tolower(byte)));
        } else if (byte >= 0x80) {
            word.push_back(ch);
        } else {
            flush();
        }
    }
    flush();
    return words;
}

// Palette queries are typed character by character, so every query word also matches as a
// prefix of a title or URL word ("quok" finds "Quokka"). SearchIndex only matches whole terms.
bool MatchesAsPrefixes(const std::vector<std::string>& query_words,
                       const HistoryRecorder::Visit& visit) {
    std::vector<std::string> words = QueryWords(visit.title);
    for (std::string& word : QueryWords(visit.url)) {
        words.push_back(std::move(word));
    }
    return std::all_of(query_words.begin(), query_words.end(), [&](const std::string& query_word) {
        return std::any_of(words.begin(), words.end(),
                           [&](const std::string& word) { return word.starts_with(query_word); });
    });
}

std::vector<HistoryResult> QueryHistory(HistoryRecorder::WorkerState& state, std::string query,
                                        std::uint64_t query_now_ms, std::size_t max_results) {
    if (state.index == nullptr || query.empty() || max_results == 0) {
        return {};
    }

    search::Query search_query;
    const std::vector<std::string> query_words = QueryWords(query);
    if (query_words.empty()) {
        return {};
    }
    for (const std::string& word : query_words) {
        search_query.text += search_query.text.empty() ? "" : " ";
        search_query.text += word;
    }
    search_query.max_results = std::max<std::size_t>(state.physical_documents, max_results);
    const search::SearchResult raw = state.index->Query(search_query, query_now_ms);

    std::unordered_map<std::string, std::size_t> visit_counts;
    std::unordered_map<std::string, HistoryRecorder::Visit> latest_visits;
    for (const HistoryRecorder::Visit& visit : state.visits) {
        ++visit_counts[visit.url];
        latest_visits[visit.url] = visit;
    }

    std::unordered_map<std::string, HistoryResult> deduplicated;
    for (const search::SearchHit& hit : raw.hits) {
        const auto latest = latest_visits.find(hit.url);
        if (latest == latest_visits.end()) {
            continue;
        }
        HistoryResult& result = deduplicated[hit.url];
        if (result.url.empty() || latest->second.visited_at_ms >= result.visited_at_ms) {
            result.url = hit.url;
            result.title = latest->second.title;
            result.visited_at_ms = latest->second.visited_at_ms;
            result.visit_count = visit_counts[hit.url];
            result.score = std::max(result.score, hit.score);
        }
    }

    // Prefix-only matches rank below every whole-term hit; recency and visits order them.
    constexpr double kPrefixOnlyScore = 1e-6;
    for (const auto& [url, visit] : latest_visits) {
        if (deduplicated.contains(url) || !MatchesAsPrefixes(query_words, visit)) {
            continue;
        }
        HistoryResult& result = deduplicated[url];
        result.url = url;
        result.title = visit.title;
        result.visited_at_ms = visit.visited_at_ms;
        result.visit_count = visit_counts[url];
        result.score = kPrefixOnlyScore;
    }

    std::vector<HistoryResult> results;
    results.reserve(deduplicated.size());
    for (auto& [url, result] : deduplicated) {
        // SearchIndex supplies BM25 + recency. Repeated visits are an explicit
        // local intent signal, so apply a small logarithmic multiplier without
        // allowing raw frequency to swamp relevance.
        result.score *= 1.0 + 0.10 * std::log1p(static_cast<double>(result.visit_count));
        results.push_back(std::move(result));
    }
    std::sort(results.begin(), results.end(),
              [](const HistoryResult& lhs, const HistoryResult& rhs) {
                  if (lhs.score != rhs.score) {
                      return lhs.score > rhs.score;
                  }
                  if (lhs.visited_at_ms != rhs.visited_at_ms) {
                      return lhs.visited_at_ms > rhs.visited_at_ms;
                  }
                  return lhs.url < rhs.url;
              });
    if (results.size() > max_results) {
        results.resize(max_results);
    }
    return results;
}

// Every logged visit, grouped by URL: the visit count is the score, and the title and time of the
// latest visit are kept. Most visits first, then the most recent visit, then the URL.
std::vector<HistoryResult> MostVisited(const HistoryRecorder::WorkerState& state,
                                       std::size_t max_results) {
    std::unordered_map<std::string, HistoryResult> by_url;
    for (const HistoryRecorder::Visit& visit : state.visits) {
        HistoryResult& result = by_url[visit.url];
        ++result.visit_count;
        if (result.url.empty() || visit.visited_at_ms >= result.visited_at_ms) {
            result.url = visit.url;
            result.title = visit.title;
            result.visited_at_ms = visit.visited_at_ms;
        }
    }

    std::vector<HistoryResult> results;
    results.reserve(by_url.size());
    for (auto& [url, result] : by_url) {
        result.score = static_cast<double>(result.visit_count);
        results.push_back(std::move(result));
    }
    std::sort(results.begin(), results.end(),
              [](const HistoryResult& lhs, const HistoryResult& rhs) {
                  if (lhs.visit_count != rhs.visit_count) {
                      return lhs.visit_count > rhs.visit_count;
                  }
                  if (lhs.visited_at_ms != rhs.visited_at_ms) {
                      return lhs.visited_at_ms > rhs.visited_at_ms;
                  }
                  return lhs.url < rhs.url;
              });
    if (results.size() > max_results) {
        results.resize(max_results);
    }
    return results;
}

bool ClearHistory(const HistoryRecorder::Options& options, HistoryRecorder::WorkerState& state) {
    state.index.reset();
    state.visits.clear();
    state.physical_documents = 0;
    state.ingests_since_flush = 0;
    state.dirty = false;

    std::error_code error;
    const auto remove_artifact = [&error](const std::filesystem::path& path) {
        error.clear();
        std::filesystem::remove(path, error);
        return !error;
    };
    bool cleared = remove_artifact(options.segment_path);
    std::filesystem::path temporary = options.segment_path;
    temporary += ".tmp";
    cleared = remove_artifact(temporary) && cleared;
    std::filesystem::path backup = options.segment_path;
    backup += ".rebuild-backup";
    cleared = remove_artifact(backup) && cleared;

    // Segment validation deliberately quarantines corrupt history instead of
    // deleting it. A user-requested clear must remove those siblings too.
    const std::string quarantine_prefix = options.segment_path.filename().string() + ".corrupt-";
    error.clear();
    std::filesystem::directory_iterator entries(options.segment_path.parent_path(), error);
    if (error) {
        cleared = false;
    } else {
        const std::filesystem::directory_iterator end;
        while (entries != end) {
            const std::filesystem::directory_entry& entry = *entries;
            const std::string filename = entry.path().filename().string();
            if (filename.starts_with(quarantine_prefix)) {
                cleared = remove_artifact(entry.path()) && cleared;
            }
            error.clear();
            entries.increment(error);
            if (error) {
                cleared = false;
                break;
            }
        }
    }
    return cleared && OpenEmptyIndex(options, state);
}

}  // namespace

HistoryRecorder::HistoryRecorder(Options options) : options_(std::move(options)) {
    if (options_.max_entries == 0) {
        options_.max_entries = 1;
    }
    if (options_.flush_every_ingests == 0) {
        options_.flush_every_ingests = 1;
    }
    worker_ = std::thread([this] { Run(); });
}

HistoryRecorder::~HistoryRecorder() {
    BeginShutdown();
    if (worker_.joinable()) {
        worker_.join();
    }
}

std::string HistoryRecorder::NormalizeHistoryUrl(std::string_view url) {
    if ((!StartsWithAsciiCaseInsensitive(url, "http://") &&
         !StartsWithAsciiCaseInsensitive(url, "https://")) ||
        search::IsPrivacyRefusedUrl(url)) {
        return {};
    }
    const std::size_t fragment = url.find('#');
    return std::string(url.substr(0, fragment));
}

bool HistoryRecorder::RecordNavigation(std::string url, std::string title,
                                       std::uint64_t visited_at_ms, bool private_mode) {
    if (private_mode) {
        return false;
    }
    url = NormalizeHistoryUrl(url);
    if (url.empty()) {
        return false;
    }
    Visit visit{.url = std::move(url), .title = std::move(title), .visited_at_ms = visited_at_ms};
    return Enqueue([this, visit = std::move(visit)](WorkerState& state) mutable {
        if (state.index == nullptr || !IngestVisit(*state.index, visit, kVisitTag)) {
            return;
        }
        state.visits.push_back(visit);
        ++state.physical_documents;
        ++state.ingests_since_flush;
        state.dirty = true;
        if (state.visits.size() > options_.max_entries) {
            state.visits.erase(
                state.visits.begin(),
                state.visits.end() - static_cast<std::ptrdiff_t>(options_.max_entries));
            static_cast<void>(RebuildIndex(options_, state));
        } else if (state.ingests_since_flush >= options_.flush_every_ingests) {
            static_cast<void>(FlushIfDirty(state));
        }
    });
}

bool HistoryRecorder::UpdateTitle(std::string url, std::string title) {
    url = NormalizeHistoryUrl(url);
    if (url.empty()) {
        return false;
    }
    return Enqueue(
        [this, url = std::move(url), title = std::move(title)](WorkerState& state) mutable {
            for (auto it = state.visits.rbegin(); it != state.visits.rend(); ++it) {
                if (it->url != url) {
                    continue;
                }
                it->title = std::move(title);
                if (state.index != nullptr && IngestVisit(*state.index, *it, kTitleTag)) {
                    ++state.physical_documents;
                    ++state.ingests_since_flush;
                    state.dirty = true;
                    if (state.physical_documents > options_.max_entries * 2) {
                        static_cast<void>(RebuildIndex(options_, state));
                    } else if (state.ingests_since_flush >= options_.flush_every_ingests) {
                        static_cast<void>(FlushIfDirty(state));
                    }
                }
                return;
            }
        });
}

void HistoryRecorder::QueryAsync(std::string query, std::uint64_t query_now_ms,
                                 std::size_t max_results, QueryCallback callback) {
    static_cast<void>(Enqueue([query = std::move(query), query_now_ms, max_results,
                               callback = std::move(callback)](WorkerState& state) mutable {
        std::vector<HistoryResult> results =
            QueryHistory(state, std::move(query), query_now_ms, max_results);
        if (callback) {
            callback(std::move(results));
        }
    }));
}

void HistoryRecorder::MostVisitedAsync(std::size_t max_results, QueryCallback callback) {
    static_cast<void>(
        Enqueue([max_results, callback = std::move(callback)](WorkerState& state) mutable {
            std::vector<HistoryResult> results = MostVisited(state, max_results);
            if (callback) {
                callback(std::move(results));
            }
        }));
}

void HistoryRecorder::ClearAsync(ClearCallback callback) {
    static_cast<void>(Enqueue([this, callback = std::move(callback)](WorkerState& state) mutable {
        const bool cleared = ClearHistory(options_, state);
        if (callback) {
            callback(cleared);
        }
    }));
}

void HistoryRecorder::BeginShutdown() {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_one();
}

bool HistoryRecorder::Enqueue(Task task) {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            return false;
        }
        tasks_.push_back(std::move(task));
    }
    wake_.notify_one();
    return true;
}

void HistoryRecorder::Run() {
    WorkerState state;
    Initialize(options_, state);
    auto next_flush = std::chrono::steady_clock::now() + options_.flush_interval;

    for (;;) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait_until(lock, next_flush, [this] { return stopping_ || !tasks_.empty(); });
            if (!tasks_.empty()) {
                task = std::move(tasks_.front());
                tasks_.pop_front();
            } else if (stopping_) {
                break;
            }
        }
        if (task) {
            task(state);
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= next_flush) {
            static_cast<void>(FlushIfDirty(state));
            next_flush = now + options_.flush_interval;
        }
    }
    static_cast<void>(FlushIfDirty(state));
}

void HistoryRecorder::WaitForIdleForTest() {
    auto completed = std::make_shared<std::promise<void>>();
    std::future<void> future = completed->get_future();
    if (Enqueue([completed](WorkerState&) { completed->set_value(); })) {
        future.wait();
    }
}

std::vector<HistoryResult> HistoryRecorder::QueryForTest(std::string query,
                                                         std::uint64_t query_now_ms,
                                                         std::size_t max_results) {
    auto completed = std::make_shared<std::promise<std::vector<HistoryResult>>>();
    std::future<std::vector<HistoryResult>> future = completed->get_future();
    QueryAsync(std::move(query), query_now_ms, max_results,
               [completed](std::vector<HistoryResult> results) mutable {
                   completed->set_value(std::move(results));
               });
    return future.get();
}

std::vector<HistoryResult> HistoryRecorder::MostVisitedForTest(std::size_t max_results) {
    auto completed = std::make_shared<std::promise<std::vector<HistoryResult>>>();
    std::future<std::vector<HistoryResult>> future = completed->get_future();
    MostVisitedAsync(max_results, [completed](std::vector<HistoryResult> results) mutable {
        completed->set_value(std::move(results));
    });
    return future.get();
}

std::size_t HistoryRecorder::EntryCountForTest() {
    auto completed = std::make_shared<std::promise<std::size_t>>();
    std::future<std::size_t> future = completed->get_future();
    if (!Enqueue([completed](WorkerState& state) { completed->set_value(state.visits.size()); })) {
        return 0;
    }
    return future.get();
}

}  // namespace island
