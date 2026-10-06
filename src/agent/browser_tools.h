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
#include <string>
#include <string_view>
#include <vector>

#include "json_util.h"

namespace island::agent {

struct AgentTabInfo {
    std::size_t index = 0;
    std::string title;
    std::string url;
    bool active = false;
    bool loading = false;
    bool pinned = false;
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

// Result of a DevTools protocol call: the method's `result` object, or an
// error message.
struct DevToolsReply {
    bool ok = false;
    json::Value result;
    std::string error;
};
using DevToolsCallback = std::function<void(DevToolsReply)>;

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

class BrowserToolbox {
  public:
    explicit BrowserToolbox(AgentBrowserHost& host);

    [[nodiscard]] const std::vector<ToolDefinition>& definitions() const { return definitions_; }
    [[nodiscard]] bool HasTool(std::string_view name) const;

    // Runs a tool. `done` is called exactly once; unknown tools and invalid
    // arguments report an error result rather than failing the call.
    void Call(std::string_view name, const json::Value& arguments, ToolCallback done);

    // Page text is clipped to this many bytes unless the caller asks for less.
    static constexpr std::size_t kDefaultMaxPageChars = 20000;
    static constexpr std::size_t kMaxPageChars = 200000;

  private:
    void Evaluate(std::optional<std::size_t> tab, std::string expression,
                  std::function<void(bool ok, json::Value value, std::string error)> done);

    void ReadPage(const json::Value& args, ToolCallback done);
    void Snapshot(const json::Value& args, ToolCallback done);
    void Click(const json::Value& args, ToolCallback done);
    void TypeText(const json::Value& args, ToolCallback done);
    void PressKey(const json::Value& args, ToolCallback done);
    void Scroll(const json::Value& args, ToolCallback done);
    void EvaluateTool(const json::Value& args, ToolCallback done);
    void Screenshot(const json::Value& args, ToolCallback done);

    AgentBrowserHost& host_;
    std::vector<ToolDefinition> definitions_;
};

// Builds the JS expression that resolves an element from a snapshot ref or a
// CSS selector; exposed for tests.
[[nodiscard]] std::string ElementLocatorScript(const json::Value& args);
// Escapes a string as a JS/JSON string literal (with quotes).
[[nodiscard]] std::string JsStringLiteral(std::string_view text);

}  // namespace island::agent

#endif  // ISLAND_AGENT_BROWSER_TOOLS_H_
