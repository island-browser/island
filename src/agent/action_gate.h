#ifndef ISLAND_AGENT_ACTION_GATE_H_
#define ISLAND_AGENT_ACTION_GATE_H_

// The action gate: decides, before an agent's page action runs, whether it is free, needs the
// person's yes (kConfirm) or is never allowed (kBlocked). Pure logic over facts the tool layer
// gathered from the page; CEF-free. Rules and word list: the agent-actions design spec, §2.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace island::agent {

enum class ActionClass : std::uint8_t { kFree, kConfirm, kBlocked };

enum class GateRule : std::uint8_t {
    kNone,
    kPassword,
    kCardField,
    kSubmit,
    kConsequentialWord,
    kEnterInForm,
    kScript,
    kForeignNavigation,
    kClosePersonTab,
};

// What the tool layer knows about the element a page action targets (or the focused element for
// page_press_key). Gathered by an evaluation in the page; absent when that failed.
struct ElementFacts {
    std::string tag, type, autocomplete, name, id, aria_label, text;
    bool in_form = false;
    bool is_submit = false;
    std::string page_origin;  // "https://shop.example.com"
};

struct GateInput {
    std::string tool;                   // "page_click", "page_type", ...
    std::optional<ElementFacts> facts;  // target or focused element, when gathered
    std::string key;                    // page_press_key: the key name
    std::string page_url;               // the tab's current URL
    std::string target_url;             // browser_navigate: the new URL
    bool tab_opened_by_agent = false;
};

struct GateDecision {
    ActionClass action = ActionClass::kFree;
    GateRule rule = GateRule::kNone;
    std::string summary;          // "Click \"Place order\" on shop.example.com"
    std::string host;             // page host without "www."
    bool trusted_allowed = true;  // false for card fields
};

// Lowercase host of a URL without scheme, userinfo, port, trailing dot or leading "www.".
// Returns "" when the input has no "://".
[[nodiscard]] std::string HostOf(std::string_view url);

// True when host equals a trusted site or is a subdomain of one. Look-alikes do not match.
[[nodiscard]] bool HostMatchesTrusted(std::string_view host,
                                      const std::vector<std::string>& trusted);

// True when text contains a consequential word or phrase (§2.2) as whole words, case-insensitive.
[[nodiscard]] bool IsConsequentialText(std::string_view text);

[[nodiscard]] GateDecision Classify(const GateInput& input,
                                    const std::vector<std::string>& trusted_sites);

}  // namespace island::agent

#endif  // ISLAND_AGENT_ACTION_GATE_H_
