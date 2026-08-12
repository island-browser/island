#ifndef ISLAND_SEARCH_TOKENIZER_H_
#define ISLAND_SEARCH_TOKENIZER_H_

#include <string>
#include <string_view>
#include <vector>

namespace island {
namespace search {

// Maximum number of bytes a single token may hold. Longer tokens are truncated
// to this bound rather than dropped.
inline constexpr std::size_t kMaxTokenLength = 128;

// Deterministic, Unicode-aware-enough tokenizer for url + title text.
//
//   * ASCII letters are lowercased; non-ASCII bytes (valid UTF-8 or not) pass
//     through unchanged.
//   * ASCII alphanumerics and any non-ASCII byte are token characters.
//   * Every other ASCII byte (whitespace and punctuation) is a delimiter.
//   * Runs of delimiters collapse to a single break; empty tokens are dropped.
//   * Tokens are truncated to kMaxTokenLength.
//   * Malformed UTF-8 never throws; bytes are handled as opaque values.
//
// Never throws. Allocates once per returned token.
std::vector<std::string> Tokenize(std::string_view input);

// Lowercases ASCII letters in a single token and truncates it to
// kMaxTokenLength. Non-ASCII bytes pass through unchanged. Never throws.
std::string NormalizeToken(std::string_view token);

}  // namespace search
}  // namespace island

#endif  // ISLAND_SEARCH_TOKENIZER_H_