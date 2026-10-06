#include "agent_navigation.h"

#include "search_provider.h"

namespace island {

namespace {

std::string_view Trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\n' ||
                             text.front() == '\r')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\n' ||
                             text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

bool LooksLikeHost(std::string_view text) {
    if (text.find_first_of(" \t") != std::string_view::npos) return false;
    if (text.find("://") != std::string_view::npos) return false;
    const std::string_view host = text.substr(0, text.find_first_of("/?#"));
    const std::size_t dot = host.find('.');
    if (host == "localhost" || host.substr(0, 10) == "localhost:") return true;
    return dot != std::string_view::npos && dot > 0 && dot + 1 < host.size();
}

}  // namespace

std::optional<std::string> ResolveAgentNavigation(std::string_view input,
                                                  const AddressValidator& validate) {
    const std::string_view text = Trim(input);
    if (text.empty()) return std::nullopt;
    const ValidatedAddress direct = validate(text);
    if (direct.is_valid()) return direct.url;
    if (LooksLikeHost(text)) {
        const std::string prefix = text.substr(0, 9) == "localhost" ? "http://" : "https://";
        const ValidatedAddress with_scheme = validate(prefix + std::string(text));
        if (with_scheme.is_valid()) return with_scheme.url;
    }
    const SearchProvider* google = FindSearchProvider(SearchProviderId::kGoogle);
    if (google == nullptr) return std::nullopt;
    const ValidatedAddress search = validate(ComposeSearchUrl(*google, text));
    if (search.is_valid()) return search.url;
    return std::nullopt;
}

}  // namespace island
