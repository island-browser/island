#ifndef ISLAND_SEARCH_QUERY_PARSER_H_
#define ISLAND_SEARCH_QUERY_PARSER_H_

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace island {
namespace search {

// Bounds enforced by ParseQuery. Terms are also truncated to kMaxTermLength by
// the provided normalizer (the default normalizer does this).
inline constexpr std::size_t kMaxQueryUnits = 64;     // terms + phrases
inline constexpr std::size_t kMaxTermLength = 128;    // bytes per term/word
inline constexpr std::size_t kMaxPhraseLength = 256;  // total phrase word bytes
inline constexpr std::size_t kMaxPhraseWords = 32;    // words inside one phrase

// A single non-phrase query unit. A trailing `*` marks a prefix term.
struct QueryTerm {
    std::string text;
    bool is_prefix = false;
};

// A quoted phrase. `is_prefix` is set when the closing quote is followed by a
// trailing `*`.
struct QueryPhrase {
    std::vector<std::string> words;
    bool is_prefix = false;
};

// A parsed query: an ordered, non-empty spell-out of its units.
struct ParsedQuery {
    std::vector<QueryTerm> terms;
    std::vector<QueryPhrase> phrases;
};

// Typed parse error. Kind is kNone when the query parsed successfully.
struct QueryParseError {
    enum class Kind {
        kNone,
        kUnmatchedQuote,
        kEmptyPhrase,
        kEmptyTermAfterMarker,
        kMalformedMarker,
        kTooManyUnits,
        kTermTooLong,
        kPhraseTooLong,
    };
    Kind kind = Kind::kNone;
    std::size_t offset = 0;
};

// Result of a parse. `ok` is true exactly when `error.kind == kNone`.
struct QueryParseResult {
    bool ok = false;
    ParsedQuery query;
    QueryParseError error;
};

// Normalizer applied to each term and phrase word. Receives the raw extracted
// text and returns the canonical form. This is an injectable hook independent
// of any downstream (W1) implementation: callers may pass already-normalized
// strings by supplying the identity function, or plug in their own normalizer.
using TermNormalizer = std::function<std::string(std::string_view)>;

// Default normalizer: lowercases ASCII and truncates to kMaxTermLength. Never
// throws.
std::string DefaultNormalize(std::string_view token);

// Parses whitespace-separated terms and quoted phrases with an optional
// trailing `*` prefix marker. There is no boolean grammar. Uses
// DefaultNormalize. Never throws.
QueryParseResult ParseQuery(std::string_view input);

// Parses with an injectable normalizer. An empty function falls back to
// DefaultNormalize. Never throws.
QueryParseResult ParseQuery(std::string_view input, const TermNormalizer& normalizer);

}  // namespace search
}  // namespace island

#endif  // ISLAND_SEARCH_QUERY_PARSER_H_