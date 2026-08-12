#include "search/tokenizer.h"

#include <cstdint>

namespace island {
namespace search {
namespace {

// Returns the ASCII lowercase form of alphanumeric ASCII bytes, otherwise the
// input byte unchanged (covers digits, punctuation, and all non-ASCII bytes).
constexpr char FoldAscii(char c) {
    const std::uint8_t u = static_cast<std::uint8_t>(c);
    const bool is_upper_ascii = u >= 'A' && u <= 'Z';
    return is_upper_ascii ? static_cast<char>(u + ('a' - 'A')) : c;
}

// True for bytes that are part of a token: ASCII alphanumerics and every byte
// with the high bit set (valid UTF-8 continuation/multibyte lead, or an
// opaque malformed byte). Everything else (ASCII whitespace/punctuation) is a
// delimiter.
constexpr bool IsTokenByte(char c) {
    const std::uint8_t u = static_cast<std::uint8_t>(c);
    const bool is_ascii_alnum =
        (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9');
    return is_ascii_alnum || u >= 0x80u;
}

void AppendTruncatedTo(std::string* out, std::string_view token) {
    if (token.size() <= kMaxTokenLength) {
        *out = token;
    } else {
        *out = token.substr(0, kMaxTokenLength);
    }
}

}  // namespace

std::vector<std::string> Tokenize(std::string_view input) {
    std::vector<std::string> tokens;
    std::string current;
    current.reserve(kMaxTokenLength);

    for (const char c : input) {
        if (IsTokenByte(c)) {
            if (current.size() < kMaxTokenLength) {
                current.push_back(FoldAscii(c));
            }
            continue;
        }
        if (!current.empty()) {
            tokens.push_back(std::move(current));
            current.clear();
        }
    }
    if (!current.empty()) {
        tokens.push_back(std::move(current));
    }
    return tokens;
}

std::string NormalizeToken(std::string_view token) {
    std::string out;
    AppendTruncatedTo(&out, token);
    for (char& c : out) {
        c = FoldAscii(c);
    }
    return out;
}

}  // namespace search
}  // namespace island