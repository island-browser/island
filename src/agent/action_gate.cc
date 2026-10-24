#include "action_gate.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace island::agent {
namespace {

// §2.2. English and Russian words and phrases, lowercase; matched as whole words.
constexpr std::string_view kConsequentialWords[] = {
    "submit",    "send",        "post",         "publish",     "buy",           "purchase",
    "order",     "checkout",    "pay",          "place order", "confirm",       "delete",
    "remove",    "unsubscribe", "transfer",     "book",        "reserve",       "sign up",
    "subscribe", "отправить",   "опубликовать", "купить",      "оплатить",      "заказать",
    "оформить",  "подтвердить", "удалить",      "перевести",   "забронировать", "подписаться",
};

constexpr std::size_t kClipChars = 40;
constexpr std::string_view kEllipsis = "\xE2\x80\xA6";  // U+2026
constexpr char32_t kReplacement = 0xFFFD;

bool IsAsciiUpper(char c) { return c >= 'A' && c <= 'Z'; }

bool IsAsciiAlpha(char c) { return (c >= 'a' && c <= 'z') || IsAsciiUpper(c); }

bool IsAsciiDigit(char c) { return c >= '0' && c <= '9'; }

std::string Lowercase(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (IsAsciiUpper(c)) c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

// Length in bytes of the whitespace character that starts at text[i], or 0 when none does. Covers
// ASCII whitespace, U+00A0, U+2000..U+200A, U+2028, U+2029, U+202F, U+205F and U+3000.
std::size_t SpaceLengthAt(std::string_view text, std::size_t i) {
    const auto c = static_cast<unsigned char>(text[i]);
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v') return 1;
    if (c == 0xC2 && i + 1 < text.size() && text[i + 1] == '\xA0') return 2;
    if (i + 2 >= text.size()) return 0;
    const auto b1 = static_cast<unsigned char>(text[i + 1]);
    const auto b2 = static_cast<unsigned char>(text[i + 2]);
    if (c == 0xE2 && b1 == 0x80 &&
        ((b2 >= 0x80 && b2 <= 0x8A) || b2 == 0xA8 || b2 == 0xA9 || b2 == 0xAF)) {
        return 3;
    }
    if (c == 0xE2 && b1 == 0x81 && b2 == 0x9F) return 3;
    if (c == 0xE3 && b1 == 0x80 && b2 == 0x80) return 3;
    return 0;
}

// Replaces every run of whitespace with one ASCII space and trims both ends. Case is kept.
std::string CollapseSpaces(std::string_view text) {
    std::string out;
    bool pending_space = false;
    for (std::size_t i = 0; i < text.size();) {
        const std::size_t space = SpaceLengthAt(text, i);
        if (space > 0) {
            pending_space = !out.empty();
            i += space;
            continue;
        }
        if (pending_space) {
            out.push_back(' ');
            pending_space = false;
        }
        out.push_back(text[i]);
        ++i;
    }
    return out;
}

// Lowercases ASCII and basic Cyrillic (U+0401, U+0410..U+042F). Used only for matching.
std::string FoldCase(std::string_view text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        const auto byte = static_cast<unsigned char>(c);
        if (byte == 0xD0 && i + 1 < text.size()) {
            const auto next = static_cast<unsigned char>(text[i + 1]);
            if (next >= 0x90 && next <= 0x9F) {  // U+0410..U+041F -> U+0430..U+043F
                out += '\xD0';
                out += static_cast<char>(next + 0x20);
                ++i;
                continue;
            }
            if (next >= 0xA0 && next <= 0xAF) {  // U+0420..U+042F -> U+0440..U+044F
                out += '\xD1';
                out += static_cast<char>(next - 0x20);
                ++i;
                continue;
            }
            if (next == 0x81) {  // U+0401 -> U+0451
                out += '\xD1';
                out += static_cast<char>(0x91);
                ++i;
                continue;
            }
        }
        out += IsAsciiUpper(c) ? static_cast<char>(c - 'A' + 'a') : c;
    }
    return out;
}

std::string NormalizeForMatch(std::string_view text) { return FoldCase(CollapseSpaces(text)); }

// Length of the UTF-8 sequence that starts with this lead byte; 0 for a continuation byte.
std::size_t Utf8Length(char lead) {
    const auto byte = static_cast<unsigned char>(lead);
    if (byte < 0x80) return 1;
    if ((byte >> 5) == 0x6) return 2;
    if ((byte >> 4) == 0xE) return 3;
    if ((byte >> 3) == 0x1E) return 4;
    return 0;
}

// Decodes the character that starts at text[i]; kReplacement when the bytes are malformed.
char32_t DecodeAt(std::string_view text, std::size_t i) {
    const std::size_t length = Utf8Length(text[i]);
    if (length == 0 || i + length > text.size()) return kReplacement;
    const auto lead = static_cast<unsigned char>(text[i]);
    char32_t cp = static_cast<char32_t>(length == 1 ? lead : lead & (0xFF >> (length + 1)));
    for (std::size_t k = 1; k < length; ++k) {
        const auto cont = static_cast<unsigned char>(text[i + k]);
        if ((cont & 0xC0) != 0x80) return kReplacement;
        cp = static_cast<char32_t>((cp << 6) | (cont & 0x3F));
    }
    return cp;
}

// Decodes the character that ends just before text[end]; kReplacement when there is none.
char32_t DecodeBefore(std::string_view text, std::size_t end) {
    std::size_t start = end - 1;
    while (start > 0 && end - start < 4 &&
           (static_cast<unsigned char>(text[start]) & 0xC0) == 0x80) {
        --start;
    }
    if (Utf8Length(text[start]) != end - start) return kReplacement;
    return DecodeAt(text, start);
}

// Letters and digits make up a word. Spaces, punctuation and symbols (NBSP, «», —, …, $) end one.
bool IsWordCharacter(char32_t cp) {
    if (cp < 0x80) {
        const char c = static_cast<char>(cp);
        return IsAsciiAlpha(c) || IsAsciiDigit(c);
    }
    if (cp >= 0xC0 && cp <= 0x24F) return cp != 0xD7 && cp != 0xF7;  // Latin-1 and Latin Extended
    return cp >= 0x400 && cp <= 0x4FF;                               // Cyrillic
}

bool ContainsWord(std::string_view text, std::string_view word) {
    for (std::size_t pos = text.find(word); pos != std::string_view::npos;
         pos = text.find(word, pos + 1)) {
        const std::size_t end = pos + word.size();
        const bool left_ok = pos == 0 || !IsWordCharacter(DecodeBefore(text, pos));
        const bool right_ok = end == text.size() || !IsWordCharacter(DecodeAt(text, end));
        if (left_ok && right_ok) return true;
    }
    return false;
}

// Keeps at most kClipChars code points, counting UTF-8 characters rather than bytes.
std::string Clip(const std::string& text) {
    std::size_t chars = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if ((static_cast<unsigned char>(text[i]) & 0xC0) == 0x80) continue;
        if (chars == kClipChars) return text.substr(0, i) + std::string(kEllipsis);
        ++chars;
    }
    return text;
}

bool IsPasswordField(const ElementFacts& facts) {
    const std::string type = Lowercase(facts.type);
    const std::string autocomplete = Lowercase(facts.autocomplete);
    return type == "password" || autocomplete.find("password") != std::string::npos ||
           autocomplete.find("one-time-code") != std::string::npos;
}

bool IsCardField(const ElementFacts& facts) {
    const std::string autocomplete = Lowercase(facts.autocomplete);
    if (autocomplete.rfind("cc-", 0) == 0) return true;
    const std::string names = Lowercase(facts.name + " " + facts.id);
    for (std::string_view needle : {"card", "cvv", "cvc", "csc", "ccnum", "cc-num", "expir",
                                    "exp-date", "exp_date", "security-code"}) {
        if (names.find(needle) != std::string::npos) return true;
    }
    return false;
}

std::string ClickSummary(const ElementFacts& facts, const std::string& host) {
    std::string label = CollapseSpaces(facts.text);
    if (label.empty()) label = CollapseSpaces(facts.aria_label);
    if (label.empty()) return "Click an element on " + host;
    return "Click \"" + Clip(label) + "\" on " + host;
}

// The parts of an absolute URL that matter to the gate.
struct UrlParts {
    std::string scheme;  // lowercase
    std::string host;    // lowercase, no trailing dot; an IPv6 host keeps its brackets
    std::string port;    // as written; "" when absent
};

// Splits an absolute URL the way a browser reads its authority: tabs and newlines are dropped,
// "\" ends the authority like "/", and the userinfo is everything before the last "@". Returns
// false when there is no "scheme://" prefix.
bool SplitUrl(std::string_view url, UrlParts& parts) {
    std::string clean;
    for (const char c : url) {
        if (c != '\t' && c != '\n' && c != '\r') clean.push_back(c);
    }
    const std::string_view text = clean;
    const std::size_t sep = text.find("://");
    if (sep == std::string_view::npos || sep == 0 || !IsAsciiAlpha(text[0])) return false;
    for (std::size_t i = 0; i < sep; ++i) {
        const char c = text[i];
        if (!IsAsciiAlpha(c) && !IsAsciiDigit(c) && c != '+' && c != '-' && c != '.') return false;
    }
    parts.scheme = Lowercase(text.substr(0, sep));

    std::string_view authority = text.substr(sep + 3);
    authority = authority.substr(0, authority.find_first_of("/?#\\"));
    const std::size_t at = authority.rfind('@');
    if (at != std::string_view::npos) authority = authority.substr(at + 1);

    std::string_view host = authority;
    parts.port.clear();
    if (authority.starts_with('[')) {
        const std::size_t close = authority.find(']');
        if (close == std::string_view::npos) return false;
        host = authority.substr(0, close + 1);
        if (authority.size() > close + 1 && authority[close + 1] == ':') {
            parts.port = std::string(authority.substr(close + 2));
        }
    } else {
        const std::size_t colon = authority.rfind(':');
        if (colon != std::string_view::npos) {
            host = authority.substr(0, colon);
            parts.port = std::string(authority.substr(colon + 1));
        }
    }
    parts.host = Lowercase(host);
    while (!parts.host.empty() && parts.host.back() == '.') parts.host.pop_back();
    return true;
}

// scheme://host:port with the default port spelled out. "" when the URL has no host, so that an
// unknown page or target never equals another one.
std::string OriginOf(std::string_view url) {
    UrlParts parts;
    if (!SplitUrl(url, parts) || parts.host.empty()) return {};
    std::string port = parts.port;
    if (port.empty()) {
        if (parts.scheme == "https") port = "443";
        if (parts.scheme == "http") port = "80";
    }
    return parts.scheme + "://" + parts.host + ":" + port;
}

}  // namespace

std::string HostOf(std::string_view url) {
    UrlParts parts;
    if (!SplitUrl(url, parts)) return {};
    if (parts.host.starts_with("www.")) return parts.host.substr(4);
    return parts.host;
}

bool HostMatchesTrusted(std::string_view host, const std::vector<std::string>& trusted) {
    if (host.empty()) return false;
    for (const std::string& site : trusted) {
        const std::string candidate = Lowercase(site);
        if (candidate.empty()) continue;
        if (host == candidate) return true;
        if (host.size() > candidate.size() && host.ends_with(candidate) &&
            host[host.size() - candidate.size() - 1] == '.') {
            return true;
        }
    }
    return false;
}

bool IsConsequentialText(std::string_view text) {
    const std::string normalized = NormalizeForMatch(text);
    for (std::string_view word : kConsequentialWords) {
        if (ContainsWord(normalized, word)) return true;
    }
    return false;
}

GateDecision Classify(const GateInput& input, const std::vector<std::string>& trusted_sites) {
    GateDecision decision;
    decision.host = HostOf(input.page_url);
    // Pages without a host (data:, about:blank, sandboxed) read as "this page" in the summaries.
    const std::string host = decision.host.empty() ? "this page" : decision.host;
    const bool facts_present = input.facts.has_value();
    const bool typing = input.tool == "page_type";
    const bool keying = input.tool == "page_press_key";

    if (input.tool == "browser_add_bookmark" || input.tool == "browser_remove_bookmark" ||
        input.tool == "browser_add_favorite" || input.tool == "browser_remove_favorite") {
        decision.action = ActionClass::kConfirm;
        decision.rule = GateRule::kBookmarkMutation;
        decision.host = HostOf(input.bookmark_url);
        const std::string bookmark_host = decision.host.empty() ? "this page" : decision.host;
        const std::string title = CollapseSpaces(input.bookmark_title).empty()
                                      ? input.bookmark_url
                                      : Clip(CollapseSpaces(input.bookmark_title));
        if (input.tool == "browser_add_bookmark") {
            decision.summary = "Add bookmark \"" + title + "\" on " + bookmark_host +
                               " to folder \"" + CollapseSpaces(input.bookmark_folder_path) + "\"";
        } else if (input.tool == "browser_remove_bookmark") {
            decision.summary = "Remove bookmark \"" + title + "\" on " + bookmark_host +
                               " from folder \"" + CollapseSpaces(input.bookmark_folder_path) +
                               "\"";
        } else if (input.tool == "browser_add_favorite") {
            decision.summary =
                "Add \"" + title + "\" on " + bookmark_host + " to " + input.favorite_scope;
        } else {
            decision.summary =
                "Remove \"" + title + "\" on " + bookmark_host + " from " + input.favorite_scope;
        }
        decision.trusted_allowed = false;
        decision.task_grant_allowed = true;
        // Keep an Allow-for-task grant narrowly bound to this persistent operation and object.
        decision.scope = input.tool + "|" + input.bookmark_url + "|" + input.bookmark_folder_path +
                         "|" + input.favorite_scope;
        return decision;
    }

    // Rule 1 covers typing and key presses only, so the agent can still focus a password field.
    if ((typing || keying) && facts_present && IsPasswordField(*input.facts)) {
        decision.action = ActionClass::kBlocked;
        decision.rule = GateRule::kPassword;
        decision.summary = "Use a password or sign-in code field on " + host;
        decision.trusted_allowed = false;
        decision.task_grant_allowed = false;
        return decision;
    }
    // Rule 2 covers typing into a card field, on every site.
    if (typing && facts_present && IsCardField(*input.facts)) {
        decision.action = ActionClass::kConfirm;
        decision.rule = GateRule::kCardField;
        decision.summary = "Type into the card number field on " + host;
        decision.trusted_allowed = false;
        decision.task_grant_allowed = false;
        return decision;
    }
    // Without the element, the gate cannot tell what the action does. It asks on every site.
    // (A missing-facts page_click is handled below as kSubmit, and Enter as kEnterInForm.)
    if (typing && !facts_present) {
        decision.action = ActionClass::kConfirm;
        decision.rule = GateRule::kUnclassified;
        decision.summary = "Type into an unidentified field on " + host;
        decision.trusted_allowed = false;
        decision.task_grant_allowed = false;
        return decision;
    }
    if (keying && !facts_present && input.key != "Enter") {
        decision.action = ActionClass::kConfirm;
        decision.rule = GateRule::kUnclassified;
        decision.summary = "Press a key on an unidentified element on " + host;
        decision.trusted_allowed = false;
        decision.task_grant_allowed = false;
        return decision;
    }

    // Rules 3-7 (kSubmit..kClosePersonTab).
    GateRule rule = GateRule::kNone;
    std::string summary;
    if (input.tool == "page_click") {
        if (!facts_present) {
            rule = GateRule::kSubmit;
            summary = "Click an element on " + host;
        } else if (input.facts->is_submit) {
            rule = GateRule::kSubmit;
            summary = ClickSummary(*input.facts, host);
        } else if (IsConsequentialText(input.facts->text) ||
                   IsConsequentialText(input.facts->aria_label)) {
            rule = GateRule::kConsequentialWord;
            summary = ClickSummary(*input.facts, host);
        }
    } else if (typing && input.submit) {
        // page_type with submit presses Enter after the text, so it is judged like page_press_key
        // Enter on the same element. Without facts the unclassified rule above already asked.
        if (facts_present && input.facts->in_form) {
            rule = GateRule::kEnterInForm;
            summary = "Press Enter in a form on " + host;
        }
    } else if (keying) {
        if (input.key == "Enter" && (!facts_present || input.facts->in_form)) {
            rule = GateRule::kEnterInForm;
            summary = "Press Enter in a form on " + host;
        }
    } else if (input.tool == "page_evaluate") {
        rule = GateRule::kScript;
        summary = "Run a script on " + host;
    } else if (input.tool == "browser_navigate" && !input.tab_opened_by_agent) {
        // Origin, not host: a change of scheme or port is a different origin too. An unknown
        // origin on either side counts as different.
        const std::string target_origin = OriginOf(input.target_url);
        if (target_origin.empty() || target_origin != OriginOf(input.page_url)) {
            rule = GateRule::kForeignNavigation;
            const std::string target = HostOf(input.target_url);
            summary = target.empty() ? "Open the new page in your tab"
                                     : "Open " + target + " in your tab";
        }
    } else if (input.tool == "browser_close_tab" && !input.tab_opened_by_agent) {
        rule = GateRule::kClosePersonTab;
        summary = "Close your tab on " + host;
    }

    if (rule == GateRule::kNone) return decision;  // kFree
    decision.rule = rule;
    decision.summary = summary;
    decision.action = HostMatchesTrusted(decision.host, trusted_sites) ? ActionClass::kFree
                                                                       : ActionClass::kConfirm;
    // A hostless page cannot be trusted or granted: one entry would cover every such page.
    decision.scope = OriginOf(input.page_url);
    if (rule == GateRule::kForeignNavigation) decision.scope += "->" + OriginOf(input.target_url);
    if (decision.host.empty() || decision.scope.empty()) {
        decision.trusted_allowed = false;
        decision.task_grant_allowed = false;
    }
    return decision;
}

}  // namespace island::agent
