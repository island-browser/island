#ifndef ISLAND_AGENT_BROWSER_TOOLS_H_
#define ISLAND_AGENT_BROWSER_TOOLS_H_

// The browser tool surface AI agents drive. Each tool has an MCP-shaped
// definition (name, description, JSON Schema input) and dispatches to an
// AgentBrowserHost, which BrowserWindow implements on the CEF UI thread.
//
// Page-level tools (read, snapshot, click, type, scroll, evaluate,
// screenshot) are built here from DevTools protocol calls, so the host only
// has to provide one asynchronous DevToolsCall primitive and the logic stays
// CEF-free and unit-testable.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "action_gate.h"
#include "json_util.h"

namespace island::agent {

struct AgentTabInfo {
    std::size_t index = 0;
    std::string title;
    std::string url;
    bool active = false;
    bool loading = false;
    bool pinned = false;
    bool opened_by_agent = false;  // the agent opened this tab, so it may navigate it freely
    // Non-empty: Island's own page (the research Plan tab). Page tools and navigation refuse the
    // tab with this message before the action gate asks anyone.
    std::string refusal;
};

struct AgentSpaceInfo {
    std::size_t index = 0;
    std::string name;
    std::uint32_t color_argb = 0;
    std::size_t tab_count = 0;
    bool active = false;
};

// Window-level actions an agent may trigger without arguments.
enum class AgentBrowserAction : std::uint8_t {
    kBack,
    kForward,
    kReload,
    kToggleSplit,
    kToggleSidebar,
};

// Outcome of a host operation: ok, or a human-readable error the agent sees.
struct HostStatus {
    bool ok = true;
    std::string error;

    static HostStatus Ok() { return {}; }
    static HostStatus Error(std::string message) { return {false, std::move(message)}; }
};

// CEF-free data exchanged with the window-side bookmark bridge. Folder paths are display paths
// (for example "Work / References"); favorite scopes are global or a persisted decimal space id.
struct AgentFavoriteScope {
    std::optional<std::uint64_t> space_id;
};

struct AgentBookmarkInfo {
    std::uint64_t id = 0;
    std::string title;
    std::string url;
    std::string folder_path;
    std::vector<AgentFavoriteScope> favorite_scopes;
};

struct AgentBookmarkDraft {
    std::string title;
    std::string url;
    std::string folder_path;
};

using AgentBookmarksCallback = std::function<void(HostStatus, std::vector<AgentBookmarkInfo>)>;
using AgentBookmarkStatusCallback = std::function<void(HostStatus)>;

// Result of a DevTools protocol call: the method's `result` object, or an
// error message.
struct DevToolsReply {
    bool ok = false;
    json::Value result;
    std::string error;
};
using DevToolsCallback = std::function<void(DevToolsReply)>;

// The person's answer to a ConfirmationRequest.
enum class ConfirmationAnswer : std::uint8_t {
    kAllowOnce,
    kAllowForTask,
    kTrustSite,
    kCancel,
    kTimeout,
    // The card was withdrawn without an answer: the tab changed, the task stopped, or the window
    // closed. Not a refusal, so the caller is told to ask again.
    kWithdrawn,
};

// A page action the gate wants the person to approve before it runs.
struct ConfirmationRequest {
    std::string id;  // "c1", "c2", ... unique per toolbox
    std::string tool;
    std::string summary;
    std::string host;
    std::optional<std::size_t> tab;
    bool trusted_allowed = true;  // false: the prompt must not offer "Trust this site"
};

// Browser operations the tool layer needs. Tab indices are positions in the
// active space's tab list; std::nullopt means "the active tab".
class AgentBrowserHost {
  public:
    virtual ~AgentBrowserHost() = default;

    virtual std::vector<AgentSpaceInfo> ListSpaces() = 0;
    virtual std::vector<AgentTabInfo> ListTabs() = 0;
    virtual HostStatus OpenTab(std::string_view url) = 0;
    virtual HostStatus Navigate(std::optional<std::size_t> tab, std::string_view url) = 0;
    virtual HostStatus ActivateTab(std::size_t tab) = 0;
    virtual HostStatus CloseTab(std::size_t tab) = 0;
    virtual HostStatus PinTab(std::size_t tab, bool pinned) = 0;
    virtual HostStatus SwitchSpace(std::size_t space) = 0;
    virtual HostStatus NewSpace(std::string_view name) = 0;
    virtual HostStatus RunAction(AgentBrowserAction action) = 0;
    // Sends one DevTools protocol method to the tab's browser and reports the
    // reply exactly once (possibly synchronously).
    virtual void DevToolsCall(std::optional<std::size_t> tab, std::string_view method,
                              json::Value params, DevToolsCallback done) = 0;
    // Sites the person trusts for agent actions (lowercase, no "www.").
    virtual std::vector<std::string> TrustedSites() = 0;
    // Asks the person to approve an action and reports the answer exactly once, possibly
    // synchronously. The host owns timeouts and reports kTimeout.
    virtual void RequestConfirmation(ConfirmationRequest request,
                                     std::function<void(ConfirmationAnswer)> done) = 0;
    // The URL browser_navigate would load for `text` (search text becomes a search URL), or
    // nullopt when it resolves to no allowed address.
    virtual std::optional<std::string> ResolveNavigationTarget(std::string_view text) = 0;
    // Bookmark operations are asynchronous because the window-side store may save on its owner
    // thread. Defaults fail closed until the window bridge implements them.
    virtual void ListBookmarks(std::optional<std::string> folder_path,
                               AgentBookmarksCallback done) {
        (void)folder_path;
        done(HostStatus::Error("bookmarks unavailable"), {});
    }
    virtual void ListFavorites(std::optional<AgentFavoriteScope> scope,
                               AgentBookmarksCallback done) {
        (void)scope;
        done(HostStatus::Error("bookmarks unavailable"), {});
    }
    virtual void AddBookmark(AgentBookmarkDraft bookmark, AgentBookmarkStatusCallback done) {
        (void)bookmark;
        done(HostStatus::Error("bookmarks unavailable"));
    }
    virtual void RemoveBookmark(std::uint64_t id, AgentBookmarkStatusCallback done) {
        (void)id;
        done(HostStatus::Error("bookmarks unavailable"));
    }
    virtual void AddFavorite(std::uint64_t bookmark_id, AgentFavoriteScope scope,
                             AgentBookmarkStatusCallback done) {
        (void)bookmark_id;
        (void)scope;
        done(HostStatus::Error("bookmarks unavailable"));
    }
    virtual void RemoveFavorite(std::uint64_t bookmark_id, AgentFavoriteScope scope,
                                AgentBookmarkStatusCallback done) {
        (void)bookmark_id;
        (void)scope;
        done(HostStatus::Error("bookmarks unavailable"));
    }
};

// One MCP content block of a tool result.
struct ToolContent {
    enum class Type : std::uint8_t { kText, kImage };
    Type type = Type::kText;
    std::string text;       // kText
    std::string data;       // kImage: base64
    std::string mime_type;  // kImage
};

struct ToolResult {
    std::vector<ToolContent> content;
    bool is_error = false;

    static ToolResult Text(std::string text);
    static ToolResult Json(const json::Value& value);
    static ToolResult Error(std::string message);
    [[nodiscard]] json::Value ToJson() const;
};
using ToolCallback = std::function<void(ToolResult)>;

struct ToolDefinition {
    std::string name;
    std::string title;
    std::string description;
    json::Value input_schema;
    bool read_only = false;
};

// The normalized form of a label for the post-card re-check. Two forms are normalized: a time-like
// run (minutes and seconds 00-59: "0:42", "1:02:03") becomes "#", and a trailing parenthesized
// counter ("Like (12)") becomes "(#)" when no digit comes before it. A label with a currency mark
// (see kCurrencyMarks in browser_tools.cc) or a '#' is returned unchanged. Every other digit stays
// exact.
std::string NormalizeLiveCounters(std::string_view text);

// True when two labels may be treated as the same after a card: identical, or both free of the
// unsafe marks above and equal once normalized. A '#' or currency label never matches a different
// label, so the placeholder cannot collide with a literal '#'.
bool LiveLabelsMatch(std::string_view a, std::string_view b);

class BrowserToolbox {
  public:
    explicit BrowserToolbox(AgentBrowserHost& host);

    [[nodiscard]] const std::vector<ToolDefinition>& definitions() const { return definitions_; }
    [[nodiscard]] bool HasTool(std::string_view name) const;

    // Runs a tool. `done` is called exactly once; unknown tools and invalid
    // arguments report an error result rather than failing the call.
    void Call(std::string_view name, const json::Value& arguments, ToolCallback done);

    // Forgets the "Allow for this task" grants; the task queue calls it when a task starts.
    void ResetTaskGrants();

    // Page text is clipped to this many bytes unless the caller asks for less.
    static constexpr std::size_t kDefaultMaxPageChars = 20000;
    static constexpr std::size_t kMaxPageChars = 200000;

  private:
    void Evaluate(std::optional<std::size_t> tab, std::string expression,
                  std::function<void(bool ok, json::Value value, std::string error)> done);

    void ReadPage(const json::Value& args, ToolCallback done);
    void Snapshot(const json::Value& args, ToolCallback done);
    // Classifies `input` and runs `run` when allowed, asking the person first when the gate says
    // so. `done` receives the error result when the action is blocked, declined or times out.
    // `run` receives `done` and finishes the action.
    // `after_wait`, when set, replaces `run` once the person was asked: the page may have changed
    // during the wait, so it re-reads the target, compares it with what was approved and acts on
    // the fresh state.
    void Gate(GateInput input, std::optional<std::size_t> tab,
              std::function<void(ToolCallback)> run, ToolCallback done,
              std::function<void(ToolCallback)> after_wait = nullptr);
    // The gate's decision for `input`, with the page origin the page reports winning over the tab
    // URL.
    [[nodiscard]] GateDecision Decide(GateInput input);
    // After a card: `fresh` must be free now, or need the same action and rule the person saw
    // (`approved`). A blocked element, or a new rule, fails the action.
    [[nodiscard]] bool StillApproved(GateInput approved, GateInput fresh);
    // The info of `tab` (the active tab when nullopt); nullopt for an unknown tab.
    [[nodiscard]] std::optional<AgentTabInfo> FindTab(std::optional<std::size_t> tab);
    // The error for a tab the agent must not act on (the Plan tab); nullopt when it may.
    [[nodiscard]] std::optional<ToolResult> Refusal(std::optional<std::size_t> tab);
    // Reads the focused element's facts with one evaluation; nullopt when the evaluation failed
    // or nothing is focused.
    void ReadFocusedFacts(std::optional<std::size_t> tab,
                          std::function<void(std::optional<ElementFacts>)> done);
    void Click(const json::Value& args, ToolCallback done);
    void TypeText(const json::Value& args, ToolCallback done);
    void PressKey(const json::Value& args, ToolCallback done);
    void Scroll(const json::Value& args, ToolCallback done);
    void EvaluateTool(const json::Value& args, ToolCallback done);
    void Screenshot(const json::Value& args, ToolCallback done);
    void ListBookmarks(const json::Value& args, ToolCallback done);
    void ListFavorites(const json::Value& args, ToolCallback done);
    void AddBookmark(const json::Value& args, ToolCallback done);
    void RemoveBookmark(const json::Value& args, ToolCallback done);
    void AddFavorite(const json::Value& args, ToolCallback done);
    void RemoveFavorite(const json::Value& args, ToolCallback done);

    AgentBrowserHost& host_;
    std::vector<ToolDefinition> definitions_;
    // (host, rule, scope): scope is the target origin for a foreign navigation, else empty.
    std::set<std::tuple<std::string, GateRule, std::string>> task_grants_;
    std::uint64_t next_confirmation_ = 1;
};

// Builds the JS expression that resolves an element from a snapshot ref or a
// CSS selector; exposed for tests.
[[nodiscard]] std::string ElementLocatorScript(const json::Value& args);
// Escapes a string as a JS/JSON string literal (with quotes).
[[nodiscard]] std::string JsStringLiteral(std::string_view text);

}  // namespace island::agent

#endif  // ISLAND_AGENT_BROWSER_TOOLS_H_
