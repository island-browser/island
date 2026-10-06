#include "browser_tools.h"

#include <gtest/gtest.h>

#include <set>
#include <string>

#include "fake_browser_host.h"

namespace island::agent {
namespace {

using json::Value;
using testing::FakeBrowserHost;

ToolResult CallTool(BrowserToolbox& toolbox, std::string_view name,
                    Value args = Value::MakeObject()) {
    std::optional<ToolResult> captured;
    int calls = 0;
    toolbox.Call(name, args, [&](ToolResult result) {
        captured = std::move(result);
        ++calls;
    });
    EXPECT_EQ(calls, 1) << name;
    return captured.value_or(ToolResult::Error("no result"));
}

Value ResultJson(const ToolResult& result) {
    EXPECT_FALSE(result.content.empty());
    std::optional<Value> parsed = json::Parse(result.content.at(0).text);
    EXPECT_TRUE(parsed.has_value()) << result.content.at(0).text;
    return parsed.value_or(Value{});
}

TEST(BrowserToolsTest, DefinitionsAreUniqueObjectSchemas) {
    FakeBrowserHost host;
    BrowserToolbox toolbox(host);
    std::set<std::string> names;
    for (const ToolDefinition& def : toolbox.definitions()) {
        EXPECT_TRUE(names.insert(def.name).second) << def.name;
        EXPECT_FALSE(def.description.empty()) << def.name;
        EXPECT_EQ(def.input_schema.StringOr("type", ""), "object") << def.name;
        EXPECT_NE(def.input_schema.FindMember("properties"), nullptr) << def.name;
    }
    EXPECT_GE(names.size(), 20U);
    EXPECT_TRUE(toolbox.HasTool("page_snapshot"));
    EXPECT_FALSE(toolbox.HasTool("nope"));
}

TEST(BrowserToolsTest, ListTabsReportsActiveSpaceAndTabs) {
    FakeBrowserHost host;
    BrowserToolbox toolbox(host);
    const ToolResult result = CallTool(toolbox, "browser_list_tabs");
    ASSERT_FALSE(result.is_error);
    const Value value = ResultJson(result);
    EXPECT_EQ(value.StringOr("space", ""), "Focus");
    const Value* tabs = value.FindMember("tabs");
    ASSERT_NE(tabs, nullptr);
    ASSERT_EQ(tabs->array_val.size(), 2U);
    EXPECT_EQ(tabs->array_val[0].StringOr("url", ""), "https://island.test/");
    EXPECT_TRUE(tabs->array_val[0].BoolOr("active", false));
    EXPECT_TRUE(tabs->array_val[1].BoolOr("loading", false));
}

TEST(BrowserToolsTest, ListSpacesFormatsColors) {
    FakeBrowserHost host;
    BrowserToolbox toolbox(host);
    const Value value = ResultJson(CallTool(toolbox, "browser_list_spaces"));
    const Value* spaces = value.FindMember("spaces");
    ASSERT_NE(spaces, nullptr);
    ASSERT_EQ(spaces->array_val.size(), 2U);
    EXPECT_EQ(spaces->array_val[0].StringOr("color", ""), "#168C99");
    EXPECT_EQ(spaces->array_val[1].StringOr("name", ""), "Research");
}

TEST(BrowserToolsTest, WindowToolsDispatchToHost) {
    FakeBrowserHost host;
    BrowserToolbox toolbox(host);
    EXPECT_FALSE(CallTool(toolbox, "browser_open_tab",
                          Value::MakeObject().Set("url", Value::String("example.com")))
                     .is_error);
    EXPECT_FALSE(CallTool(toolbox, "browser_navigate",
                          Value::MakeObject()
                              .Set("url", Value::String("https://a.test"))
                              .Set("tab", Value::Int(1)))
                     .is_error);
    EXPECT_FALSE(
        CallTool(toolbox, "browser_activate_tab", Value::MakeObject().Set("tab", Value::Int(1)))
            .is_error);
    EXPECT_FALSE(
        CallTool(toolbox, "browser_switch_space", Value::MakeObject().Set("space", Value::Int(1)))
            .is_error);
    EXPECT_FALSE(CallTool(toolbox, "browser_back").is_error);
    EXPECT_FALSE(CallTool(toolbox, "browser_pin_tab").is_error);
    EXPECT_FALSE(
        CallTool(toolbox, "browser_pin_tab",
                 Value::MakeObject().Set("tab", Value::Int(1)).Set("pinned", Value::Bool(false)))
            .is_error);
    EXPECT_EQ(host.calls, (std::vector<std::string>{"open:example.com", "navigate:1:https://a.test",
                                                    "activate:1", "space:1", "action:0", "pin:0:1",
                                                    "pin:1:0"}));
}

TEST(BrowserToolsTest, ArgumentErrorsAreToolErrorsNotCrashes) {
    FakeBrowserHost host;
    BrowserToolbox toolbox(host);
    EXPECT_TRUE(CallTool(toolbox, "browser_navigate").is_error);
    EXPECT_TRUE(CallTool(toolbox, "browser_activate_tab").is_error);
    EXPECT_TRUE(
        CallTool(toolbox, "browser_activate_tab", Value::MakeObject().Set("tab", Value::Int(-1)))
            .is_error);
    EXPECT_TRUE(
        CallTool(toolbox, "browser_activate_tab", Value::MakeObject().Set("tab", Value::Int(9)))
            .is_error);
    EXPECT_TRUE(CallTool(toolbox, "page_click").is_error);
    EXPECT_TRUE(CallTool(toolbox, "page_type").is_error);
    EXPECT_TRUE(CallTool(toolbox, "page_scroll",
                         Value::MakeObject().Set("direction", Value::String("left")))
                    .is_error);
    EXPECT_TRUE(CallTool(toolbox, "unknown_tool").is_error);
    EXPECT_TRUE(host.calls.empty());
    EXPECT_TRUE(host.devtools.empty());
}

TEST(BrowserToolsTest, ReadPageEvaluatesAndReturnsPageJson) {
    FakeBrowserHost host;
    host.QueueScriptResult(R"({"title":"T","url":"https://t.test/","length":5,"text":"hello"})");
    BrowserToolbox toolbox(host);
    const ToolResult result =
        CallTool(toolbox, "page_read", Value::MakeObject().Set("max_chars", Value::Int(10)));
    ASSERT_FALSE(result.is_error) << result.content[0].text;
    EXPECT_EQ(ResultJson(result).StringOr("text", ""), "hello");
    ASSERT_EQ(host.devtools.size(), 1U);
    EXPECT_EQ(host.devtools[0].method, "Runtime.evaluate");
    EXPECT_FALSE(host.devtools[0].tab.has_value());
    EXPECT_TRUE(host.devtools[0].params.BoolOr("returnByValue", false));
    EXPECT_NE(host.devtools[0].params.StringOr("expression", "").find("const max = 10;"),
              std::string_view::npos);
}

TEST(BrowserToolsTest, EvaluateSurfacesPageExceptions) {
    FakeBrowserHost host;
    DevToolsReply reply;
    reply.ok = true;
    reply.result = Value::MakeObject().Set(
        "exceptionDetails",
        Value::MakeObject()
            .Set("text", Value::String("Uncaught"))
            .Set("exception",
                 Value::MakeObject().Set("description",
                                         Value::String("ReferenceError: x is not defined"))));
    host.replies.push_back(reply);
    BrowserToolbox toolbox(host);
    const ToolResult result = CallTool(toolbox, "page_evaluate",
                                       Value::MakeObject().Set("expression", Value::String("x")));
    EXPECT_TRUE(result.is_error);
    EXPECT_NE(result.content[0].text.find("ReferenceError"), std::string::npos);
}

TEST(BrowserToolsTest, EvaluateReturnsValue) {
    FakeBrowserHost host;
    DevToolsReply reply;
    reply.ok = true;
    reply.result = Value::MakeObject().Set(
        "result",
        Value::MakeObject().Set("type", Value::String("number")).Set("value", Value::Int(42)));
    host.replies.push_back(reply);
    BrowserToolbox toolbox(host);
    const ToolResult result = CallTool(
        toolbox, "page_evaluate",
        Value::MakeObject().Set("expression", Value::String("6*7")).Set("tab", Value::Int(1)));
    ASSERT_FALSE(result.is_error);
    EXPECT_EQ(result.content[0].text, R"({"value":42})");
    EXPECT_EQ(host.devtools[0].tab, std::optional<std::size_t>(1));
}

TEST(BrowserToolsTest, ClickUsesTrustedMouseEventsAtElementCenter) {
    FakeBrowserHost host;
    host.QueueScriptResult(R"({"ok":true,"x":120,"y":48})");
    BrowserToolbox toolbox(host);
    const ToolResult result =
        CallTool(toolbox, "page_click", Value::MakeObject().Set("ref", Value::Int(7)));
    ASSERT_FALSE(result.is_error) << result.content[0].text;
    ASSERT_EQ(host.devtools.size(), 4U);
    EXPECT_NE(host.devtools[0].params.StringOr("expression", "").find("[data-island-ref=\"7\"]"),
              std::string_view::npos);
    const char* const kTypes[] = {"mouseMoved", "mousePressed", "mouseReleased"};
    for (int i = 0; i < 3; ++i) {
        const auto& call = host.devtools[static_cast<std::size_t>(i + 1)];
        EXPECT_EQ(call.method, "Input.dispatchMouseEvent");
        EXPECT_EQ(call.params.StringOr("type", ""), kTypes[i]);
        EXPECT_DOUBLE_EQ(call.params.FindMember("x")->AsDouble(), 120.0);
        EXPECT_DOUBLE_EQ(call.params.FindMember("y")->AsDouble(), 48.0);
    }
    EXPECT_EQ(host.devtools[2].params.StringOr("button", ""), "left");
}

TEST(BrowserToolsTest, ClickReportsMissingElement) {
    FakeBrowserHost host;
    host.QueueScriptResult(R"({"ok":false,"error":"element not found"})");
    BrowserToolbox toolbox(host);
    const ToolResult result =
        CallTool(toolbox, "page_click", Value::MakeObject().Set("selector", Value::String("#go")));
    EXPECT_TRUE(result.is_error);
    EXPECT_EQ(host.devtools.size(), 1U);
}

TEST(BrowserToolsTest, TypeFocusesInsertsTextAndSubmits) {
    FakeBrowserHost host;
    host.QueueScriptResult(R"({"ok":true})");
    BrowserToolbox toolbox(host);
    const ToolResult result = CallTool(toolbox, "page_type",
                                       Value::MakeObject()
                                           .Set("ref", Value::Int(3))
                                           .Set("text", Value::String("hello \"world\""))
                                           .Set("submit", Value::Bool(true)));
    ASSERT_FALSE(result.is_error) << result.content[0].text;
    ASSERT_EQ(host.devtools.size(), 4U);
    EXPECT_EQ(host.devtools[1].method, "Input.insertText");
    EXPECT_EQ(host.devtools[1].params.StringOr("text", ""), "hello \"world\"");
    EXPECT_EQ(host.devtools[2].params.StringOr("type", ""), "keyDown");
    EXPECT_EQ(host.devtools[2].params.StringOr("key", ""), "Enter");
    EXPECT_EQ(host.devtools[2].params.StringOr("text", ""), "\r");
    EXPECT_EQ(host.devtools[3].params.StringOr("type", ""), "keyUp");
}

TEST(BrowserToolsTest, TypeWithoutTargetTypesIntoFocus) {
    FakeBrowserHost host;
    BrowserToolbox toolbox(host);
    const ToolResult result =
        CallTool(toolbox, "page_type", Value::MakeObject().Set("text", Value::String("abc")));
    ASSERT_FALSE(result.is_error);
    ASSERT_EQ(host.devtools.size(), 1U);
    EXPECT_EQ(host.devtools[0].method, "Input.insertText");
}

TEST(BrowserToolsTest, DevToolsFailureBecomesToolError) {
    FakeBrowserHost host;
    host.replies.push_back({.ok = false, .error = "No browser for tab 4"});
    BrowserToolbox toolbox(host);
    const ToolResult result =
        CallTool(toolbox, "page_read", Value::MakeObject().Set("tab", Value::Int(4)));
    EXPECT_TRUE(result.is_error);
    EXPECT_NE(result.content[0].text.find("No browser for tab 4"), std::string::npos);
}

TEST(BrowserToolsTest, ScreenshotReturnsImageContent) {
    FakeBrowserHost host;
    host.replies.push_back(
        {.ok = true, .result = Value::MakeObject().Set("data", Value::String("iVBORw0KGgo="))});
    BrowserToolbox toolbox(host);
    const ToolResult result = CallTool(toolbox, "page_screenshot");
    ASSERT_FALSE(result.is_error);
    ASSERT_EQ(result.content.size(), 1U);
    EXPECT_EQ(result.content[0].type, ToolContent::Type::kImage);
    EXPECT_EQ(result.content[0].mime_type, "image/png");
    const Value json = result.ToJson();
    EXPECT_EQ(json.FindMember("content")->array_val[0].StringOr("type", ""), "image");
    EXPECT_FALSE(json.BoolOr("isError", true));
    EXPECT_EQ(host.devtools[0].method, "Page.captureScreenshot");
}

TEST(BrowserToolsTest, PressKeyAndScroll) {
    FakeBrowserHost host;
    BrowserToolbox toolbox(host);
    EXPECT_FALSE(
        CallTool(toolbox, "page_press_key", Value::MakeObject().Set("key", Value::String("Escape")))
            .is_error);
    EXPECT_TRUE(
        CallTool(toolbox, "page_press_key", Value::MakeObject().Set("key", Value::String("F13")))
            .is_error);
    host.QueueScriptResult(R"({"scroll_y":600})");
    const ToolResult scrolled = CallTool(
        toolbox, "page_scroll", Value::MakeObject().Set("direction", Value::String("down")));
    ASSERT_FALSE(scrolled.is_error);
    ASSERT_EQ(host.devtools.size(), 3U);
    EXPECT_EQ(host.devtools[0].params.FindMember("windowsVirtualKeyCode")->int_val, 27);
    EXPECT_NE(host.devtools[2].params.StringOr("expression", "").find("scrollBy(0, "),
              std::string_view::npos);
}

TEST(BrowserToolsTest, LocatorScriptsEscapeSelectors) {
    EXPECT_EQ(ElementLocatorScript(Value::MakeObject().Set("ref", Value::Int(12))),
              "document.querySelector('[data-island-ref=\"12\"]')");
    const std::string script = ElementLocatorScript(
        Value::MakeObject().Set("selector", Value::String("a[title=\"x\"]</script>")));
    EXPECT_NE(script.find(R"("a[title=\"x\"]\u003c/script>")"), std::string::npos) << script;
    EXPECT_TRUE(ElementLocatorScript(Value::MakeObject()).empty());
    EXPECT_EQ(JsStringLiteral("a\xE2\x80\xA8"
                              "b"),
              R"("a\u2028b")");
}

}  // namespace
}  // namespace island::agent
