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
    EXPECT_GE(names.size(), 26U);
    EXPECT_TRUE(toolbox.HasTool("page_snapshot"));
    EXPECT_FALSE(toolbox.HasTool("nope"));
}

TEST(BrowserToolsTest, ListsBookmarksAndFavoritesWithFilters) {
    FakeBrowserHost host;
    BrowserToolbox toolbox(host);
    const Value bookmarks = ResultJson(
        CallTool(toolbox, "browser_list_bookmarks",
                 Value::MakeObject().Set("folder_path", Value::String("Work / References"))));
    ASSERT_EQ(bookmarks.FindMember("bookmarks")->array_val.size(), 1U);
    EXPECT_EQ(bookmarks.FindMember("bookmarks")->array_val[0].IntOr("id", 0), 7);
    const Value favorites = ResultJson(CallTool(
        toolbox, "browser_list_favorites", Value::MakeObject().Set("scope", Value::String("12"))));
    ASSERT_EQ(favorites.FindMember("favorites")->array_val.size(), 1U);
    EXPECT_EQ(favorites.FindMember("favorites")->array_val[0].StringOr("title", ""), "Island");
    const Value all_favorites = ResultJson(CallTool(toolbox, "browser_list_favorites"));
    ASSERT_EQ(all_favorites.FindMember("favorites")->array_val.size(), 1U);
    EXPECT_EQ(all_favorites.FindMember("favorites")->array_val[0].IntOr("id", 0), 7);
}

TEST(BrowserToolsTest, BookmarkInvalidArgumentsDoNotAskOrWrite) {
    FakeBrowserHost host;
    BrowserToolbox toolbox(host);
    EXPECT_TRUE(CallTool(toolbox, "browser_add_bookmark").is_error);
    EXPECT_TRUE(
        CallTool(toolbox, "browser_remove_bookmark", Value::MakeObject().Set("id", Value::Int(99)))
            .is_error);
    EXPECT_TRUE(
        CallTool(toolbox, "browser_add_favorite",
                 Value::MakeObject().Set("id", Value::Int(7)).Set("scope", Value::String("0")))
            .is_error);
    EXPECT_TRUE(CallTool(toolbox, "browser_add_bookmark",
                         Value::MakeObject().Set("url", Value::String("javascript:alert(1)")))
                    .is_error);
    EXPECT_EQ(host.resolved_targets, (std::vector<std::string>{"javascript:alert(1)"}));
    EXPECT_TRUE(host.confirmations.empty());
    EXPECT_TRUE(std::none_of(host.calls.begin(), host.calls.end(), [](const std::string& call) {
        return call.starts_with("add_") || call.starts_with("remove_");
    }));
}

TEST(BrowserToolsTest, BookmarkMutationsAskBeforeWritingAndReportHostErrors) {
    FakeBrowserHost host;
    host.defer_confirmations = true;
    host.trusted = {"example.test"};
    BrowserToolbox toolbox(host);
    std::optional<ToolResult> result;
    toolbox.Call("browser_add_bookmark",
                 Value::MakeObject()
                     .Set("url", Value::String("https://example.test/a"))
                     .Set("title", Value::String("Example"))
                     .Set("folder_path", Value::String("Work")),
                 [&](ToolResult value) { result = std::move(value); });
    ASSERT_EQ(host.confirmations.size(), 1U);
    EXPECT_EQ(host.confirmations[0].summary,
              "Add bookmark \"Example\" on example.test to folder \"Work\"");
    EXPECT_FALSE(host.confirmations[0].trusted_allowed);
    EXPECT_TRUE(host.confirmations[0].task_grant_allowed);
    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(std::none_of(host.calls.begin(), host.calls.end(), [](const std::string& call) {
        return call.starts_with("add_bookmark:");
    }));
    host.bookmark_status = HostStatus::Error("disk full");
    host.AnswerNext(ConfirmationAnswer::kAllowOnce);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->is_error);
    EXPECT_NE(result->content[0].text.find("disk full"), std::string::npos);
}

TEST(BrowserToolsTest, BookmarkTaskGrantIsBoundToTheBookmarkAndScope) {
    FakeBrowserHost host;
    host.answers = {ConfirmationAnswer::kAllowForTask, ConfirmationAnswer::kCancel,
                    ConfirmationAnswer::kCancel};
    BrowserToolbox toolbox(host);
    const auto add_favorite = [&toolbox](std::int64_t id, std::string scope) {
        return CallTool(toolbox, "browser_add_favorite",
                        Value::MakeObject()
                            .Set("id", Value::Int(id))
                            .Set("scope", Value::String(std::move(scope))));
    };
    EXPECT_FALSE(add_favorite(7, "global").is_error);
    EXPECT_FALSE(add_favorite(7, "global").is_error);
    EXPECT_TRUE(add_favorite(8, "global").is_error);
    EXPECT_TRUE(add_favorite(7, "12").is_error);
    EXPECT_EQ(host.confirmations.size(), 3U);
}

TEST(BrowserToolsTest, RemovingFavoriteOutsideItsScopeDoesNotAskOrWrite) {
    FakeBrowserHost host;
    BrowserToolbox toolbox(host);
    EXPECT_TRUE(
        CallTool(toolbox, "browser_remove_favorite",
                 Value::MakeObject().Set("id", Value::Int(7)).Set("scope", Value::String("77")))
            .is_error);
    EXPECT_TRUE(host.confirmations.empty());
    EXPECT_EQ(host.calls, (std::vector<std::string>{"list_bookmarks"}));
}

TEST(BrowserToolsTest, BookmarkMutationAllowTaskCancelAndTimeoutBehaveSafely) {
    FakeBrowserHost host;
    host.answers = {ConfirmationAnswer::kAllowForTask, ConfirmationAnswer::kCancel,
                    ConfirmationAnswer::kTimeout};
    BrowserToolbox toolbox(host);
    EXPECT_FALSE(
        CallTool(toolbox, "browser_remove_favorite",
                 Value::MakeObject().Set("id", Value::Int(7)).Set("scope", Value::String("12")))
            .is_error);
    // The task grant bypasses a second card, even though this scope never permits trusted-site
    // bypass.
    EXPECT_FALSE(
        CallTool(toolbox, "browser_remove_favorite",
                 Value::MakeObject().Set("id", Value::Int(7)).Set("scope", Value::String("12")))
            .is_error);
    EXPECT_EQ(host.confirmations.size(), 1U);
    toolbox.ResetTaskGrants();
    EXPECT_TRUE(
        CallTool(toolbox, "browser_remove_bookmark", Value::MakeObject().Set("id", Value::Int(7)))
            .is_error);
    EXPECT_TRUE(CallTool(toolbox, "browser_add_bookmark",
                         Value::MakeObject().Set("url", Value::String("https://new.test/")))
                    .is_error);
    EXPECT_EQ(host.calls,
              (std::vector<std::string>{"list_bookmarks", "remove_favorite:7:12", "list_bookmarks",
                                        "remove_favorite:7:12", "list_bookmarks"}));
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
    host.tabs[1].opened_by_agent = true;
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

TEST(BrowserToolsTest, HostilePageJsonIsAnErrorNotACrash) {
    // A page can replace JSON.stringify, so the page script's result is untrusted.
    FakeBrowserHost host;
    host.QueueScriptResult(std::string(50000, '['));
    BrowserToolbox toolbox(host);
    EXPECT_TRUE(CallTool(toolbox, "page_read").is_error);
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
    host.trusted = {"island.test"};
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
    host.trusted = {"docs.test"};
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
    host.QueueScriptResult(R"({"ok":true,"x":120,"y":48,"facts":{"tag":"a","text":"Next",
        "page_origin":"https://island.test"}})");
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
    host.QueueScriptResult(R"({"ok":true,"facts":{"tag":"input","type":"text","in_form":false,
        "page_origin":"https://island.test"}})");
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

TEST(BrowserToolsTest, TypeRefusesWhenTheFocusDidNotLandOnTheTarget) {
    FakeBrowserHost host;
    // FocusScript reports ok:false when the element that has focus is not the target.
    host.QueueScriptResult(R"({"ok":false,"facts":{"tag":"body","type":"",
        "page_origin":"https://island.test"}})");
    BrowserToolbox toolbox(host);
    const ToolResult result =
        CallTool(toolbox, "page_type",
                 Value::MakeObject().Set("ref", Value::Int(3)).Set("text", Value::String("x")));
    EXPECT_TRUE(result.is_error);
    for (const FakeBrowserHost::DevToolsRecord& call : host.devtools) {
        EXPECT_NE(call.method, "Input.insertText");
    }
    EXPECT_NE(result.content[0].text.find("could not focus"), std::string::npos);
}

TEST(BrowserToolsTest, TypeWithoutTargetTypesIntoFocus) {
    FakeBrowserHost host;
    host.QueueScriptResult(R"({"ok":true,"facts":{"tag":"input","type":"text",
        "page_origin":"https://island.test"}})");
    BrowserToolbox toolbox(host);
    const ToolResult result =
        CallTool(toolbox, "page_type", Value::MakeObject().Set("text", Value::String("abc")));
    ASSERT_FALSE(result.is_error);
    ASSERT_EQ(host.devtools.size(), 2U);
    EXPECT_EQ(host.devtools[0].method, "Runtime.evaluate");
    EXPECT_EQ(host.devtools[1].method, "Input.insertText");
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
    host.QueueScriptResult(R"({"ok":true,"facts":{"tag":"input","type":"text",
        "page_origin":"https://island.test"}})");
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
    ASSERT_EQ(host.devtools.size(), 4U);
    EXPECT_EQ(host.devtools[1].params.FindMember("windowsVirtualKeyCode")->int_val, 27);
    EXPECT_NE(host.devtools[3].params.StringOr("expression", "").find("scrollBy(0, "),
              std::string_view::npos);
}

// ---- the action gate ---------------------------------------------------------------

int CountMethod(const FakeBrowserHost& host, std::string_view method) {
    int count = 0;
    for (const auto& call : host.devtools) count += call.method == method;
    return count;
}

TEST(BrowserToolsTest, ConsequentialClickWaitsForAllow) {
    FakeBrowserHost host;
    BrowserToolbox toolbox(host);
    host.QueueScriptResult(R"({"ok":true,"x":10,"y":20,"facts":{"tag":"button",
        "text":"Place order","in_form":true,"is_submit":false,
        "page_origin":"https://shop.example.com"}})");
    // The target is read again after the card, so the page answers twice.
    host.QueueScriptResult(R"({"ok":true,"x":10,"y":20,"facts":{"tag":"button",
        "text":"Place order","in_form":true,"is_submit":false,
        "page_origin":"https://shop.example.com"}})");
    host.tabs[0].url = "https://shop.example.com/cart";
    host.answers.push_back(ConfirmationAnswer::kAllowOnce);
    ToolResult r = CallTool(toolbox, "page_click", Value::MakeObject().Set("ref", Value::Int(3)));
    EXPECT_FALSE(r.is_error);
    ASSERT_EQ(host.confirmations.size(), 1u);
    EXPECT_EQ(host.confirmations[0].id, "c1");
    EXPECT_EQ(host.confirmations[0].summary, "Click \"Place order\" on shop.example.com");
    EXPECT_EQ(host.devtools.back().method, "Input.dispatchMouseEvent");
}

TEST(BrowserToolsTest, CancelledClickDispatchesNoInput) {
    FakeBrowserHost host;
    BrowserToolbox toolbox(host);
    host.QueueScriptResult(R"({"ok":true,"x":1,"y":1,"facts":{"tag":"button","text":"Delete",
        "page_origin":"https://a.test"}})");
    ToolResult r = CallTool(toolbox, "page_click", Value::MakeObject().Set("ref", Value::Int(1)));
    EXPECT_TRUE(r.is_error);
    EXPECT_EQ(r.content.at(0).text, "The person declined this action.");
    for (const auto& call : host.devtools) EXPECT_NE(call.method, "Input.dispatchMouseEvent");
}

TEST(BrowserToolsTest, PasswordTypingNeverReachesInsertText) {
    FakeBrowserHost host;
    BrowserToolbox toolbox(host);
    host.trusted = {"a.test"};
    host.QueueScriptResult(R"({"ok":true,"facts":{"tag":"input","type":"password",
        "page_origin":"https://a.test"}})");
    ToolResult r = CallTool(
        toolbox, "page_type",
        Value::MakeObject().Set("ref", Value::Int(2)).Set("text", Value::String("hunter2")));
    EXPECT_TRUE(r.is_error);
    EXPECT_EQ(r.content.at(0).text, "The agent cannot type into password fields.");
    EXPECT_TRUE(host.confirmations.empty());
    for (const auto& call : host.devtools) EXPECT_NE(call.method, "Input.insertText");
}

TEST(BrowserToolsTest, AllowForTaskCoversSameHostAndRuleOnly) {
    FakeBrowserHost host;
    BrowserToolbox toolbox(host);
    host.tabs[0].url = "https://a.test/";
    host.answers.push_back(ConfirmationAnswer::kAllowForTask);
    CallTool(toolbox, "page_evaluate", Value::MakeObject().Set("expression", Value::String("1")));
    CallTool(toolbox, "page_evaluate", Value::MakeObject().Set("expression", Value::String("2")));
    EXPECT_EQ(host.confirmations.size(), 1u);
    // Another host is a different grant.
    host.tabs[0].url = "https://b.test/";
    CallTool(toolbox, "page_evaluate", Value::MakeObject().Set("expression", Value::String("2")));
    EXPECT_EQ(host.confirmations.size(), 2u);
    host.tabs[0].url = "https://a.test/";
    toolbox.ResetTaskGrants();
    CallTool(toolbox, "page_evaluate", Value::MakeObject().Set("expression", Value::String("3")));
    EXPECT_EQ(host.confirmations.size(), 3u);
}

TEST(BrowserToolsTest, FactsAndClickPointComeFromOneEvaluation) {
    FakeBrowserHost host;
    BrowserToolbox toolbox(host);
    host.QueueScriptResult(R"({"ok":true,"x":5,"y":5,"facts":{"tag":"a","text":"Next",
        "page_origin":"https://a.test"}})");
    CallTool(toolbox, "page_click", Value::MakeObject().Set("ref", Value::Int(1)));
    EXPECT_EQ(CountMethod(host, "Runtime.evaluate"), 1);
    EXPECT_EQ(CountMethod(host, "Input.dispatchMouseEvent"), 3);
}

TEST(BrowserToolsTest, TrustSiteAnswerAllowsAndIsNotAskedAgainWhenHostTrustsIt) {
    FakeBrowserHost host;
    BrowserToolbox toolbox(host);
    host.answers.push_back(ConfirmationAnswer::kTrustSite);
    const char* const facts = R"({"ok":true,"x":5,"y":5,"facts":{"tag":"button","text":"Go",
        "in_form":true,"is_submit":true,"page_origin":"https://a.test"}})";
    host.QueueScriptResult(facts);
    host.QueueScriptResult(facts);  // read again after the card
    EXPECT_FALSE(
        CallTool(toolbox, "page_click", Value::MakeObject().Set("ref", Value::Int(1))).is_error);
    host.QueueScriptResult(facts);
    EXPECT_FALSE(
        CallTool(toolbox, "page_click", Value::MakeObject().Set("ref", Value::Int(1))).is_error);
    EXPECT_EQ(host.confirmations.size(), 1u);
    EXPECT_EQ(CountMethod(host, "Input.dispatchMouseEvent"), 6);
}

TEST(BrowserToolsTest, ForeignNavigationOfPersonTabAsks) {
    FakeBrowserHost host;
    BrowserToolbox toolbox(host);
    const Value args = Value::MakeObject()
                           .Set("url", Value::String("https://evil.test/"))
                           .Set("tab", Value::Int(0));
    EXPECT_TRUE(CallTool(toolbox, "browser_navigate", args).is_error);
    ASSERT_EQ(host.confirmations.size(), 1u);
    EXPECT_EQ(host.confirmations[0].summary, "Open evil.test in your tab");
    EXPECT_EQ(host.confirmations[0].tab, std::optional<std::size_t>(0));
    EXPECT_TRUE(host.calls.empty());
    host.answers.push_back(ConfirmationAnswer::kAllowOnce);
    EXPECT_FALSE(CallTool(toolbox, "browser_navigate", args).is_error);
    EXPECT_EQ(host.calls, (std::vector<std::string>{"navigate:0:https://evil.test/"}));
}

TEST(BrowserToolsTest, ForeignNavigationGrantCoversOnlyTheApprovedTarget) {
    FakeBrowserHost host;
    BrowserToolbox toolbox(host);
    const auto go = [&](const char* url) {
        return CallTool(
            toolbox, "browser_navigate",
            Value::MakeObject().Set("url", Value::String(url)).Set("tab", Value::Int(0)));
    };
    host.answers.push_back(ConfirmationAnswer::kAllowForTask);
    EXPECT_FALSE(go("https://one.test/").is_error);
    EXPECT_FALSE(go("https://one.test/other").is_error);
    EXPECT_EQ(host.confirmations.size(), 1u);
    EXPECT_TRUE(go("https://two.test/").is_error);  // another target asks again
    EXPECT_EQ(host.confirmations.size(), 2u);
}

TEST(BrowserToolsTest, HostlessPageSaysThisPageAndOffersNoGrant) {
    FakeBrowserHost host;
    BrowserToolbox toolbox(host);
    host.tabs[0].url = "about:blank";
    host.answers.push_back(ConfirmationAnswer::kAllowForTask);
    host.answers.push_back(ConfirmationAnswer::kAllowForTask);
    for (int i = 0; i < 2; ++i) {
        CallTool(toolbox, "page_evaluate",
                 Value::MakeObject().Set("expression", Value::String("1")));
    }
    ASSERT_EQ(host.confirmations.size(), 2u);  // the first "for this task" granted nothing
    EXPECT_EQ(host.confirmations[0].summary, "Run a script on this page");
    EXPECT_FALSE(host.confirmations[0].trusted_allowed);
}

TEST(BrowserToolsTest, PlanTabIsRefusedBeforeAnyoneIsAsked) {
    FakeBrowserHost host;
    BrowserToolbox toolbox(host);
    host.tabs[0].refusal = "That tab is the research Plan tab.";
    const Value tab0 = Value::MakeObject().Set("tab", Value::Int(0));
    Value evaluate = tab0;
    evaluate.Set("expression", Value::String("1"));
    Value navigate = tab0;
    navigate.Set("url", Value::String("https://evil.test/"));
    Value click = tab0;
    click.Set("ref", Value::Int(1));
    Value type = click;
    type.Set("text", Value::String("x"));
    Value key = tab0;
    key.Set("key", Value::String("Enter"));
    for (const auto& [tool, args] :
         std::vector<std::pair<const char*, Value>>{{"page_evaluate", evaluate},
                                                    {"browser_navigate", navigate},
                                                    {"page_click", click},
                                                    {"page_type", type},
                                                    {"page_press_key", key}}) {
        const ToolResult r = CallTool(toolbox, tool, args);
        EXPECT_TRUE(r.is_error) << tool;
        EXPECT_EQ(r.content.at(0).text, "That tab is the research Plan tab.") << tool;
    }
    EXPECT_TRUE(host.confirmations.empty());
    EXPECT_TRUE(host.devtools.empty());
}

TEST(BrowserToolsTest, ClickIsDroppedWhenThePageChangedDuringTheCard) {
    FakeBrowserHost host;
    BrowserToolbox toolbox(host);
    host.QueueScriptResult(R"({"ok":true,"x":10,"y":20,"facts":{"tag":"button","text":"Place order",
        "is_submit":true,"page_origin":"https://a.test"}})");
    host.QueueScriptResult(
        R"({"ok":true,"x":10,"y":20,"facts":{"tag":"button","text":"Delete account",
        "is_submit":true,"page_origin":"https://a.test"}})");
    host.answers.push_back(ConfirmationAnswer::kAllowOnce);
    const ToolResult r =
        CallTool(toolbox, "page_click", Value::MakeObject().Set("ref", Value::Int(1)));
    EXPECT_TRUE(r.is_error);
    EXPECT_EQ(r.content.at(0).text, "The page changed while waiting; take a fresh snapshot.");
    EXPECT_EQ(CountMethod(host, "Input.dispatchMouseEvent"), 0);
}

TEST(BrowserToolsTest, ClickUsesTheFreshPointAfterTheCard) {
    FakeBrowserHost host;
    BrowserToolbox toolbox(host);
    const char* const same = R"("facts":{"tag":"button","text":"Place order","is_submit":true,
        "page_origin":"https://a.test"}})";
    host.QueueScriptResult(std::string(R"({"ok":true,"x":10,"y":20,)") + same);
    host.QueueScriptResult(std::string(R"({"ok":true,"x":77,"y":88,)") + same);
    host.answers.push_back(ConfirmationAnswer::kAllowOnce);
    EXPECT_FALSE(
        CallTool(toolbox, "page_click", Value::MakeObject().Set("ref", Value::Int(1))).is_error);
    ASSERT_EQ(CountMethod(host, "Input.dispatchMouseEvent"), 3);
    EXPECT_EQ(host.devtools.back().params.FindMember("x")->AsDouble(), 77);
}

TEST(BrowserToolsTest, TypingIsDroppedWhenFocusMovedDuringTheCard) {
    FakeBrowserHost host;
    BrowserToolbox toolbox(host);
    host.QueueScriptResult(R"({"ok":true,"facts":{"tag":"input","type":"text",
        "autocomplete":"cc-number","page_origin":"https://a.test"}})");
    host.QueueScriptResult(R"({"ok":true,"facts":{"tag":"input","type":"text",
        "page_origin":"https://a.test"}})");
    host.answers.push_back(ConfirmationAnswer::kAllowOnce);
    const ToolResult r =
        CallTool(toolbox, "page_type",
                 Value::MakeObject().Set("ref", Value::Int(2)).Set("text", Value::String("4111")));
    EXPECT_TRUE(r.is_error);
    EXPECT_EQ(r.content.at(0).text, "The page changed while waiting; take a fresh snapshot.");
    EXPECT_EQ(CountMethod(host, "Input.insertText"), 0);
}

TEST(BrowserToolsTest, KeyIsDroppedWhenFocusMovedDuringTheCard) {
    FakeBrowserHost host;
    BrowserToolbox toolbox(host);
    host.QueueScriptResult(R"({"ok":true,"facts":{"tag":"input","type":"text","in_form":true,
        "page_origin":"https://a.test"}})");
    host.QueueScriptResult(R"({"ok":true,"facts":{"tag":"input","type":"search","in_form":true,
        "page_origin":"https://a.test"}})");
    host.answers.push_back(ConfirmationAnswer::kAllowOnce);
    const ToolResult r =
        CallTool(toolbox, "page_press_key", Value::MakeObject().Set("key", Value::String("Enter")));
    EXPECT_TRUE(r.is_error);
    EXPECT_EQ(CountMethod(host, "Input.dispatchKeyEvent"), 0);
}

TEST(BrowserToolsTest, SyntheticClickChecksTheElementItClicks) {
    FakeBrowserHost host;
    BrowserToolbox toolbox(host);
    host.QueueScriptResult(R"({"ok":true,"synthetic":true,"facts":{"tag":"button","text":"Next",
        "page_origin":"https://a.test"}})");
    host.QueueScriptResult(
        R"({"ok":false,"error":"The page changed while waiting; take a fresh snapshot."})");
    const ToolResult r =
        CallTool(toolbox, "page_click", Value::MakeObject().Set("selector", Value::String("b")));
    EXPECT_TRUE(r.is_error);
    EXPECT_EQ(r.content.at(0).text, "The page changed while waiting; take a fresh snapshot.");
    const std::string script = host.devtools.back().params.FindMember("expression")->string_val;
    EXPECT_NE(script.find(R"("text":"Next")"), std::string::npos) << script;
    EXPECT_NE(script.find("rect.width === 0"), std::string::npos) << script;
}

TEST(BrowserToolsTest, FactsComeFromTheElementThatReallyHasFocus) {
    FakeBrowserHost host;
    BrowserToolbox toolbox(host);
    host.QueueScriptResult(R"({"ok":true,"facts":null})");
    CallTool(toolbox, "page_type",
             Value::MakeObject().Set("ref", Value::Int(1)).Set("text", Value::String("x")));
    CallTool(toolbox, "page_press_key", Value::MakeObject().Set("key", Value::String("Tab")));
    int checked = 0;
    for (const auto& call : host.devtools) {
        if (call.method != "Runtime.evaluate") continue;
        const std::string script = call.params.FindMember("expression")->string_val;
        EXPECT_NE(script.find("shadowRoot.activeElement"), std::string::npos);
        EXPECT_NE(script.find("contentDocument"), std::string::npos);
        ++checked;
    }
    EXPECT_GE(checked, 2);
    // A null fact set (opaque frame) can neither be trusted nor granted.
    ASSERT_FALSE(host.confirmations.empty());
    EXPECT_FALSE(host.confirmations[0].trusted_allowed);
}

TEST(BrowserToolsTest, NavigationGateJudgesTheResolvedUrlNotTheRawArgument) {
    FakeBrowserHost host;
    BrowserToolbox toolbox(host);
    // Search text resolves to another origin than the tab's: it must ask even though the raw
    // argument names no host.
    EXPECT_TRUE(CallTool(toolbox, "browser_navigate",
                         Value::MakeObject().Set("url", Value::String("island.test/next")))
                    .is_error == false);
    EXPECT_TRUE(host.confirmations.empty());  // same origin as https://island.test/
    EXPECT_TRUE(CallTool(toolbox, "browser_navigate",
                         Value::MakeObject().Set("url", Value::String("other.test")))
                    .is_error);
    EXPECT_EQ(host.confirmations.size(), 1u);
}

TEST(BrowserToolsTest, AgentTabNavigatesFreely) {
    FakeBrowserHost host;
    BrowserToolbox toolbox(host);
    host.tabs[1].opened_by_agent = true;
    EXPECT_FALSE(CallTool(toolbox, "browser_navigate",
                          Value::MakeObject()
                              .Set("url", Value::String("https://evil.test/"))
                              .Set("tab", Value::Int(1)))
                     .is_error);
    EXPECT_FALSE(
        CallTool(toolbox, "browser_close_tab", Value::MakeObject().Set("tab", Value::Int(1)))
            .is_error);
    EXPECT_TRUE(host.confirmations.empty());
}

TEST(BrowserToolsTest, ClosingAPersonTabAsks) {
    FakeBrowserHost host;
    BrowserToolbox toolbox(host);
    EXPECT_TRUE(
        CallTool(toolbox, "browser_close_tab", Value::MakeObject().Set("tab", Value::Int(0)))
            .is_error);
    ASSERT_EQ(host.confirmations.size(), 1u);
    EXPECT_EQ(host.confirmations[0].summary, "Close your tab on island.test");
    EXPECT_TRUE(host.calls.empty());
}

TEST(BrowserToolsTest, TimeoutReturnsTheTimeoutError) {
    FakeBrowserHost host;
    BrowserToolbox toolbox(host);
    host.answers.push_back(ConfirmationAnswer::kTimeout);
    const ToolResult r = CallTool(toolbox, "page_evaluate",
                                  Value::MakeObject().Set("expression", Value::String("1")));
    EXPECT_TRUE(r.is_error);
    EXPECT_EQ(r.content.at(0).text, "No one confirmed this action in time.");
    EXPECT_EQ(CountMethod(host, "Runtime.evaluate"), 0);
}

TEST(BrowserToolsTest, WithdrawnCardSaysAskAgainNotDeclined) {
    FakeBrowserHost host;
    BrowserToolbox toolbox(host);
    host.answers.push_back(ConfirmationAnswer::kWithdrawn);
    const ToolResult r = CallTool(toolbox, "page_evaluate",
                                  Value::MakeObject().Set("expression", Value::String("1")));
    EXPECT_TRUE(r.is_error);
    EXPECT_EQ(r.content.at(0).text,
              "The tab changed or the task stopped before the action was confirmed; ask again.");
    EXPECT_EQ(CountMethod(host, "Runtime.evaluate"), 0);
}

TEST(BrowserToolsTest, TypingWithoutFactsAsksAndCannotBeTrusted) {
    FakeBrowserHost host;
    BrowserToolbox toolbox(host);
    host.trusted = {"island.test"};
    // No scripted reply: the focused-element evaluation yields no facts.
    EXPECT_TRUE(CallTool(toolbox, "page_type", Value::MakeObject().Set("text", Value::String("x")))
                    .is_error);
    ASSERT_EQ(host.confirmations.size(), 1u);
    EXPECT_FALSE(host.confirmations[0].trusted_allowed);
    EXPECT_FALSE(host.confirmations[0].task_grant_allowed);
    EXPECT_EQ(CountMethod(host, "Input.insertText"), 0);
}

TEST(BrowserToolsTest, EnterInAFormAsksButEscapeDoesNot) {
    FakeBrowserHost host;
    BrowserToolbox toolbox(host);
    const char* const facts = R"({"ok":true,"facts":{"tag":"input","type":"text","in_form":true,
        "page_origin":"https://a.test"}})";
    host.QueueScriptResult(facts);
    EXPECT_FALSE(
        CallTool(toolbox, "page_press_key", Value::MakeObject().Set("key", Value::String("Escape")))
            .is_error);
    EXPECT_TRUE(host.confirmations.empty());
    host.QueueScriptResult(facts);
    EXPECT_TRUE(
        CallTool(toolbox, "page_press_key", Value::MakeObject().Set("key", Value::String("Enter")))
            .is_error);
    ASSERT_EQ(host.confirmations.size(), 1u);
    EXPECT_EQ(host.confirmations[0].summary, "Press Enter in a form on a.test");
}

TEST(BrowserToolsTest, CardFieldAskedEvenOnTrustedSiteAndNeverGrantedForTask) {
    FakeBrowserHost host;
    BrowserToolbox toolbox(host);
    host.trusted = {"a.test"};
    const char* const facts = R"({"ok":true,"facts":{"tag":"input","type":"text",
        "autocomplete":"cc-number","page_origin":"https://a.test"}})";
    const Value args =
        Value::MakeObject().Set("ref", Value::Int(2)).Set("text", Value::String("4111"));
    host.QueueScriptResult(facts);
    host.QueueScriptResult(facts);
    host.answers.push_back(ConfirmationAnswer::kAllowForTask);
    EXPECT_FALSE(CallTool(toolbox, "page_type", args).is_error);
    ASSERT_FALSE(host.confirmations.empty());
    EXPECT_FALSE(host.confirmations[0].task_grant_allowed);
    host.QueueScriptResult(facts);
    EXPECT_TRUE(CallTool(toolbox, "page_type", args).is_error);
    EXPECT_EQ(host.confirmations.size(), 2u);
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

// Label pairs for the live-label rule, shared in spirit with facts_probe.cjs (which runs the same
// pairs through the page copy). `match` is whether the labels count as the same after a card.
struct LiveCase {
    const char* before;
    const char* after;
    bool match;
};
const LiveCase kLiveCases[] = {
    // Countdowns and counters that must keep matching.
    {"Resend in 0:42", "Resend in 0:41", true},
    {"Pay now (expires 4:59)", "Pay now (expires 4:58)", true},
    {"Like (12)", "Like (13)", true},
    {"Try again in 0:42", "Try again in 0:41", true},
    {"Checkout (3)", "Checkout (12)", true},
    // Prices, amounts and quantities that must not match. Each pair is a must-not-click case.
    {"Pay $5", "Pay $500", false},
    {"Transfer 10 USD", "Transfer 9000 USD", false},
    {"Buy 1", "Buy 10", false},
    {"Pay $5 (1)", "Pay $5 (2)", false},
    {"Pay 5 (1)", "Pay 5 (2)", false},
    {"Köp 29:90", "Köp 299:90", false},
    {"Betala 29:90 kr", "Betala 299:90 kr", false},
    {"Pay 5 SEK (1)", "Pay 5 SEK (2)", false},
    {"Оплатить 500 руб (1)", "Оплатить 500 руб (2)", false},
    {"Pay 5 USDT (1)", "Pay 5 USDT (2)", false},
    {"Pay ＄5 (1)", "Pay ＄5 (2)", false},
    {"Pay ₿ (5)", "Pay ₿ (500)", false},
    {"Order # (#)", "Order 1:00 (5)", false},
    {"Pay ５ (1)", "Pay ５ (2)", false},
    {"Pay ５0:42", "Pay ５9:42", false},
    {"Pay 0:42５", "Pay 9:42５", false},
    {"Pay 0:42:5５", "Pay 9:42:5５", false},
    {"ادفع ٥٠ (1)", "ادفع ٥٠ (2)", false},
    {"Pay ￦ (5)", "Pay ￦ (500)", false},
    {"Pay ﹩ (5)", "Pay ﹩ (500)", false},
    {"Pay ﷼ (5)", "Pay ﷼ (500)", false},
    {"Pay ₨ (5)", "Pay ₨ (500)", false},
    {"Pay 5 грн (1)", "Pay 5 грн (2)", false},
    {"Pay 5 Rs (1)", "Pay 5 Rs (2)", false},
    {"Pay 5 Rp (1)", "Pay 5 Rp (2)", false},
    {"Pay 5 RM (1)", "Pay 5 RM (2)", false},
};

TEST(BrowserToolsTest, LiveCountersNormalizeOnlyTimesAndATrailingCounter) {
    EXPECT_EQ(NormalizeLiveCounters("0:42"), "#");
    EXPECT_EQ(NormalizeLiveCounters("1:02:03"), "#");
    EXPECT_EQ(NormalizeLiveCounters("Resend in 0:42"), "Resend in #");
    EXPECT_EQ(NormalizeLiveCounters("Pay now (expires 4:59)"), "Pay now (expires #)");
    EXPECT_EQ(NormalizeLiveCounters("Checkout (3)"), "Checkout (#)");
    EXPECT_EQ(NormalizeLiveCounters("Checkout (12)  "), "Checkout (#)  ");
    // A counter is normalized only when no digit comes before it; a time still is.
    EXPECT_EQ(NormalizeLiveCounters("Pay 5 (1)"), "Pay 5 (1)");
    EXPECT_EQ(NormalizeLiveCounters("Timer 0:42 (3)"), "Timer # (3)");
    // Not a time: minutes or seconds above 59, three leading digits, or a counter not last.
    EXPECT_EQ(NormalizeLiveCounters("29:90"), "29:90");
    EXPECT_EQ(NormalizeLiveCounters("299:90"), "299:90");
    EXPECT_EQ(NormalizeLiveCounters("1000:00"), "1000:00");
    EXPECT_EQ(NormalizeLiveCounters("10:423"), "10:423");
    EXPECT_EQ(NormalizeLiveCounters("Like (12) now"), "Like (12) now");
    // Plain digits stay exact.
    EXPECT_EQ(NormalizeLiveCounters("Buy 1"), "Buy 1");
    EXPECT_EQ(NormalizeLiveCounters("a12b3"), "a12b3");
    EXPECT_EQ(NormalizeLiveCounters("Delete account"), "Delete account");
    // A currency mark (symbol, ISO code, token in any case, Cyrillic word) or a '#' turns it off.
    EXPECT_EQ(NormalizeLiveCounters("Pay $5 (1)"), "Pay $5 (1)");
    EXPECT_EQ(NormalizeLiveCounters("Transfer 10 usd"), "Transfer 10 usd");
    EXPECT_EQ(NormalizeLiveCounters("Pay 5 kr"), "Pay 5 kr");
    EXPECT_EQ(NormalizeLiveCounters("Pay 5 €"), "Pay 5 €");
    EXPECT_EQ(NormalizeLiveCounters("Pay 5 ₽ (2)"), "Pay 5 ₽ (2)");
    EXPECT_EQ(NormalizeLiveCounters("Оплатить 500 руб (1)"), "Оплатить 500 руб (1)");
    EXPECT_EQ(NormalizeLiveCounters("Order # (#)"), "Order # (#)");
    // Edge cases the page copy must match too (facts_probe.cjs).
    EXPECT_EQ(NormalizeLiveCounters("1:02:034"), "#:034");
    EXPECT_EQ(NormalizeLiveCounters("1:00:2:00"), "#:#");
    // A non-ASCII digit touching a time run is not a boundary: the run stays as it is.
    EXPECT_EQ(NormalizeLiveCounters("Pay ５0:42"), "Pay ５0:42");
    EXPECT_EQ(NormalizeLiveCounters("Pay 0:42５"), "Pay 0:42５");
    EXPECT_EQ(NormalizeLiveCounters("Pay 0:42:5５"), "Pay 0:42:5５");
    // Letters that only contain a token are not the token.
    EXPECT_EQ(NormalizeLiveCounters("Usdx 0:42"), "Usdx #");
    EXPECT_EQ(NormalizeLiveCounters("Okra 0:42"), "Okra #");
}

TEST(BrowserToolsTest, LiveLabelsMatchOnlyWhatTheRuleAllows) {
    for (const LiveCase& c : kLiveCases) {
        EXPECT_EQ(LiveLabelsMatch(c.before, c.after), c.match) << c.before << " -> " << c.after;
        EXPECT_EQ(LiveLabelsMatch(c.after, c.before), c.match) << c.after << " -> " << c.before;
    }
    // A literal '#' must not collide with a normalized counter, in either direction.
    EXPECT_FALSE(LiveLabelsMatch("Order 1:00 (5)", "Order # (#)"));
    EXPECT_FALSE(LiveLabelsMatch("Order # (#)", "Order 1:00 (5)"));
    // Accepted debt (src/agent/AGENTS.md): a timer and a counter in parentheses both become "(#)".
    EXPECT_TRUE(LiveLabelsMatch("Retry (0:42)", "Retry (42)"));
}

// The button's label as the page reports it: a submit control on a.test.
std::string ButtonFacts(const std::string& field, const std::string& value) {
    return std::string(R"({"ok":true,"x":10,"y":20,"facts":{"tag":"button",")") + field + R"(":")" +
           value + R"(","is_submit":true,"page_origin":"https://a.test"}})";
}

TEST(BrowserToolsTest, LiveCountersPressAfterTheCard) {
    for (const LiveCase& c : kLiveCases) {
        if (!c.match) continue;
        FakeBrowserHost host;
        BrowserToolbox toolbox(host);
        host.QueueScriptResult(ButtonFacts("text", c.before));
        host.QueueScriptResult(ButtonFacts("text", c.after));
        host.answers.push_back(ConfirmationAnswer::kAllowOnce);
        EXPECT_FALSE(
            CallTool(toolbox, "page_click", Value::MakeObject().Set("ref", Value::Int(1))).is_error)
            << c.before << " -> " << c.after;
        EXPECT_EQ(CountMethod(host, "Input.dispatchMouseEvent"), 3) << c.before;
    }
    FakeBrowserHost host;
    BrowserToolbox toolbox(host);
    host.QueueScriptResult(ButtonFacts("aria_label", "Like (12)"));
    host.QueueScriptResult(ButtonFacts("aria_label", "Like (13)"));
    host.answers.push_back(ConfirmationAnswer::kAllowOnce);
    EXPECT_FALSE(
        CallTool(toolbox, "page_click", Value::MakeObject().Set("ref", Value::Int(1))).is_error);
    EXPECT_EQ(CountMethod(host, "Input.dispatchMouseEvent"), 3);
}

TEST(BrowserToolsTest, ChangedAmountOrCountNeverClicksAfterTheCard) {
    // The person approved one amount or count; the page now shows another. Nothing may be clicked.
    for (const LiveCase& c : kLiveCases) {
        if (c.match) continue;
        FakeBrowserHost host;
        BrowserToolbox toolbox(host);
        host.QueueScriptResult(ButtonFacts("text", c.before));
        host.QueueScriptResult(ButtonFacts("text", c.after));
        host.answers.push_back(ConfirmationAnswer::kAllowOnce);
        const ToolResult r =
            CallTool(toolbox, "page_click", Value::MakeObject().Set("ref", Value::Int(1)));
        EXPECT_TRUE(r.is_error) << c.before << " -> " << c.after;
        EXPECT_EQ(CountMethod(host, "Input.dispatchMouseEvent"), 0)
            << c.before << " -> " << c.after;
    }
}

TEST(BrowserToolsTest, CountdownThatBecomesAnotherActionIsDropped) {
    FakeBrowserHost host;
    BrowserToolbox toolbox(host);
    host.QueueScriptResult(ButtonFacts("text", "Resend in 0:42"));
    host.QueueScriptResult(ButtonFacts("text", "Delete account 0:41"));
    host.answers.push_back(ConfirmationAnswer::kAllowOnce);
    const ToolResult r =
        CallTool(toolbox, "page_click", Value::MakeObject().Set("ref", Value::Int(1)));
    EXPECT_TRUE(r.is_error);
    EXPECT_EQ(r.content.at(0).text, "The page changed while waiting; take a fresh snapshot.");
    EXPECT_EQ(CountMethod(host, "Input.dispatchMouseEvent"), 0);
}

TEST(BrowserToolsTest, RecheckSeesATrustedSiteAddedDuringTheCard) {
    // Trusting the site during the card changes the fresh decision to free; the re-check accepts it
    // and the click goes through. (A fresh decision that needs a new rule is the failing direction
    // of StillApproved; no fact outside SameElement's set can make that happen, so it is untested.)
    FakeBrowserHost host;
    BrowserToolbox toolbox(host);
    host.QueueScriptResult(ButtonFacts("text", "Pay"));
    host.QueueScriptResult(ButtonFacts("text", "Pay"));
    host.answers.push_back(ConfirmationAnswer::kTrustSite);
    EXPECT_FALSE(
        CallTool(toolbox, "page_click", Value::MakeObject().Set("ref", Value::Int(1))).is_error);
    EXPECT_EQ(CountMethod(host, "Input.dispatchMouseEvent"), 3);
    ASSERT_EQ(host.trusted.size(), 1u);
    EXPECT_EQ(host.trusted.front(), "a.test");
}

TEST(BrowserToolsTest, DigitOnlyChangeInNameOrIdStillFailsTheRecheck) {
    // The label is the same; only a field the gate compares changes its digits.
    for (const std::string field : {"name", "id"}) {
        FakeBrowserHost host;
        BrowserToolbox toolbox(host);
        const auto facts = [&field](const std::string& value) {
            return R"({"ok":true,"x":10,"y":20,"facts":{"tag":"button",")" + field + R"(":")" +
                   value + R"(","is_submit":true,"page_origin":"https://a.test"}})";
        };
        host.QueueScriptResult(facts("item1"));
        host.QueueScriptResult(facts("item2"));
        host.answers.push_back(ConfirmationAnswer::kAllowOnce);
        const ToolResult r =
            CallTool(toolbox, "page_click", Value::MakeObject().Set("ref", Value::Int(1)));
        EXPECT_TRUE(r.is_error) << field;
        EXPECT_EQ(CountMethod(host, "Input.dispatchMouseEvent"), 0) << field;
    }
}

}  // namespace
}  // namespace island::agent
