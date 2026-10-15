// The CEF view types must be complete before browser_window.h (see browser_window_test.cpp).
#include "browser_window.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "browser_chrome.h"
#include "browser_command.h"
#include "include/views/cef_overlay_controller.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_window.h"
#include "json_util.h"
#include "keymap.h"
#include "reader_mode.h"
#include "space.h"
#include "tab.h"

namespace island {
namespace {

constexpr std::string_view kArticle = R"({"ok":true,"title":"A <b>title</b>","byline":"Ada",
"site":"Example","lang":"en","url":"https://example.com/a","published":"2026-03-04",
"words":321,"truncated":false,"content":[["p",null,"Hello </script><script>alert(1)</script> world"],
["h2",null,"Section"],["ul",null,["li",null,"one"],["li",null,"two"]],
["img",{"src":"https://example.com/i.png","alt":"x"}]]})";

TEST(ReaderModeTest, Base64MatchesTheRfcVectors) {
    EXPECT_EQ(Base64Encode(""), "");
    EXPECT_EQ(Base64Encode("f"), "Zg==");
    EXPECT_EQ(Base64Encode("fo"), "Zm8=");
    EXPECT_EQ(Base64Encode("foo"), "Zm9v");
    EXPECT_EQ(Base64Encode("foob"), "Zm9vYg==");
    EXPECT_EQ(Base64Encode("fooba"), "Zm9vYmE=");
    EXPECT_EQ(Base64Encode("foobar"), "Zm9vYmFy");
}

TEST(ReaderModeTest, SettingsNormalizeToTheSupportedChoices) {
    const ReaderSettings settings = ReaderSettings::Normalized(99, "huge", "neon");
    EXPECT_EQ(settings.size, ReaderSettings::kMaxSize);
    EXPECT_EQ(settings.width, "medium");
    EXPECT_EQ(settings.theme, "auto");
    EXPECT_EQ(ReaderSettings::Normalized(2, "wide", "sepia").size, ReaderSettings::kMinSize);
    EXPECT_EQ(ReaderSettings::Normalized(0, "narrow", "dark").size, ReaderSettings::kDefaultSize);
    const ReaderSettings valid = ReaderSettings::Normalized(20, "narrow", "dark");
    EXPECT_EQ(valid, (ReaderSettings{.size = 20, .width = "narrow", .theme = "dark"}));
    EXPECT_TRUE(IsReaderTheme("sepia"));
    EXPECT_FALSE(IsReaderTheme("solarized"));
}

TEST(ReaderModeTest, OnlyWebPagesAreEligible) {
    EXPECT_TRUE(IsReaderEligibleUrl("https://en.wikipedia.org/wiki/Web_browser"));
    EXPECT_TRUE(IsReaderEligibleUrl("http://example.com/"));
    EXPECT_FALSE(IsReaderEligibleUrl("data:text/html,x"));
    EXPECT_FALSE(IsReaderEligibleUrl("file:///etc/hosts"));
    EXPECT_FALSE(IsReaderEligibleUrl(""));
}

TEST(ReaderModeTest, ExtractionExpressionIsTheEmbeddedScriptPlusACall) {
    const std::string expression = ReaderExtractionExpression();
    EXPECT_NE(expression.find("function islandExtractArticle(doc, win)"), std::string::npos);
    EXPECT_NE(expression.rfind("JSON.stringify(islandExtractArticle(document, window));"),
              std::string::npos);
    // No network access of any kind: the script is the only code the page ever runs for Island.
    for (std::string_view forbidden : {"fetch(", "XMLHttpRequest", "WebSocket", "import("}) {
        EXPECT_EQ(expression.find(forbidden), std::string::npos) << forbidden;
    }
}

TEST(ReaderModeTest, ParseRejectsMalformedAndNoArticleResults) {
    EXPECT_FALSE(ParseReaderArticle("not json"));
    EXPECT_FALSE(ParseReaderArticle(R"({"ok":false,"reason":"no_article"})"));
    EXPECT_FALSE(ParseReaderArticle(R"({"ok":true,"content":[]})"));
    EXPECT_FALSE(ParseReaderArticle(R"({"ok":true})"));
    const auto article = ParseReaderArticle(kArticle);
    ASSERT_TRUE(article.has_value());
    EXPECT_EQ(article->title(), "A <b>title</b>");
}

TEST(ReaderModeTest, OversizedArticlesAreCutToFitADataUrl) {
    json::Value content = json::Value::MakeArray();
    const std::string block(100'000, 'x');
    for (int i = 0; i < 30; ++i) {
        content.Push(json::Value::MakeArray().Push(json::Value::String("p"))
                         .Push(json::Value::Null())
                         .Push(json::Value::String(block)));
    }
    json::Value root = json::Value::MakeObject();
    root.Set("ok", json::Value::Bool(true));
    root.Set("title", json::Value::String("Long"));
    root.Set("content", std::move(content));
    const auto article = ParseReaderArticle(json::Serialize(root));
    ASSERT_TRUE(article.has_value());
    EXPECT_TRUE(article->value.BoolOr("truncated", false));
    const json::Value* kept = article->value.FindMember("content");
    ASSERT_NE(kept, nullptr);
    EXPECT_LT(kept->array_val.size(), 30U);
    EXPECT_GT(kept->array_val.size(), 5U);
    EXPECT_LE(json::Serialize(article->value).size(), kMaxReaderPageBytes);
}

TEST(ReaderModeTest, DocumentEmbedsTheArticleWithoutLettingItCloseTheScript) {
    const auto article = ParseReaderArticle(kArticle);
    ASSERT_TRUE(article.has_value());
    const std::string html =
        BuildReaderHtml(&*article, "", "tok123", ReaderSettings{}, /*app_dark=*/true);
    EXPECT_EQ(html.find("__ISLAND_READER_DATA__"), std::string::npos);
    EXPECT_NE(html.find("tok123"), std::string::npos);
    EXPECT_NE(html.find("\"app_theme\":\"dark\""), std::string::npos);
    // The article text carries a literal </script>; inside the JSON block it must be escaped.
    const std::size_t block = html.find("id=\"island-reader-data\">");
    ASSERT_NE(block, std::string::npos);
    const std::size_t end = html.find("</script>", block);
    ASSERT_NE(end, std::string::npos);
    const std::string data = html.substr(block, end - block);
    EXPECT_EQ(data.find("<"), std::string_view::npos);
    EXPECT_NE(data.find("\\u003c/script\\u003e"), std::string::npos);
    // And it still parses back to the same article.
    const std::size_t json_start = data.find('>') + 1;
    const auto parsed = json::Parse(std::string_view(data).substr(json_start));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->StringOr("token", ""), "tok123");
    ASSERT_NE(parsed->FindMember("article"), nullptr);
    EXPECT_EQ(parsed->FindMember("article")->StringOr("title", ""), "A <b>title</b>");
}

TEST(ReaderModeTest, ErrorDocumentCarriesTheMessageAndNoArticle) {
    const std::string html =
        BuildReaderHtml(nullptr, "No article here", "t", ReaderSettings{}, /*app_dark=*/false);
    EXPECT_NE(html.find("No article here"), std::string::npos);
    EXPECT_NE(html.find("\"article\":null"), std::string::npos);
    EXPECT_NE(html.find("\"app_theme\":\"light\""), std::string::npos);
}

TEST(ReaderModeTest, DataUrlIsBase64OfTheDocument) {
    EXPECT_EQ(ReaderDataUrl("foobar"), "data:text/html;charset=utf-8;base64,Zm9vYmFy");
}

TEST(ReaderModeTest, PageTemplateFollowsTheGraphiteContract) {
    const std::string html = BuildReaderHtml(nullptr, "", "t", ReaderSettings{}, false);
    for (std::string_view needle :
         {"Geist, ui-sans-serif", "\"Geist Mono\"", "data-theme=\"sepia\"", "--accent: #0068D6",
          "window.islandRender", "reader_settings", "reader_exit", "narrow", "medium", "wide",
          "Original page"}) {
        EXPECT_NE(html.find(needle), std::string::npos) << needle;
    }
}

// --- BrowserWindow wiring (headless) -------------------------------------------------------

// Stands in for CefParseURL, which is unavailable headless.
ValidatedAddress AcceptHttps(std::string_view text) {
    if (text.substr(0, 8) != "https://" || text.find(' ') != std::string_view::npos) {
        return {.url = {}, .error = AddressError::kNotAbsolute};
    }
    return {.url = std::string(text), .error = std::nullopt};
}

CefRefPtr<BrowserWindow> MakeWebWindow() {
    CefRefPtr<BrowserWindow> window = BrowserWindow::CreateHeadlessForTest("data:text/html,Island");
    window->SetAddressValidatorForTest(&AcceptHttps);
    EXPECT_TRUE(window->OpenNewTab("https://example.com/article"));
    return window;
}

TEST(ReaderModeWindowTest, ShowingTheReaderDocumentMakesTheTabReaderActive) {
    CefRefPtr<BrowserWindow> window = MakeWebWindow();
    const TabId tab = window->active_tab_id();
    EXPECT_FALSE(window->ReaderActive(tab));
    const std::string url = window->ShowReaderDocument(
        tab, "https://example.com/article", std::string(kArticle), "");
    ASSERT_FALSE(url.empty());
    EXPECT_EQ(url.rfind("data:text/html;charset=utf-8;base64,", 0), 0U);
    EXPECT_TRUE(window->ReaderActive(tab));
    EXPECT_EQ(window->navigation_snapshot().url, url);
}

TEST(ReaderModeWindowTest, ANoArticleResultStillShowsAnExplainingReaderPage) {
    CefRefPtr<BrowserWindow> window = MakeWebWindow();
    const TabId tab = window->active_tab_id();
    const std::string url = window->ShowReaderDocument(
        tab, "https://example.com/article", std::string(R"({"ok":false,"reason":"no_article"})"),
        "No article was found.");
    EXPECT_FALSE(url.empty());
    EXPECT_TRUE(window->ReaderActive(tab));
}

TEST(ReaderModeWindowTest, TogglingWithoutTheDevToolsBridgeLeavesThePageAlone) {
    CefRefPtr<BrowserWindow> window = MakeWebWindow();
    const TabId tab = window->active_tab_id();
    window->ExecuteCommand(BrowserCommand::kToggleReaderMode);
    EXPECT_FALSE(window->ReaderActive(tab));
    EXPECT_EQ(window->navigation_snapshot().url.rfind("data:", 0), std::string::npos);
}

TEST(ReaderModeWindowTest, TheReaderKeyActionRunsTheToggleCommand) {
    CefRefPtr<BrowserWindow> window = MakeWebWindow();
    window->RunKeyAction(KeyAction::kReaderMode);  // must not crash headless
    EXPECT_FALSE(window->ReaderActive(window->active_tab_id()));
}

TEST(FindWindowTest, FindKeyActionsDriveTheFindBarModel) {
    CefRefPtr<BrowserWindow> window = MakeWebWindow();
    EXPECT_FALSE(window->find_bar_model().open());
    window->RunKeyAction(KeyAction::kFindInPage);
    EXPECT_TRUE(window->find_bar_model().open());
    window->FindInPage("needle");
    EXPECT_EQ(window->find_bar_model().query(), "needle");
    EXPECT_TRUE(window->find_bar_model().searched());
    window->RunKeyAction(KeyAction::kFindNext);
    window->RunKeyAction(KeyAction::kFindPrevious);
    EXPECT_TRUE(window->find_bar_model().open());
}

TEST(FindWindowTest, SwitchingTabsClosesTheFindBarAndKeepsTheQuery) {
    CefRefPtr<BrowserWindow> window = MakeWebWindow();
    window->FindInPage("needle");
    ASSERT_TRUE(window->find_bar_model().open());
    window->ExecuteCommand(BrowserCommand::kNewTab);
    EXPECT_FALSE(window->find_bar_model().open());
    EXPECT_EQ(window->find_bar_model().query(), "needle");
}

TEST(FindWindowTest, FindNextWithTheBarClosedReopensIt) {
    CefRefPtr<BrowserWindow> window = MakeWebWindow();
    window->RunKeyAction(KeyAction::kFindNext);
    EXPECT_TRUE(window->find_bar_model().open());
}

}  // namespace
}  // namespace island
