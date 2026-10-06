#include "search/index/search_index.h"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <span>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "search/store/segment_writer.h"
#include "search/tokenizer.h"

namespace island {
namespace search {
namespace {

SearchError MakeError(SearchErrorKind kind, std::string detail) {
    SearchError error;
    error.kind = kind;
    error.detail = std::move(detail);
    return error;
}

bool IsAsciiAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

bool IsSchemeChar(char c) {
    return IsAsciiAlpha(c) || (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.';
}

char LowerAscii(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

// WHATWG URL parsing strips leading and trailing C0 controls and spaces
// before anything else, so " data:..." is still a data: URL to a browser.
std::string_view TrimC0ControlsAndSpace(std::string_view url) {
    const auto is_c0_or_space = [](char c) { return static_cast<unsigned char>(c) <= 0x20; };
    while (!url.empty() && is_c0_or_space(url.front())) {
        url.remove_prefix(1);
    }
    while (!url.empty() && is_c0_or_space(url.back())) {
        url.remove_suffix(1);
    }
    return url;
}

// Walks a trimmed URL the way the WHATWG parser sees it: every ASCII tab and
// newline is removed first, so "da\tta:" is a data: URL. Skipping them in
// place instead of building a stripped copy keeps IsPrivacyRefusedUrl free of
// allocation, which matters because it is noexcept.
class UrlCursor {
  public:
    explicit UrlCursor(std::string_view url) : url_(url) { SkipTabsAndNewlines(); }

    [[nodiscard]] bool done() const { return pos_ >= url_.size(); }
    [[nodiscard]] char peek() const { return url_[pos_]; }
    void Advance() {
        ++pos_;
        SkipTabsAndNewlines();
    }

  private:
    void SkipTabsAndNewlines() {
        while (pos_ < url_.size() &&
               (url_[pos_] == '\t' || url_[pos_] == '\n' || url_[pos_] == '\r')) {
            ++pos_;
        }
    }

    std::string_view url_;
    std::size_t pos_ = 0;
};

// The WHATWG "special" schemes whose authority may follow the colon after any
// run of '/' or '\' -- including none, so "https:user@host" carries userinfo.
bool IsSpecialScheme(std::string_view scheme) {
    return scheme == "http" || scheme == "https" || scheme == "ws" || scheme == "wss" ||
           scheme == "ftp" || scheme == "file";
}

// True when the authority starting at `cursor` carries userinfo. It ends at
// '/', '?' or '#' (and '\' for special schemes); an '@' before that means
// user[:pass]@host. An '@' later in the URL -- a query parameter holding an
// address, say -- is not userinfo and must not trip the refusal.
bool AuthorityHasUserinfo(UrlCursor& cursor, bool special) {
    for (; !cursor.done(); cursor.Advance()) {
        const char c = cursor.peek();
        if (c == '@') {
            return true;
        }
        if (c == '/' || c == '?' || c == '#' || (special && c == '\\')) {
            return false;
        }
    }
    return false;
}

// Merges `postings` into `candidates`, keeping only ids in [lo, hi).
void CollectRange(std::span<const std::uint64_t> postings, std::uint64_t lo, std::uint64_t hi,
                  std::unordered_set<DocId>& candidates) {
    for (const std::uint64_t id : postings) {
        if (id >= lo && id < hi) {
            candidates.insert(id);
        }
    }
}

FieldFrequencies CountTokens(const std::vector<std::string>& tokens) {
    FieldFrequencies frequencies;
    for (const std::string& token : tokens) {
        ++frequencies[token];
    }
    return frequencies;
}

}  // namespace

bool IsPrivacyRefusedUrl(std::string_view url) noexcept {
    UrlCursor cursor(TrimC0ControlsAndSpace(url));

    // scheme = ASCII alpha *( alnum / "+" / "-" / "." ) ":". Only schemes up
    // to five letters are compared by name, so a longer one is kept as a
    // truncated prefix flagged as matching none of them.
    if (cursor.done() || !IsAsciiAlpha(cursor.peek())) {
        // No scheme: a relative reference has no authority to inspect.
        return false;
    }
    char scheme_buffer[5] = {};
    std::size_t scheme_length = 0;
    bool scheme_too_long = false;
    for (; !cursor.done() && IsSchemeChar(cursor.peek()); cursor.Advance()) {
        if (scheme_length < sizeof(scheme_buffer)) {
            scheme_buffer[scheme_length++] = LowerAscii(cursor.peek());
        } else {
            scheme_too_long = true;
        }
    }
    if (cursor.done() || cursor.peek() != ':') {
        return false;
    }
    cursor.Advance();
    const std::string_view scheme =
        scheme_too_long ? std::string_view() : std::string_view(scheme_buffer, scheme_length);

    if (scheme == "data") {
        return true;
    }
    if (IsSpecialScheme(scheme)) {
        while (!cursor.done() && (cursor.peek() == '/' || cursor.peek() == '\\')) {
            cursor.Advance();
        }
        return AuthorityHasUserinfo(cursor, /*special=*/true);
    }
    // Any other scheme has an authority only when "//" follows the colon, so
    // "about:blank#x=http://a@b" carries no userinfo.
    for (int slash = 0; slash < 2; ++slash) {
        if (cursor.done() || cursor.peek() != '/') {
            return false;
        }
        cursor.Advance();
    }
    return AuthorityHasUserinfo(cursor, /*special=*/false);
}

SearchIndex::SearchIndex(Options options, std::unique_ptr<Segment> segment,
                         std::optional<SearchError> segment_error)
    : options_(std::move(options)),
      memtable_(segment ? segment->next_doc_id() : 1),
      segment_(std::move(segment)),
      segment_error_(std::move(segment_error)),
      cache_(options_.block_cache_bytes) {
    if (segment_) {
        segment_boundary_ = segment_->next_doc_id();
        // Open mapped a segment written before this object existed, so the
        // MemTable starts empty and does not contain those documents.
        segment_predates_memtable_ = true;
    }
}

Expected<std::unique_ptr<SearchIndex>, SearchError> SearchIndex::Open(Options options) {
    std::unique_ptr<Segment> segment;
    std::optional<SearchError> segment_error;

    auto opened = Segment::Open(options.segment_path);
    if (opened.has_value()) {
        segment = std::move(opened.value());
    } else {
        // A missing or rejected segment is not a failure to open the index: it
        // runs MemTable-only, which is exactly the graceful degradation the
        // quarantine path exists to provide.
        segment_error = opened.error();
    }

    return Expected<std::unique_ptr<SearchIndex>, SearchError>(std::unique_ptr<SearchIndex>(
        new SearchIndex(std::move(options), std::move(segment), std::move(segment_error))));
}

Expected<DocId, SearchError> SearchIndex::Ingest(const DocumentInput& input) {
    if (input.url.empty()) {
        return Expected<DocId, SearchError>::Error(
            MakeError(SearchErrorKind::kInvalidInput, "ingest requires a non-empty url"));
    }
    if (IsPrivacyRefusedUrl(input.url)) {
        return Expected<DocId, SearchError>::Error(
            MakeError(SearchErrorKind::kRefusedForPrivacy,
                      "refused: data: URLs and credentialed URLs never enter a persisted index"));
    }

    // One tokenizer pass shared with Query: the same Tokenize over the same
    // stored strings, so what is indexed and what is scored cannot diverge.
    const std::vector<std::string> title_tokens = Tokenize(input.title);
    const std::vector<std::string> url_tokens = Tokenize(input.url);

    StoredDocument document;
    document.url = input.url;
    document.title = input.title;
    document.visited_at_ms = input.visited_at_ms;
    document.partition_tag = input.partition_tag;

    const std::optional<DocId> id =
        memtable_.AddDocument(std::move(document), title_tokens, url_tokens);
    if (!id.has_value()) {
        return Expected<DocId, SearchError>::Error(MakeError(
            SearchErrorKind::kInvalidInput, "document field exceeds the 1 MiB arena chunk size"));
    }
    // The cache is left alone: it holds segment posting lists only, and a
    // mapped segment is immutable, so ingesting into the MemTable cannot make
    // any cached entry stale.
    return Expected<DocId, SearchError>(*id);
}

const std::vector<std::uint64_t>* SearchIndex::SegmentPostings(
    const std::string& term, std::vector<std::uint64_t>& uncached) const {
    if (!segment_) {
        return nullptr;
    }
    if (const std::vector<std::uint64_t>* cached = cache_.Get(term)) {
        return cached;
    }
    std::optional<std::vector<std::uint64_t>> postings = segment_->PostingsFor(term);
    if (!postings.has_value()) {
        return nullptr;
    }
    if (!cache_.Admits(term, postings->size())) {
        // Oversized-entry bypass: the list is larger than the whole ceiling.
        // Correctness must not depend on the cache, so hand back the decoded
        // list from the caller's own storage, held for this query only.
        uncached = std::move(*postings);
        return &uncached;
    }
    return cache_.Put(term, std::move(*postings));
}

double SearchIndex::CorpusAverageDocLength() const {
    if (segment_ != nullptr && segment_predates_memtable_) {
        // The MemTable holds only the documents ingested since Open, so the
        // segment's average, weighted by its document count, stands in for
        // the rest of the corpus.
        const auto segment_docs = static_cast<double>(segment_->doc_count());
        const double docs = segment_docs + static_cast<double>(memtable_.doc_count());
        if (docs == 0.0) {
            return 0.0;
        }
        return (segment_->avg_doc_len() * segment_docs +
                static_cast<double>(memtable_.total_token_count())) /
               docs;
    }
    // Otherwise the retained MemTable holds every document, flushed or not,
    // and its exact average beats the segment's Q16-rounded copy of the same
    // number: a flush must not move any score.
    return memtable_.avg_doc_length();
}

std::optional<StoredDocument> SearchIndex::LoadDocument(DocId id) const {
    if (id < segment_boundary_ && segment_) {
        const std::optional<SegmentDocument> record = segment_->DocumentAt(id);
        if (!record.has_value()) {
            return std::nullopt;
        }
        return record->document;
    }
    const std::optional<MemTableRecord> record = memtable_.RecordAt(id);
    if (!record.has_value()) {
        return std::nullopt;
    }
    // The MemTable stores bytes in its arena; materialize the owned strings
    // the StoredDocument contract expects.
    StoredDocument document;
    document.id = id;
    document.url = std::string(record->document.url);
    document.title = std::string(record->document.title);
    document.visited_at_ms = record->document.visited_at_ms;
    if (record->document.partition_tag.has_value()) {
        document.partition_tag = std::string(*record->document.partition_tag);
    }
    return document;
}

std::optional<DocumentStat> SearchIndex::BuildStat(DocId id, std::uint64_t query_now_ms) const {
    const std::optional<StoredDocument> document = LoadDocument(id);
    if (!document.has_value()) {
        return std::nullopt;
    }

    // Per-field term frequencies are derived here rather than read back from
    // the record. The segment's document store carries field lengths but not
    // frequencies, and Tokenize is deterministic over the stored bytes, so
    // re-tokenizing reproduces exactly what ingest indexed. The alternative --
    // widening the on-disk record -- is a store/ format change with its own
    // format_version bump, and belongs to that unit rather than this one.
    const std::vector<std::string> title_tokens = Tokenize(document->title);
    const std::vector<std::string> url_tokens = Tokenize(document->url);

    DocumentStat stat;
    stat.doc_id = id;
    stat.title_tf = CountTokens(title_tokens);
    stat.url_tf = CountTokens(url_tokens);
    stat.title_length = title_tokens.size();
    stat.url_length = url_tokens.size();
    stat.age_seconds = query_now_ms > document->visited_at_ms
                           ? static_cast<double>(query_now_ms - document->visited_at_ms) / 1000.0
                           : 0.0;
    return stat;
}

SearchResult SearchIndex::Query(const struct Query& query, std::uint64_t query_now_ms) const {
    SearchResult result;

    const std::vector<std::string> terms = Tokenize(query.text);
    if (terms.empty() || query.max_results == 0) {
        result.scanned_terms = terms.size();
        return result;
    }

    std::unordered_set<std::string> distinct(terms.begin(), terms.end());
    result.scanned_terms = distinct.size();

    // Candidates are a set, so a document present in both the segment and the
    // retained MemTable is collected once regardless. The range split at
    // segment_boundary_ is what keeps each posting scan bounded to the half
    // that owns those ids; the corpus-size computation below is where the
    // boundary is load-bearing for correctness, since counting the flushed
    // documents twice there would distort idf and therefore every score.
    std::unordered_set<DocId> candidates;
    std::vector<std::uint64_t> uncached;
    for (const std::string& term : distinct) {
        {
            // A cached list can be evicted by another Query's Put the moment
            // the lock drops, so it is read while the lock is still held.
            const std::lock_guard<std::mutex> lock(cache_mutex_);
            if (const std::vector<std::uint64_t>* postings = SegmentPostings(term, uncached)) {
                CollectRange(*postings, 0, segment_boundary_, candidates);
            }
        }
        CollectRange(memtable_.PostingsFor(term), segment_boundary_, memtable_.next_doc_id(),
                     candidates);
    }
    if (candidates.empty()) {
        return result;
    }

    std::vector<DocumentStat> corpus;
    corpus.reserve(candidates.size());
    for (const DocId id : candidates) {
        if (std::optional<DocumentStat> stat = BuildStat(id, query_now_ms)) {
            corpus.push_back(std::move(*stat));
        }
    }
    if (corpus.empty()) {
        return result;
    }

    // The MemTable is retained across Flush, so its doc_count() still includes
    // every flushed document. Adding it to the segment's count would double
    // the corpus size; only the ids minted since the boundary are new.
    CorpusStats stats;
    stats.num_docs = static_cast<std::size_t>(
        segment_ ? segment_->doc_count() + (memtable_.next_doc_id() - segment_boundary_)
                 : memtable_.doc_count());
    // BM25's avgdl is a property of the corpus, not of the candidates: an
    // average over the matching documents would let an unrelated query term
    // change every other term's length normalization.
    stats.avg_doc_length = CorpusAverageDocLength();

    const std::vector<std::string> query_terms(distinct.begin(), distinct.end());
    const std::vector<ScoredDoc> ranked =
        TopKDocuments(corpus, query_terms, stats, options_.weights, query.max_results);

    result.hits.reserve(ranked.size());
    for (const ScoredDoc& scored : ranked) {
        const std::optional<StoredDocument> document = LoadDocument(scored.doc_id);
        if (!document.has_value()) {
            continue;
        }
        SearchHit hit;
        hit.doc = scored.doc_id;
        hit.url = document->url;
        hit.title = document->title;
        hit.score = scored.score;
        result.hits.push_back(std::move(hit));
    }
    return result;
}

Expected<void, SearchError> SearchIndex::Flush() {
    if (segment_predates_memtable_ && segment_ != nullptr && segment_->doc_count() > 0) {
        return Expected<void, SearchError>::Error(MakeError(
            SearchErrorKind::kInvalidInput,
            "refusing to flush: the active segment holds documents this MemTable does not, and "
            "S0 has no segment merge, so writing would discard them"));
    }

    // Unmap the active segment before the writer renames over its file:
    // Windows refuses to replace a file that still has a live view. The
    // MemTable holds every document the segment does (the guard above ensures
    // it), so with the boundary at 0 the MemTable alone answers every id until
    // a segment is mapped again. The cache holds that segment's lists, so it
    // goes too.
    {
        const std::lock_guard<std::mutex> lock(cache_mutex_);
        cache_.Clear();
    }
    segment_.reset();
    segment_boundary_ = 0;

    const Expected<void, SearchError> written = WriteSegment(memtable_, options_.segment_path);
    auto opened = Segment::Open(options_.segment_path);
    if (!written.has_value()) {
        // The rename is atomic, so a failed write leaves the previous segment
        // file (if any) intact. Re-map it on a best-effort basis; results do
        // not depend on it either way.
        if (opened.has_value()) {
            segment_ = std::move(opened.value());
            segment_boundary_ = segment_->next_doc_id();
            segment_error_.reset();
        } else {
            segment_error_ = written.error();
        }
        return written;
    }

    if (!opened.has_value()) {
        // The segment we just wrote failed to re-open; it has been quarantined.
        // Keep answering from the MemTable rather than failing the call chain.
        segment_error_ = opened.error();
        return Expected<void, SearchError>::Error(opened.error());
    }

    segment_ = std::move(opened.value());
    segment_error_.reset();
    segment_boundary_ = segment_->next_doc_id();
    // The MemTable is retained and still holds every flushed document, so the
    // segment does not predate it and a later Flush stays safe.
    segment_predates_memtable_ = false;
    return Expected<void, SearchError>::Ok();
}

}  // namespace search
}  // namespace island
