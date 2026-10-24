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
    kUnclassified,  // missing element facts for page_type / page_press_key; always asks
    kBookmarkMutation,
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
    bool submit = false;                // page_type with submit: Enter follows the typed text
    std::string page_url;               // the tab's current URL
    // browser_navigate: the URL the navigation will actually load, i.e. after the tool layer has
    // resolved and validated the agent's argument (search text becomes a search URL). Never the
    // raw argument.
    std::string target_url;
    bool tab_opened_by_agent = false;
    // Persistent bookmark-library action. These values are supplied by the tool layer, never by a
    // page, and let the confirmation card identify the exact local-state change.
    std::string bookmark_title;
    std::uint64_t bookmark_id = 0;
    std::string bookmark_url;
    std::string bookmark_folder_path;
    std::string favorite_scope;
};

struct GateDecision {
    ActionClass action = ActionClass::kFree;
    GateRule rule = GateRule::kNone;
    std::string summary;  // "Click \"Place order\" on shop.example.com"
    std::string host;     // page host without "www."
    // false for card fields, password fields, kUnclassified and pages without a host
    bool trusted_allowed = true;
    // Whether Allow for this task may remember this confirmation. Bookmark mutations deliberately
    // allow this even though a trusted site must never bypass their confirmation.
    bool task_grant_allowed = true;
    // What an "Allow for this task" grant covers besides host and rule: the page's origin, and for
    // a foreign navigation the target origin too. Empty when the origin is unknown.
    std::string scope;
};

// Lowercase host of an absolute URL without scheme, userinfo, port, trailing dot or leading
// "www."; an IPv6 host keeps its brackets. Reads the authority the way a browser does: "\" ends it
// like "/", and tabs and newlines are ignored. Returns "" when the input has no "scheme://".
[[nodiscard]] std::string HostOf(std::string_view url);

// True when host equals a trusted site or is a subdomain of one. Look-alikes do not match. host
// is expected in the form HostOf returns; trusted sites are stored lowercase without "www.".
[[nodiscard]] bool HostMatchesTrusted(std::string_view host,
                                      const std::vector<std::string>& trusted);

// True when text contains a consequential word or phrase (§2.2) as whole words, case-insensitive.
[[nodiscard]] bool IsConsequentialText(std::string_view text);

[[nodiscard]] GateDecision Classify(const GateInput& input,
                                    const std::vector<std::string>& trusted_sites);

}  // namespace island::agent

#endif  // ISLAND_AGENT_ACTION_GATE_H_
