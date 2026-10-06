#include "browser_tools.h"

#include <array>
#include <memory>
#include <utility>

namespace island::agent {

namespace {

using json::Value;

// ---- schema helpers ----------------------------------------------------------

Value Prop(std::string_view type, std::string_view description) {
    return Value::MakeObject()
        .Set("type", Value::String(std::string(type)))
        .Set("description", Value::String(std::string(description)));
}

Value EnumProp(std::string_view description, std::initializer_list<std::string_view> values) {
    Value list = Value::MakeArray();
    for (std::string_view v : values) list.Push(Value::String(std::string(v)));
    return Prop("string", description).Set("enum", std::move(list));
}

Value TabProp() {
    return Prop("integer",
                "Tab index in the active space (from browser_list_tabs). Defaults to the "
                "active tab.")
        .Set("minimum", Value::Int(0));
}

Value Schema(Value properties, std::initializer_list<std::string_view> required = {}) {
    Value schema = Value::MakeObject()
                       .Set("type", Value::String("object"))
                       .Set("properties", std::move(properties));
    if (required.size() > 0) {
        Value list = Value::MakeArray();
        for (std::string_view r : required) list.Push(Value::String(std::string(r)));
        schema.Set("required", std::move(list));
    }
    schema.Set("additionalProperties", Value::Bool(false));
    return schema;
}

Value Props() { return Value::MakeObject(); }

// ---- argument helpers --------------------------------------------------------

struct TabArg {
    bool valid = true;
    std::optional<std::size_t> tab;
};

TabArg ReadTabArg(const Value& args, std::string_view key = "tab") {
    const Value* v = args.FindMember(key);
    if (v == nullptr || v->IsNull()) return {};
    const std::int64_t index = args.IntOr(key, -1);
    if (index < 0) return {.valid = false};
    return {.valid = true, .tab = static_cast<std::size_t>(index)};
}

std::optional<std::size_t> ReadIndex(const Value& args, std::string_view key) {
    const std::int64_t index = args.IntOr(key, -1);
    if (index < 0) return std::nullopt;
    return static_cast<std::size_t>(index);
}

ToolResult StatusResult(const HostStatus& status, std::string_view success) {
    if (!status.ok) return ToolResult::Error(status.error);
    return ToolResult::Text(std::string(success));
}

Value TabsJson(const std::vector<AgentTabInfo>& tabs) {
    Value list = Value::MakeArray();
    for (const AgentTabInfo& tab : tabs) {
        Value item = Value::MakeObject()
                         .Set("index", Value::Int(static_cast<std::int64_t>(tab.index)))
                         .Set("title", Value::String(tab.title))
                         .Set("url", Value::String(tab.url))
                         .Set("active", Value::Bool(tab.active))
                         .Set("loading", Value::Bool(tab.loading));
        if (tab.pinned) item.Set("pinned", Value::Bool(true));
        list.Push(std::move(item));
    }
    return list;
}

std::string HexColor(std::uint32_t argb) {
    static constexpr std::array<char, 16> kDigits = {'0', '1', '2', '3', '4', '5', '6', '7',
                                                     '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
    std::string out = "#";
    for (int shift = 20; shift >= 0; shift -= 4) {
        out += kDigits[(argb >> static_cast<unsigned>(shift)) & 0xFU];
    }
    return out;
}

// ---- page scripts --------------------------------------------------------------

// Every script returns a JSON string so the value crosses the DevTools
// boundary intact (returnByValue) and parses with the same JSON code.

std::string ReadPageScript(std::size_t max_chars) {
    return "(() => { const max = " + std::to_string(max_chars) +
           "; const body = document.body ? document.body.innerText : '';"
           " return JSON.stringify({title: document.title, url: location.href,"
           " length: body.length, truncated: body.length > max, text: body.slice(0, max)}); })()";
}

std::string SnapshotScript(std::size_t limit) {
    return "(() => { const limit = " + std::to_string(limit) +
           R"JS(;
  const selector = 'a[href],button,input:not([type=hidden]),textarea,select,summary,' +
    '[role=button],[role=link],[role=checkbox],[role=radio],[role=tab],[role=menuitem],' +
    '[role=option],[role=switch],[role=textbox],[role=combobox],[role=searchbox],' +
    '[contenteditable=""],[contenteditable=true],[onclick]';
  for (const old of document.querySelectorAll('[data-island-ref]')) old.removeAttribute('data-island-ref');
  const elements = [];
  let ref = 0;
  let total = 0;
  for (const el of document.querySelectorAll(selector)) {
    const rect = el.getBoundingClientRect();
    const style = getComputedStyle(el);
    if (rect.width === 0 || rect.height === 0 || style.visibility === 'hidden' || style.display === 'none') continue;
    total += 1;
    if (elements.length >= limit) continue;
    ref += 1;
    el.setAttribute('data-island-ref', String(ref));
    const secret = el.type === 'password';
    const raw = el.getAttribute('aria-label') || el.innerText || (secret ? '' : el.value) ||
      el.placeholder || el.title || el.alt || '';
    const label = String(raw).trim().replace(/\s+/g, ' ').slice(0, 120);
    const item = {ref, tag: el.tagName.toLowerCase()};
    const role = el.getAttribute('role');
    if (role) item.role = role;
    if (el.tagName === 'INPUT') item.type = el.type;
    if (label) item.label = label;
    if (el.href) item.href = String(el.href).slice(0, 300);
    if (el.name) item.name = el.name;
    if (el.disabled) item.disabled = true;
    if (el.checked) item.checked = true;
    item.in_viewport = rect.bottom > 0 && rect.top < innerHeight && rect.right > 0 && rect.left < innerWidth;
    elements.push(item);
  }
  return JSON.stringify({title: document.title, url: location.href, total, elements});
})())JS";
}

std::string ClickTargetScript(const std::string& locator) {
    return "(() => { const el = " + locator +
           R"JS(;
  if (!el) return JSON.stringify({ok: false, error: 'element not found; call page_snapshot for fresh refs'});
  el.scrollIntoView({block: 'center', inline: 'center'});
  const rect = el.getBoundingClientRect();
  if (rect.width === 0 || rect.height === 0) { el.click(); return JSON.stringify({ok: true, synthetic: true}); }
  return JSON.stringify({ok: true, x: Math.round(rect.left + rect.width / 2), y: Math.round(rect.top + rect.height / 2)});
})())JS";
}

std::string FocusScript(const std::string& locator, bool clear) {
    return "(() => { const el = " + locator + "; const clear = " + (clear ? "true" : "false") +
           R"JS(;
  if (!el) return JSON.stringify({ok: false, error: 'element not found; call page_snapshot for fresh refs'});
  el.scrollIntoView({block: 'center'});
  el.focus();
  if (clear) {
    if (typeof el.select === 'function') el.select();
    else if (el.isContentEditable) document.execCommand('selectAll');
  }
  return JSON.stringify({ok: document.activeElement === el || el.contains(document.activeElement)});
})())JS";
}

std::string ScrollScript(std::string_view direction, std::int64_t amount) {
    std::string action;
    if (direction == "top") {
        action = "window.scrollTo(0, 0);";
    } else if (direction == "bottom") {
        action = "window.scrollTo(0, document.documentElement.scrollHeight);";
    } else {
        const std::string step =
            amount > 0 ? std::to_string(amount) : "Math.round(innerHeight * 0.8)";
        action = std::string("window.scrollBy(0, ") + (direction == "up" ? "-" : "") + step + ");";
    }
    return "(() => { " + action +
           " return JSON.stringify({scroll_y: Math.round(scrollY), viewport_height: innerHeight,"
           " page_height: document.documentElement.scrollHeight}); })()";
}

struct KeySpec {
    std::string_view key;
    std::string_view code;
    int key_code;
    std::string_view text;
};

constexpr std::array<KeySpec, 14> kKeys = {{
    {"Enter", "Enter", 13, "\r"},
    {"Tab", "Tab", 9, ""},
    {"Escape", "Escape", 27, ""},
    {"Backspace", "Backspace", 8, ""},
    {"Delete", "Delete", 46, ""},
    {"ArrowUp", "ArrowUp", 38, ""},
    {"ArrowDown", "ArrowDown", 40, ""},
    {"ArrowLeft", "ArrowLeft", 37, ""},
    {"ArrowRight", "ArrowRight", 39, ""},
    {"PageUp", "PageUp", 33, ""},
    {"PageDown", "PageDown", 34, ""},
    {"Home", "Home", 36, ""},
    {"End", "End", 35, ""},
    {"Space", "Space", 32, " "},
}};

const KeySpec* FindKey(std::string_view name) {
    for (const KeySpec& spec : kKeys) {
        if (spec.key == name) return &spec;
    }
    return nullptr;
}

Value KeyEventParams(const KeySpec& spec, bool down) {
    Value params = Value::MakeObject()
                       .Set("type", Value::String(down ? "keyDown" : "keyUp"))
                       .Set("key", Value::String(spec.key == "Space" ? " " : std::string(spec.key)))
                       .Set("code", Value::String(std::string(spec.code)))
                       .Set("windowsVirtualKeyCode", Value::Int(spec.key_code))
                       .Set("nativeVirtualKeyCode", Value::Int(spec.key_code));
    if (down && !spec.text.empty()) params.Set("text", Value::String(std::string(spec.text)));
    return params;
}

// Sends keyDown then keyUp for one key and reports the combined outcome.
void SendKey(AgentBrowserHost& host, std::optional<std::size_t> tab, const KeySpec& spec,
             std::function<void(bool ok, std::string error)> done) {
    host.DevToolsCall(
        tab, "Input.dispatchKeyEvent", KeyEventParams(spec, true),
        [&host, tab, spec, done = std::move(done)](DevToolsReply down) mutable {
            if (!down.ok) {
                done(false, down.error);
                return;
            }
            host.DevToolsCall(
                tab, "Input.dispatchKeyEvent", KeyEventParams(spec, false),
                [done = std::move(done)](DevToolsReply up) { done(up.ok, up.error); });
        });
}

Value MouseParams(std::string_view type, double x, double y) {
    Value params = Value::MakeObject()
                       .Set("type", Value::String(std::string(type)))
                       .Set("x", Value::Double(x))
                       .Set("y", Value::Double(y));
    if (type != "mouseMoved") {
        params.Set("button", Value::String("left")).Set("clickCount", Value::Int(1));
    }
    return params;
}

// Parses the JSON string a page script returned.
std::optional<Value> ParseScriptJson(const Value& value) {
    if (!value.IsString()) return std::nullopt;
    std::optional<Value> parsed = json::Parse(value.string_val);
    if (!parsed || !parsed->IsObject()) return std::nullopt;
    return parsed;
}

}  // namespace

// ---- ToolResult ------------------------------------------------------------------

ToolResult ToolResult::Text(std::string text) {
    ToolResult result;
    result.content.push_back({.type = ToolContent::Type::kText, .text = std::move(text)});
    return result;
}

ToolResult ToolResult::Json(const json::Value& value) { return Text(json::Serialize(value)); }

ToolResult ToolResult::Error(std::string message) {
    ToolResult result = Text(std::move(message));
    result.is_error = true;
    return result;
}

json::Value ToolResult::ToJson() const {
    Value blocks = Value::MakeArray();
    for (const ToolContent& block : content) {
        if (block.type == ToolContent::Type::kImage) {
            blocks.Push(Value::MakeObject()
                            .Set("type", Value::String("image"))
                            .Set("data", Value::String(block.data))
                            .Set("mimeType", Value::String(block.mime_type)));
        } else {
            blocks.Push(Value::MakeObject()
                            .Set("type", Value::String("text"))
                            .Set("text", Value::String(block.text)));
        }
    }
    return Value::MakeObject()
        .Set("content", std::move(blocks))
        .Set("isError", Value::Bool(is_error));
}

// ---- helpers exposed for tests ---------------------------------------------------

std::string JsStringLiteral(std::string_view text) {
    // JSON string syntax is valid JS; additionally escape the line separators
    // that JS (pre-ES2019) and HTML-embedded contexts treat as terminators, and
    // '<' so a "</script>" can never close an enclosing tag.
    std::string serialized = json::Serialize(Value::String(std::string(text)));
    std::string out;
    out.reserve(serialized.size());
    for (std::size_t i = 0; i < serialized.size(); ++i) {
        const char c = serialized[i];
        if (c == '<') {
            out += "\\u003c";
        } else if (static_cast<unsigned char>(c) == 0xE2 && i + 2 < serialized.size() &&
                   static_cast<unsigned char>(serialized[i + 1]) == 0x80 &&
                   (static_cast<unsigned char>(serialized[i + 2]) == 0xA8 ||
                    static_cast<unsigned char>(serialized[i + 2]) == 0xA9)) {
            out += static_cast<unsigned char>(serialized[i + 2]) == 0xA8 ? "\\u2028" : "\\u2029";
            i += 2;
        } else {
            out += c;
        }
    }
    return out;
}

std::string ElementLocatorScript(const json::Value& args) {
    const std::int64_t ref = args.IntOr("ref", -1);
    if (ref > 0) {
        return "document.querySelector('[data-island-ref=\"" + std::to_string(ref) + "\"]')";
    }
    const std::string_view selector = args.StringOr("selector", "");
    if (!selector.empty()) {
        // A malformed selector throws inside querySelector; treat it as "not found".
        return "(() => { try { return document.querySelector(" + JsStringLiteral(selector) +
               "); } catch (e) { return null; } })()";
    }
    return {};
}

// ---- BrowserToolbox ---------------------------------------------------------------

BrowserToolbox::BrowserToolbox(AgentBrowserHost& host) : host_(host) {
    auto add = [this](std::string name, std::string title, std::string description, Value schema,
                      bool read_only) {
        definitions_.push_back({.name = std::move(name),
                                .title = std::move(title),
                                .description = std::move(description),
                                .input_schema = std::move(schema),
                                .read_only = read_only});
    };

    // Window, space, and tab management.
    add("browser_list_tabs", "List tabs",
        "List the tabs of the active space with index, title, URL, and loading state.",
        Schema(Props()), true);
    add("browser_list_spaces", "List spaces",
        "List the browser's spaces (Arc-style tab groups) with index, name, color, and tab "
        "count.",
        Schema(Props()), true);
    add("browser_open_tab", "Open tab",
        "Open a new tab in the active space and make it active. Accepts a URL or search text.",
        Schema(Props().Set("url", Prop("string", "URL or search text to load."))), false);
    add("browser_navigate", "Navigate",
        "Load a URL (or search text) in a tab. Use page_read or page_snapshot afterwards to see "
        "the result.",
        Schema(
            Props().Set("url", Prop("string", "URL or search text to load.")).Set("tab", TabProp()),
            {"url"}),
        false);
    add("browser_activate_tab", "Activate tab", "Make a tab of the active space the visible tab.",
        Schema(Props().Set("tab", TabProp()), {"tab"}), false);
    add("browser_close_tab", "Close tab", "Close a tab of the active space.",
        Schema(Props().Set("tab", TabProp()), {"tab"}), false);
    add("browser_back", "Go back", "Go back in the active tab's history.", Schema(Props()), false);
    add("browser_forward", "Go forward", "Go forward in the active tab's history.", Schema(Props()),
        false);
    add("browser_reload", "Reload", "Reload the active tab.", Schema(Props()), false);
    add("browser_switch_space", "Switch space", "Switch the window to another space.",
        Schema(Props().Set("space", Prop("integer", "Space index from browser_list_spaces.")
                                        .Set("minimum", Value::Int(0))),
               {"space"}),
        false);
    add("browser_new_space", "New space", "Create a new space and switch to it.",
        Schema(Props().Set("name", Prop("string", "Optional name for the space."))), false);
    add("browser_toggle_split", "Toggle split view",
        "Show the active tab side by side with its neighbour, or leave split view.",
        Schema(Props()), false);
    add("browser_toggle_sidebar", "Toggle sidebar", "Show or hide the sidebar.", Schema(Props()),
        false);

    // Page inspection and interaction.
    add("page_read", "Read page",
        "Return the title, URL, and visible text of a tab's page (clipped to max_chars).",
        Schema(Props()
                   .Set("tab", TabProp())
                   .Set("max_chars", Prop("integer",
                                          "Maximum characters of text to return "
                                          "(default 20000).")
                                         .Set("minimum", Value::Int(1)))),
        true);
    add("page_snapshot", "Snapshot interactive elements",
        "List visible links, buttons, inputs, and other interactive elements, each tagged with "
        "a numeric ref for page_click and page_type. Refs are invalidated by the next snapshot "
        "or navigation.",
        Schema(Props()
                   .Set("tab", TabProp())
                   .Set("limit", Prop("integer", "Maximum elements to return (default 150).")
                                     .Set("minimum", Value::Int(1)))),
        true);
    add("page_click", "Click element",
        "Click an element by snapshot ref or CSS selector with a real mouse event.",
        Schema(Props()
                   .Set("ref", Prop("integer", "Element ref from page_snapshot."))
                   .Set("selector", Prop("string", "CSS selector (used when ref is absent)."))
                   .Set("tab", TabProp())),
        false);
    add("page_type", "Type text",
        "Focus an element (by ref or selector) and type text into it with real keyboard input. "
        "Optionally clear it first and press Enter afterwards.",
        Schema(Props()
                   .Set("ref", Prop("integer", "Element ref from page_snapshot."))
                   .Set("selector", Prop("string", "CSS selector (used when ref is absent)."))
                   .Set("text", Prop("string", "Text to type."))
                   .Set("clear", Prop("boolean", "Replace the current value (default true)."))
                   .Set("submit", Prop("boolean", "Press Enter after typing (default false)."))
                   .Set("tab", TabProp()),
               {"text"}),
        false);
    add("page_press_key", "Press key", "Press one key in the focused element of a tab.",
        Schema(Props()
                   .Set("key", EnumProp("Key to press.",
                                        {"Enter", "Tab", "Escape", "Backspace", "Delete", "ArrowUp",
                                         "ArrowDown", "ArrowLeft", "ArrowRight", "PageUp",
                                         "PageDown", "Home", "End", "Space"}))
                   .Set("tab", TabProp()),
               {"key"}),
        false);
    add("page_scroll", "Scroll page", "Scroll a tab's page.",
        Schema(Props()
                   .Set("direction", EnumProp("Scroll direction.", {"down", "up", "top", "bottom"}))
                   .Set("amount", Prop("integer",
                                       "Pixels for up/down (default: 80% of the "
                                       "viewport)."))
                   .Set("tab", TabProp()),
               {"direction"}),
        false);
    add("page_evaluate", "Evaluate JavaScript",
        "Evaluate a JavaScript expression in a tab and return its JSON-serializable value. "
        "Promises are awaited.",
        Schema(Props()
                   .Set("expression", Prop("string", "JavaScript expression to evaluate."))
                   .Set("tab", TabProp()),
               {"expression"}),
        false);
    add("page_screenshot", "Screenshot", "Capture a PNG screenshot of a tab's visible viewport.",
        Schema(Props().Set("tab", TabProp())), true);
}

bool BrowserToolbox::HasTool(std::string_view name) const {
    for (const ToolDefinition& def : definitions_) {
        if (def.name == name) return true;
    }
    return false;
}

void BrowserToolbox::Call(std::string_view name, const json::Value& raw_arguments,
                          ToolCallback done) {
    const Value arguments = raw_arguments.IsObject() ? raw_arguments : Value::MakeObject();
    if (!HasTool(name)) {
        done(ToolResult::Error("Unknown tool: " + std::string(name)));
        return;
    }
    const TabArg tab_arg = ReadTabArg(arguments);
    if (!tab_arg.valid) {
        done(ToolResult::Error("'tab' must be a non-negative integer."));
        return;
    }

    if (name == "browser_list_tabs") {
        std::vector<AgentTabInfo> tabs = host_.ListTabs();
        std::string space_name;
        for (const AgentSpaceInfo& space : host_.ListSpaces()) {
            if (space.active) space_name = space.name;
        }
        done(ToolResult::Json(Value::MakeObject()
                                  .Set("space", Value::String(space_name))
                                  .Set("tabs", TabsJson(tabs))));
        return;
    }
    if (name == "browser_list_spaces") {
        Value list = Value::MakeArray();
        for (const AgentSpaceInfo& space : host_.ListSpaces()) {
            list.Push(Value::MakeObject()
                          .Set("index", Value::Int(static_cast<std::int64_t>(space.index)))
                          .Set("name", Value::String(space.name))
                          .Set("color", Value::String(HexColor(space.color_argb)))
                          .Set("tab_count", Value::Int(static_cast<std::int64_t>(space.tab_count)))
                          .Set("active", Value::Bool(space.active)));
        }
        done(ToolResult::Json(Value::MakeObject().Set("spaces", std::move(list))));
        return;
    }
    if (name == "browser_open_tab") {
        done(StatusResult(host_.OpenTab(arguments.StringOr("url", "")), "Opened a new tab."));
        return;
    }
    if (name == "browser_navigate") {
        const std::string_view url = arguments.StringOr("url", "");
        if (url.empty()) {
            done(ToolResult::Error("'url' is required."));
            return;
        }
        done(StatusResult(host_.Navigate(tab_arg.tab, url),
                          "Navigation started: " + std::string(url)));
        return;
    }
    if (name == "browser_activate_tab" || name == "browser_close_tab") {
        if (!tab_arg.tab) {
            done(ToolResult::Error("'tab' is required."));
            return;
        }
        if (name == "browser_activate_tab") {
            done(StatusResult(host_.ActivateTab(*tab_arg.tab), "Tab activated."));
        } else {
            done(StatusResult(host_.CloseTab(*tab_arg.tab), "Tab closed."));
        }
        return;
    }
    if (name == "browser_switch_space") {
        const std::optional<std::size_t> space = ReadIndex(arguments, "space");
        if (!space) {
            done(ToolResult::Error("'space' must be a non-negative integer."));
            return;
        }
        done(StatusResult(host_.SwitchSpace(*space), "Space switched."));
        return;
    }
    if (name == "browser_new_space") {
        done(StatusResult(host_.NewSpace(arguments.StringOr("name", "")), "Space created."));
        return;
    }
    struct ActionTool {
        std::string_view name;
        AgentBrowserAction action;
        std::string_view message;
    };
    static constexpr std::array<ActionTool, 5> kActions = {{
        {"browser_back", AgentBrowserAction::kBack, "Went back."},
        {"browser_forward", AgentBrowserAction::kForward, "Went forward."},
        {"browser_reload", AgentBrowserAction::kReload, "Reloading."},
        {"browser_toggle_split", AgentBrowserAction::kToggleSplit, "Split view toggled."},
        {"browser_toggle_sidebar", AgentBrowserAction::kToggleSidebar, "Sidebar toggled."},
    }};
    for (const ActionTool& tool : kActions) {
        if (name == tool.name) {
            done(StatusResult(host_.RunAction(tool.action), tool.message));
            return;
        }
    }

    if (name == "page_read") return ReadPage(arguments, std::move(done));
    if (name == "page_snapshot") return Snapshot(arguments, std::move(done));
    if (name == "page_click") return Click(arguments, std::move(done));
    if (name == "page_type") return TypeText(arguments, std::move(done));
    if (name == "page_press_key") return PressKey(arguments, std::move(done));
    if (name == "page_scroll") return Scroll(arguments, std::move(done));
    if (name == "page_evaluate") return EvaluateTool(arguments, std::move(done));
    if (name == "page_screenshot") return Screenshot(arguments, std::move(done));
    done(ToolResult::Error("Unhandled tool: " + std::string(name)));
}

void BrowserToolbox::Evaluate(
    std::optional<std::size_t> tab, std::string expression,
    std::function<void(bool ok, json::Value value, std::string error)> done) {
    Value params = Value::MakeObject()
                       .Set("expression", Value::String(std::move(expression)))
                       .Set("returnByValue", Value::Bool(true))
                       .Set("awaitPromise", Value::Bool(true))
                       .Set("userGesture", Value::Bool(true));
    host_.DevToolsCall(
        tab, "Runtime.evaluate", std::move(params), [done = std::move(done)](DevToolsReply reply) {
            if (!reply.ok) {
                done(false, {}, reply.error.empty() ? "DevTools call failed." : reply.error);
                return;
            }
            if (const Value* details = reply.result.FindMember("exceptionDetails")) {
                std::string message(details->StringOr("text", "Uncaught exception"));
                if (const Value* exception = details->FindMember("exception")) {
                    const std::string_view description = exception->StringOr("description", "");
                    if (!description.empty()) message = std::string(description);
                }
                done(false, {}, message);
                return;
            }
            const Value* result = reply.result.FindMember("result");
            if (result == nullptr) {
                done(true, {}, {});
                return;
            }
            const Value* value = result->FindMember("value");
            done(true, value != nullptr ? *value : Value{}, {});
        });
}

void BrowserToolbox::ReadPage(const json::Value& args, ToolCallback done) {
    std::int64_t max_chars =
        args.IntOr("max_chars", static_cast<std::int64_t>(kDefaultMaxPageChars));
    if (max_chars <= 0) max_chars = static_cast<std::int64_t>(kDefaultMaxPageChars);
    if (max_chars > static_cast<std::int64_t>(kMaxPageChars)) {
        max_chars = static_cast<std::int64_t>(kMaxPageChars);
    }
    Evaluate(ReadTabArg(args).tab, ReadPageScript(static_cast<std::size_t>(max_chars)),
             [done = std::move(done)](bool ok, Value value, std::string error) {
                 if (!ok) {
                     done(ToolResult::Error("page_read failed: " + error));
                     return;
                 }
                 std::optional<Value> page = ParseScriptJson(value);
                 if (!page) {
                     done(ToolResult::Error("page_read returned no page data."));
                     return;
                 }
                 done(ToolResult::Json(*page));
             });
}

void BrowserToolbox::Snapshot(const json::Value& args, ToolCallback done) {
    std::int64_t limit = args.IntOr("limit", 150);
    if (limit <= 0) limit = 150;
    if (limit > 1000) limit = 1000;
    Evaluate(ReadTabArg(args).tab, SnapshotScript(static_cast<std::size_t>(limit)),
             [done = std::move(done)](bool ok, Value value, std::string error) {
                 if (!ok) {
                     done(ToolResult::Error("page_snapshot failed: " + error));
                     return;
                 }
                 std::optional<Value> snapshot = ParseScriptJson(value);
                 if (!snapshot) {
                     done(ToolResult::Error("page_snapshot returned no data."));
                     return;
                 }
                 done(ToolResult::Json(*snapshot));
             });
}

void BrowserToolbox::Click(const json::Value& args, ToolCallback done) {
    const std::string locator = ElementLocatorScript(args);
    if (locator.empty()) {
        done(ToolResult::Error("page_click needs 'ref' or 'selector'."));
        return;
    }
    const std::optional<std::size_t> tab = ReadTabArg(args).tab;
    Evaluate(tab, ClickTargetScript(locator),
             [this, tab, done = std::move(done)](bool ok, Value value, std::string error) mutable {
                 if (!ok) {
                     done(ToolResult::Error("page_click failed: " + error));
                     return;
                 }
                 std::optional<Value> target = ParseScriptJson(value);
                 if (!target) {
                     done(ToolResult::Error("page_click returned no data."));
                     return;
                 }
                 if (!target->BoolOr("ok", false)) {
                     done(ToolResult::Error(std::string(target->StringOr("error", "Not found."))));
                     return;
                 }
                 if (target->BoolOr("synthetic", false)) {
                     done(ToolResult::Text("Clicked (element has no box; used element.click())."));
                     return;
                 }
                 const Value* xv = target->FindMember("x");
                 const Value* yv = target->FindMember("y");
                 if (xv == nullptr || yv == nullptr || !xv->IsNumber() || !yv->IsNumber()) {
                     done(ToolResult::Error("page_click could not locate the element center."));
                     return;
                 }
                 const double x = xv->AsDouble();
                 const double y = yv->AsDouble();
                 auto shared_done = std::make_shared<ToolCallback>(std::move(done));
                 host_.DevToolsCall(
                     tab, "Input.dispatchMouseEvent", MouseParams("mouseMoved", x, y),
                     [this, tab, x, y, shared_done](DevToolsReply moved) {
                         if (!moved.ok) {
                             (*shared_done)(ToolResult::Error("page_click failed: " + moved.error));
                             return;
                         }
                         host_.DevToolsCall(
                             tab, "Input.dispatchMouseEvent", MouseParams("mousePressed", x, y),
                             [this, tab, x, y, shared_done](DevToolsReply pressed) {
                                 if (!pressed.ok) {
                                     (*shared_done)(
                                         ToolResult::Error("page_click failed: " + pressed.error));
                                     return;
                                 }
                                 host_.DevToolsCall(
                                     tab, "Input.dispatchMouseEvent",
                                     MouseParams("mouseReleased", x, y),
                                     [shared_done](DevToolsReply released) {
                                         if (!released.ok) {
                                             (*shared_done)(ToolResult::Error(
                                                 "page_click failed: " + released.error));
                                             return;
                                         }
                                         (*shared_done)(ToolResult::Text("Clicked."));
                                     });
                             });
                     });
             });
}

void BrowserToolbox::TypeText(const json::Value& args, ToolCallback done) {
    const Value* text_value = args.FindMember("text");
    if (text_value == nullptr || !text_value->IsString()) {
        done(ToolResult::Error("page_type needs 'text'."));
        return;
    }
    const std::string text = text_value->string_val;
    const bool submit = args.BoolOr("submit", false);
    const std::optional<std::size_t> tab = ReadTabArg(args).tab;
    const std::string locator = ElementLocatorScript(args);

    auto type_into_focus = [this, tab, text, submit](ToolCallback finish) {
        host_.DevToolsCall(
            tab, "Input.insertText", Value::MakeObject().Set("text", Value::String(text)),
            [this, tab, submit, finish = std::move(finish)](DevToolsReply inserted) mutable {
                if (!inserted.ok) {
                    finish(ToolResult::Error("page_type failed: " + inserted.error));
                    return;
                }
                if (!submit) {
                    finish(ToolResult::Text("Typed."));
                    return;
                }
                SendKey(host_, tab, *FindKey("Enter"),
                        [finish = std::move(finish)](bool ok, std::string error) {
                            finish(ok ? ToolResult::Text("Typed and pressed Enter.")
                                      : ToolResult::Error("page_type failed: " + error));
                        });
            });
    };

    if (locator.empty()) {
        // No target: type into whatever currently has focus.
        type_into_focus(std::move(done));
        return;
    }
    Evaluate(
        tab, FocusScript(locator, args.BoolOr("clear", true)),
        [type_into_focus, done = std::move(done)](bool ok, Value value, std::string error) mutable {
            if (!ok) {
                done(ToolResult::Error("page_type failed: " + error));
                return;
            }
            std::optional<Value> focused = ParseScriptJson(value);
            if (!focused) {
                done(ToolResult::Error("page_type returned no data."));
                return;
            }
            const std::string_view focus_error = focused->StringOr("error", "");
            if (!focus_error.empty()) {
                done(ToolResult::Error(std::string(focus_error)));
                return;
            }
            type_into_focus(std::move(done));
        });
}

void BrowserToolbox::PressKey(const json::Value& args, ToolCallback done) {
    const KeySpec* spec = FindKey(args.StringOr("key", ""));
    if (spec == nullptr) {
        done(ToolResult::Error("Unsupported key."));
        return;
    }
    const std::string key(spec->key);
    SendKey(host_, ReadTabArg(args).tab, *spec,
            [key, done = std::move(done)](bool ok, std::string error) {
                done(ok ? ToolResult::Text("Pressed " + key + ".")
                        : ToolResult::Error("page_press_key failed: " + error));
            });
}

void BrowserToolbox::Scroll(const json::Value& args, ToolCallback done) {
    const std::string_view direction = args.StringOr("direction", "");
    if (direction != "up" && direction != "down" && direction != "top" && direction != "bottom") {
        done(ToolResult::Error("'direction' must be one of up, down, top, bottom."));
        return;
    }
    Evaluate(ReadTabArg(args).tab, ScrollScript(direction, args.IntOr("amount", 0)),
             [done = std::move(done)](bool ok, Value value, std::string error) {
                 if (!ok) {
                     done(ToolResult::Error("page_scroll failed: " + error));
                     return;
                 }
                 std::optional<Value> position = ParseScriptJson(value);
                 done(position ? ToolResult::Json(*position) : ToolResult::Text("Scrolled."));
             });
}

void BrowserToolbox::EvaluateTool(const json::Value& args, ToolCallback done) {
    const std::string_view expression = args.StringOr("expression", "");
    if (expression.empty()) {
        done(ToolResult::Error("'expression' is required."));
        return;
    }
    Evaluate(ReadTabArg(args).tab, std::string(expression),
             [done = std::move(done)](bool ok, Value value, std::string error) {
                 if (!ok) {
                     done(ToolResult::Error("Evaluation failed: " + error));
                     return;
                 }
                 done(ToolResult::Json(Value::MakeObject().Set("value", std::move(value))));
             });
}

void BrowserToolbox::Screenshot(const json::Value& args, ToolCallback done) {
    host_.DevToolsCall(ReadTabArg(args).tab, "Page.captureScreenshot",
                       Value::MakeObject().Set("format", Value::String("png")),
                       [done = std::move(done)](DevToolsReply reply) {
                           if (!reply.ok) {
                               done(ToolResult::Error("page_screenshot failed: " + reply.error));
                               return;
                           }
                           const std::string_view data = reply.result.StringOr("data", "");
                           if (data.empty()) {
                               done(ToolResult::Error("page_screenshot returned no image."));
                               return;
                           }
                           ToolResult result;
                           result.content.push_back({.type = ToolContent::Type::kImage,
                                                     .data = std::string(data),
                                                     .mime_type = "image/png"});
                           done(std::move(result));
                       });
}

}  // namespace island::agent
