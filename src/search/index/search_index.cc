#include "search/index/search_index.h"

#include <algorithm>
#include <cctype>
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

char LowerAscii(char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

bool HasSchemeCaseInsensitive(std::string_view url, std::string_view scheme) {
    if (url.size() < scheme.size()) {
        return false;
    }
    for (std::size_t i = 0; i < scheme.size(); ++i) {
        if (LowerAscii(url[i]) != scheme[i]) {
            return false;
        }
    }
    return true;
}

// True when the authority component carries userinfo. The authority runs from
// after "://" to the first '/', '?' or '#'; an '@' inside it means
// user[:pass]@host. Anything later in the URL (a query parameter holding an
// address, say) is not userinfo and must not trip the refusal.
bool HasCredentials(std::string_view url) {
    const std::size_t scheme_end = url.find("://");
    if (scheme_end == std::string_view::npos) {
        return false;
    }
    const std::size_t authority_begin = scheme_end + 3;
    std::size_t authority_end = url.size();
    for (std::size_t i = authority_begin; i < url.size(); ++i) {
        if (url[i] == '/' || url[i] == '?' || url[i] == '#') {
            authority_end = i;
            break;
        }
    }
    return url.substr(authority_begin, authority_end - authority_begin).find('@') !=
           std::string_view::npos;
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
    return HasSchemeCaseInsensitive(url, "data:") || HasCredentials(url);
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
        return Expected<DocId, SearchError>::Error(MakeError(
            SearchErrorKind::kRefusedForPrivacy,
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

    const DocId id = memtable_.AddDocument(std::move(document), title_tokens, url_tokens);
    // A newly ingested term's segment posting list is unchanged, but a term
    // that now resolves differently must not be served from a stale entry.
    cache_.Clear();
    return Expected<DocId, SearchError>(id);
}

const std::vector<std::uint64_t>* SearchIndex::SegmentPostings(const std::string& term) const {
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
    if (!cache_.Put(term, std::move(*postings))) {
        // Oversized-entry bypass: the list is larger than the whole ceiling.
        // Correctness must not depend on the cache, so decode it again and
        // hand back an uncached copy held for this query only.
        std::optional<std::vector<std::uint64_t>> again = segment_->PostingsFor(term);
        if (!again.has_value()) {
            return nullptr;
        }
        uncached_ = std::move(*again);
        return &uncached_;
    }
    return cache_.Get(term);
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
    if (!record.has_value() || record->document == nullptr) {
        return std::nullopt;
    }
    return *record->document;
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
    for (const std::string& term : distinct) {
        if (const std::vector<std::uint64_t>* postings = SegmentPostings(term)) {
            CollectRange(*postings, 0, segment_boundary_, candidates);
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
    double total_length = 0.0;
    for (const DocumentStat& stat : corpus) {
        total_length += static_cast<double>(stat.title_length + stat.url_length);
    }
    stats.avg_doc_length =
        corpus.empty() ? 0.0 : total_length / static_cast<double>(corpus.size());

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

    const Expected<void, SearchError> written = WriteSegment(memtable_, options_.segment_path);
    if (!written.has_value()) {
        return written;
    }

    auto opened = Segment::Open(options_.segment_path);
    if (!opened.has_value()) {
        // The segment we just wrote failed to re-open; it has been quarantined.
        // Keep answering from the MemTable rather than failing the call chain.
        segment_.reset();
        segment_error_ = opened.error();
        segment_boundary_ = 0;
        cache_.Clear();
        return Expected<void, SearchError>::Error(opened.error());
    }

    segment_ = std::move(opened.value());
    segment_error_.reset();
    segment_boundary_ = segment_->next_doc_id();
    // The MemTable is retained and still holds every flushed document, so the
    // segment does not predate it and a later Flush stays safe.
    segment_predates_memtable_ = false;
    cache_.Clear();
    return Expected<void, SearchError>::Ok();
}

}  // namespace search
}  // namespace island
