#include "local_page.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

#include "browser_window.h"
#include "include/views/cef_overlay_controller.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_window.h"
#include "json_util.h"
#include "local_pages_html.h"

namespace island {
namespace {

using json::Value;

TEST(LocalPageTest, DecodesOnlyPrefixedObjectMessagesWithAType) {
    const auto message = LocalPage::DecodeMessage(std::string(LocalPage::kMessagePrefix) +
                                                  R"({"type":"send","text":"hi"})");
    ASSERT_TRUE(message.has_value());
    EXPECT_EQ(message->StringOr("text", ""), "hi");
    EXPECT_FALSE(LocalPage::DecodeMessage(R"({"type":"send"})").has_value());
    EXPECT_FALSE(LocalPage::DecodeMessage(std::string(LocalPage::kMessagePrefix) + "[1]"));
    EXPECT_FALSE(LocalPage::DecodeMessage(std::string(LocalPage::kMessagePrefix) + "{}"));
    EXPECT_FALSE(LocalPage::DecodeMessage(std::string(LocalPage::kMessagePrefix) + "{oops"));
}

TEST(LocalPageTest, RenderScriptEscapesLineSeparators) {
    const std::string script = LocalPage::RenderScript("{\"text\":\"a\xE2\x80\xA8z\xE2\x80\xA9\"}");
    EXPECT_EQ(script.find("\xE2\x80\xA8"), std::string::npos);
    EXPECT_NE(script.find("a\\u2028z\\u2029"), std::string::npos);
    EXPECT_EQ(script.rfind("window.islandRender && window.islandRender(", 0), 0U);
}

TEST(LocalPageTest, TheEmbeddedPageCarriesTheBridgeContract) {
    const std::string html(AgentPageHtml());
    EXPECT_EQ(html.rfind("<!doctype html>", 0), 0U);
    EXPECT_NE(html.find("window.islandRender = function"), std::string::npos);
    EXPECT_NE(html.find("window.islandFocus ="), std::string::npos);
    // The page's message prefix must match the native decoder's.
    EXPECT_NE(html.find("\"\\u0001island:\""), std::string::npos);
    // No network fetches from the panel: everything is inline.
    EXPECT_EQ(html.find("<script src"), std::string::npos);
    EXPECT_EQ(html.find("<link"), std::string::npos);
}

TEST(LocalPageTest, HeadlessWindowHandlesPanelMessagesWithoutChrome) {
    CefRefPtr<BrowserWindow> window = BrowserWindow::CreateHeadlessForTest("data:text/html,x");
    window->SetAddressValidatorForTest([](std::string_view text) {
        return text.rfind("https://", 0) == 0
                   ? ValidatedAddress{.url = std::string(text), .error = std::nullopt}
                   : ValidatedAddress{.url = {}, .error = AddressError::kNotAbsolute};
    });
    // No chrome: the panel cannot open, and that is a no-op, not a crash.
    window->ToggleAgentPanel();
    EXPECT_FALSE(window->agent_panel_open());

    window->OnLocalPageMessage(LocalPageKind::kAgent,
                               Value::MakeObject()
                                   .Set("type", Value::String("open_url"))
                                   .Set("url", Value::String("https://linked.test/")));
    EXPECT_EQ(window->spaces()[0].tab_count(), 2U);
    EXPECT_EQ(window->spaces()[0].tabs()[1].startup_url(), "https://linked.test/");

    // A start with a command that cannot launch reports through the session
    // instead of throwing; unknown message types are ignored.
    window->OnLocalPageMessage(LocalPageKind::kAgent,
                               Value::MakeObject()
                                   .Set("type", Value::String("start"))
                                   .Set("command", Value::String("\"unbalanced")));
    window->OnLocalPageMessage(LocalPageKind::kAgent,
                               Value::MakeObject().Set("type", Value::String("bogus")));
}

TEST(LocalPageTest, AgentCommandResolutionPrefersTheEnvironment) {
    CefRefPtr<BrowserWindow> window = BrowserWindow::CreateHeadlessForTest("data:text/html,x");
    const char* saved = std::getenv("ISLAND_AGENT_COMMAND");
    const std::string saved_value = saved != nullptr ? saved : "";
#if defined(_WIN32)
    _putenv_s("ISLAND_AGENT_COMMAND", "");
#else
    unsetenv("ISLAND_AGENT_COMMAND");
#endif
    EXPECT_EQ(window->ResolvedAgentCommand(), BrowserWindow::kDefaultAgentCommand);
#if !defined(_WIN32)
    setenv("ISLAND_AGENT_COMMAND", "gemini --experimental-acp", 1);
    EXPECT_EQ(window->ResolvedAgentCommand(), "gemini --experimental-acp");
    if (saved != nullptr) {
        setenv("ISLAND_AGENT_COMMAND", saved_value.c_str(), 1);
    } else {
        unsetenv("ISLAND_AGENT_COMMAND");
    }
#endif
}

TEST(LocalPageTest, EveryBuiltInPageSpeaksTheSameBridge) {
    for (const LocalPageKind kind :
         {LocalPageKind::kAgent, LocalPageKind::kSettings, LocalPageKind::kTabOverview}) {
        const std::string html(LocalPageHtml(kind));
        EXPECT_EQ(html.rfind("<!doctype html>", 0), 0U);
        EXPECT_NE(html.find("window.islandRender = function"), std::string::npos);
        EXPECT_NE(html.find("window.islandFocus = function"), std::string::npos);
        EXPECT_NE(html.find("\"\\u0001island:\""), std::string::npos);
        EXPECT_NE(html.find("post({ type: \"ready\" })"), std::string::npos);
        EXPECT_EQ(html.find("<script src"), std::string::npos);
        // Pages stay ASCII; escapes carry any symbol.
        for (const char c : html) {
            EXPECT_LT(static_cast<unsigned char>(c), 0x80U);
            if (static_cast<unsigned char>(c) >= 0x80U) break;
        }
    }
}

}  // namespace
}  // namespace island
