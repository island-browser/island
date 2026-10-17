#include "action_gate.h"

#include <cstddef>
#include <initializer_list>
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

bool IsAsciiSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

bool IsAsciiUpper(char c) { return c >= 'A' && c <= 'Z'; }

bool IsAsciiLower(char c) { return c >= 'a' && c <= 'z'; }

bool IsAsciiDigit(char c) { return c >= '0' && c <= '9'; }

// A word byte is an ASCII letter or digit, or any byte of a multi-byte (non-ASCII) character, so
// Cyrillic words are never split at a byte boundary.
bool IsWordByte(char c) {
    return IsAsciiLower(c) || IsAsciiUpper(c) || IsAsciiDigit(c) ||
           static_cast<unsigned char>(c) >= 0x80;
}

std::string Lowercase(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (IsAsciiUpper(c)) c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

std::string Trim(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && IsAsciiSpace(text[begin])) ++begin;
    while (end > begin && IsAsciiSpace(text[end - 1])) --end;
    return std::string(text.substr(begin, end - begin));
}

// Lowercases ASCII and basic Cyrillic (U+0401, U+0410..U+042F), collapses whitespace runs into a
// single space, and trims. Used only for matching, never for display.
std::string NormalizeForMatch(std::string_view text) {
    std::string out;
    bool pending_space = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (IsAsciiSpace(c)) {
            pending_space = !out.empty();
            continue;
        }
        if (pending_space) {
            out.push_back(' ');
            pending_space = false;
        }
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

bool ContainsWord(std::string_view text, std::string_view word) {
    for (std::size_t pos = text.find(word); pos != std::string_view::npos;
         pos = text.find(word, pos + 1)) {
        const bool left_ok = pos == 0 || !IsWordByte(text[pos - 1]);
        const std::size_t end = pos + word.size();
        const bool right_ok = end == text.size() || !IsWordByte(text[end]);
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
    for (std::string_view needle :
         {"card", "cvv", "cvc", "csc", "ccnum", "expir", "exp-date", "exp_date", "security-code"}) {
        if (names.find(needle) != std::string::npos) return true;
    }
    return false;
}

std::string ClickSummary(const ElementFacts& facts, const std::string& host) {
    const std::string label = Trim(facts.text.empty() ? facts.aria_label : facts.text);
    return "Click \"" + Clip(label) + "\" on " + host;
}

}  // namespace

std::string HostOf(std::string_view url) {
    const std::size_t scheme = url.find("://");
    if (scheme == std::string_view::npos) return {};
    std::string_view rest = url.substr(scheme + 3);
    std::string_view authority = rest.substr(0, rest.find_first_of("/?#"));
    const std::size_t at = authority.rfind('@');
    if (at != std::string_view::npos) authority = authority.substr(at + 1);
    if (!authority.empty() && authority.front() != '[') {
        const std::size_t colon = authority.rfind(':');
        if (colon != std::string_view::npos) authority = authority.substr(0, colon);
    }
    std::string host = Lowercase(authority);
    while (!host.empty() && host.back() == '.') host.pop_back();
    if (host.rfind("www.", 0) == 0) host.erase(0, 4);
    return host;
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
    const std::string& host = decision.host;
    const bool facts_present = input.facts.has_value();

    // Rules 1 and 2 hold on every site, trusted or not.
    if (facts_present && IsPasswordField(*input.facts)) {
        decision.action = ActionClass::kBlocked;
        decision.rule = GateRule::kPassword;
        decision.summary = "Use a password or sign-in code field on " + host;
        return decision;
    }
    if (facts_present && IsCardField(*input.facts)) {
        decision.action = ActionClass::kConfirm;
        decision.rule = GateRule::kCardField;
        decision.summary = "Type into the card number field on " + host;
        decision.trusted_allowed = false;
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
    } else if (input.tool == "page_press_key") {
        if (input.key == "Enter" && (!facts_present || input.facts->in_form)) {
            rule = GateRule::kEnterInForm;
            summary = "Press Enter in a form on " + host;
        }
    } else if (input.tool == "page_evaluate") {
        rule = GateRule::kScript;
        summary = "Run a script on " + host;
    } else if (input.tool == "browser_navigate" && !input.tab_opened_by_agent) {
        const std::string target = HostOf(input.target_url);
        if (target != host) {
            rule = GateRule::kForeignNavigation;
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
    decision.action =
        HostMatchesTrusted(host, trusted_sites) ? ActionClass::kFree : ActionClass::kConfirm;
    return decision;
}

}  // namespace island::agent
