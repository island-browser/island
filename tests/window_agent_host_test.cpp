#include "window_agent_host.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>

#include "agent_navigation.h"
#include "browser_window.h"
#include "include/views/cef_overlay_controller.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_window.h"
#include "json_util.h"
#include "space.h"

// Headless coverage of the agent seams: the tools agents call resolve to the
// same window model mutations the UI uses. No CefBrowser exists here, so
// page-level tools must fail cleanly with an actionable message.

namespace island {
namespace {

using json::Value;

// Stand-in for ParseAndValidate (CefParseURL is unavailable headless):
// absolute http(s) URLs without whitespace are valid.
ValidatedAddress FakeValidate(std::string_view text) {
    const bool scheme = text.substr(0, 7) == "http://" || text.substr(0, 8) == "https://";
    if (!scheme || text.find(' ') != std::string_view::npos) {
        return {.url = {}, .error = AddressError::kNotAbsolute};
    }
    return {.url = std::string(text), .error = std::nullopt};
}

CefRefPtr<BrowserWindow> MakeWindow() {
    CefRefPtr<BrowserWindow> window = BrowserWindow::CreateHeadlessForTest("data:text/html,Island");
    window->SetAddressValidatorForTest(&FakeValidate);
    return window;
}

agent::ToolResult Call(WindowAgentHost& host, std::string_view tool,
                       Value args = Value::MakeObject()) {
    std::optional<agent::ToolResult> result;
    host.toolbox().Call(tool, args, [&](agent::ToolResult r) { result = std::move(r); });
    EXPECT_TRUE(result.has_value()) << tool;
    return result.value_or(agent::ToolResult::Error("no result"));
}

const Space& ActiveSpace(const BrowserWindow& window) {
    return window.spaces()[window.active_space_index()];
}

TEST(AgentNavigationTest, ResolvesUrlsHostsAndSearches) {
    EXPECT_EQ(ResolveAgentNavigation("https://a.test/x", &FakeValidate), "https://a.test/x");
    EXPECT_EQ(ResolveAgentNavigation("  example.com/docs ", &FakeValidate),
              "https://example.com/docs");
    EXPECT_EQ(ResolveAgentNavigation("localhost:3000", &FakeValidate), "http://localhost:3000");
    EXPECT_EQ(ResolveAgentNavigation("rust borrow checker", &FakeValidate),
              "https://www.google.com/search?q=rust%20borrow%20checker");
    EXPECT_EQ(ResolveAgentNavigation("   ", &FakeValidate), std::nullopt);
}

TEST(WindowAgentHostTest, ListsTheWindowModel) {
    CefRefPtr<BrowserWindow> window = MakeWindow();
    WindowAgentHost host(*window);
    const Value tabs = *json::Parse(Call(host, "browser_list_tabs").content[0].text);
    EXPECT_EQ(tabs.StringOr("space", ""), "Default");
    ASSERT_EQ(tabs.FindMember("tabs")->array_val.size(), 1U);
    EXPECT_TRUE(tabs.FindMember("tabs")->array_val[0].BoolOr("active", false));

    const Value spaces = *json::Parse(Call(host, "browser_list_spaces").content[0].text);
    ASSERT_EQ(spaces.FindMember("spaces")->array_val.size(), 1U);
    EXPECT_EQ(spaces.FindMember("spaces")->array_val[0].IntOr("tab_count", 0), 1);
}

TEST(WindowAgentHostTest, OpenTabResolvesHostsAndSearchText) {
    CefRefPtr<BrowserWindow> window = MakeWindow();
    WindowAgentHost host(*window);
    EXPECT_FALSE(
        Call(host, "browser_open_tab", Value::MakeObject().Set("url", Value::String("example.com")))
            .is_error);
    EXPECT_FALSE(Call(host, "browser_open_tab",
                      Value::MakeObject().Set("url", Value::String("island browser")))
                     .is_error);
    const Space& space = ActiveSpace(*window);
    ASSERT_EQ(space.tab_count(), 3U);
    EXPECT_EQ(space.tabs()[1].startup_url(), "https://example.com");
    EXPECT_EQ(space.tabs()[2].startup_url(), "https://www.google.com/search?q=island%20browser");
    EXPECT_EQ(space.active_tab_index(), 2U);
}

TEST(WindowAgentHostTest, NavigateUpdatesTheTargetTab) {
    CefRefPtr<BrowserWindow> window = MakeWindow();
    WindowAgentHost host(*window);
    window->ExecuteCommand(BrowserCommand::kNewTab);
    EXPECT_FALSE(Call(host, "browser_navigate",
                      Value::MakeObject()
                          .Set("url", Value::String("https://docs.test/"))
                          .Set("tab", Value::Int(0)))
                     .is_error);
    EXPECT_EQ(ActiveSpace(*window).tabs()[0].startup_url(), "https://docs.test/");
    EXPECT_TRUE(Call(host, "browser_navigate",
                     Value::MakeObject()
                         .Set("url", Value::String("https://docs.test/"))
                         .Set("tab", Value::Int(7)))
                    .is_error);
}

TEST(WindowAgentHostTest, TabAndSpaceToolsDriveTheModel) {
    CefRefPtr<BrowserWindow> window = MakeWindow();
    WindowAgentHost host(*window);
    window->ExecuteCommand(BrowserCommand::kNewTab);
    EXPECT_FALSE(
        Call(host, "browser_activate_tab", Value::MakeObject().Set("tab", Value::Int(0))).is_error);
    EXPECT_EQ(ActiveSpace(*window).active_tab_index(), 0U);
    EXPECT_FALSE(
        Call(host, "browser_close_tab", Value::MakeObject().Set("tab", Value::Int(1))).is_error);
    EXPECT_EQ(ActiveSpace(*window).tab_count(), 1U);

    EXPECT_FALSE(
        Call(host, "browser_new_space", Value::MakeObject().Set("name", Value::String("Research")))
            .is_error);
    EXPECT_EQ(window->space_count(), 2U);
    EXPECT_EQ(ActiveSpace(*window).name(), "Research");
    EXPECT_FALSE(Call(host, "browser_switch_space", Value::MakeObject().Set("space", Value::Int(0)))
                     .is_error);
    EXPECT_EQ(window->active_space_index(), 0U);
    EXPECT_TRUE(Call(host, "browser_switch_space", Value::MakeObject().Set("space", Value::Int(5)))
                    .is_error);
}

TEST(WindowAgentHostTest, PageToolsWithoutALiveBrowserExplainWhatToDo) {
    CefRefPtr<BrowserWindow> window = MakeWindow();
    WindowAgentHost host(*window);
    const agent::ToolResult read = Call(host, "page_read");
    EXPECT_TRUE(read.is_error);
    EXPECT_NE(read.content[0].text.find("no live page"), std::string::npos);
    const agent::ToolResult other =
        Call(host, "page_snapshot", Value::MakeObject().Set("tab", Value::Int(3)));
    EXPECT_TRUE(other.is_error);
}

TEST(WindowAgentHostTest, ShutdownIsIdempotent) {
    CefRefPtr<BrowserWindow> window = MakeWindow();
    WindowAgentHost host(*window);
    host.Shutdown();
    host.Shutdown();
    EXPECT_EQ(host.endpoint(), nullptr);
}

TEST(WindowAgentHostTest, PinnedTabsGroupAtTheFront) {
    CefRefPtr<BrowserWindow> window = MakeWindow();
    window->ExecuteCommand(BrowserCommand::kNewTab);
    window->ExecuteCommand(BrowserCommand::kNewTab);
    const TabId last = ActiveSpace(*window).tabs()[2].id();
    ASSERT_TRUE(window->SetActiveSpaceTabPinned(2, true));
    EXPECT_EQ(ActiveSpace(*window).tabs()[0].id(), last);
    EXPECT_TRUE(ActiveSpace(*window).tabs()[0].pinned());
    EXPECT_EQ(ActiveSpace(*window).active_tab_id(), last);
    EXPECT_EQ(ActiveSpace(*window).pinned_count(), 1U);
    EXPECT_FALSE(window->SetActiveSpaceTabPinned(9, true));

    WindowAgentHost host(*window);
    const Value tabs = *json::Parse(Call(host, "browser_list_tabs").content[0].text);
    EXPECT_TRUE(tabs.FindMember("tabs")->array_val[0].BoolOr("pinned", false));
}

}  // namespace
}  // namespace island
