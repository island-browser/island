#include "search_provider.h"

#include <array>

namespace island {
namespace {

constexpr std::string_view kUppercaseHexDigits = "0123456789ABCDEF";

constexpr std::array<SearchProvider, 5> kSearchProviders = {{
    {SearchProviderId::kChatGpt, "ChatGPT", "https://chatgpt.com/"},
    {SearchProviderId::kPerplexity, "Perplexity", "https://www.perplexity.ai/search"},
    {SearchProviderId::kClaude, "Claude", "https://claude.ai/new"},
    {SearchProviderId::kGemini, "Gemini", "https://gemini.google.com/app"},
    {SearchProviderId::kGoogle, "Google", "https://www.google.com/search"},
}};

[[nodiscard]] constexpr bool IsUnreserved(unsigned char byte) noexcept {
    return (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
           (byte >= '0' && byte <= '9') || byte == '-' || byte == '.' || byte == '_' ||
           byte == '~';
}

[[nodiscard]] constexpr bool IsAsciiWhitespace(unsigned char byte) noexcept {
    return byte == ' ' || byte == '\t' || byte == '\n' || byte == '\v' || byte == '\f' ||
           byte == '\r';
}

}  // namespace

std::string PercentEncodeQuery(std::string_view query) {
    std::string encoded;
    encoded.reserve(query.size());
    for (const char character : query) {
        const auto byte = static_cast<unsigned char>(character);
        if (IsUnreserved(byte)) {
            encoded.push_back(static_cast<char>(byte));
            continue;
        }
        encoded.push_back('%');
        encoded.push_back(kUppercaseHexDigits[byte >> 4U]);
        encoded.push_back(kUppercaseHexDigits[byte & 0x0FU]);
    }
    return encoded;
}

std::span<const SearchProvider> SearchProviders() noexcept {
    return std::span<const SearchProvider>(kSearchProviders.data(), kSearchProviders.size());
}

const SearchProvider* FindSearchProvider(SearchProviderId id) noexcept {
    for (const SearchProvider& provider : kSearchProviders) {
        if (provider.id == id) {
            return &provider;
        }
    }
    return nullptr;
}

std::string ComposeSearchUrl(const SearchProvider& provider, std::string_view query) {
    std::string url(provider.base_url);
    url.append("?q=");
    url.append(PercentEncodeQuery(query));
    return url;
}

bool IsSubmittableQuery(std::string_view query) noexcept {
    for (const char character : query) {
        if (!IsAsciiWhitespace(static_cast<unsigned char>(character))) {
            return true;
        }
    }
    return false;
}

}  // namespace island
