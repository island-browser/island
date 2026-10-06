// Headless coverage of the Settings / All-tabs / import / shortcut seams of
// BrowserWindow. The CEF view types must be complete before browser_window.h.
#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "browser_window.h"
#include "include/views/cef_overlay_controller.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_window.h"
#include "json_util.h"
#include "keymap.h"

namespace island {
namespace {

using json::Value;

ValidatedAddress FakeValidate(std::string_view text) {
    const bool web = text.rfind("https://", 0) == 0 || text.rfind("http://", 0) == 0;
    if (!web || text.find(' ') != std::string_view::npos) {
        return {.url = {}, .error = AddressError::kNotAbsolute};
    }
    return {.url = std::string(text), .error = std::nullopt};
}

CefRefPtr<BrowserWindow> MakeWindow() {
    CefRefPtr<BrowserWindow> window = BrowserWindow::CreateHeadlessForTest("data:text/html,x");
    window->SetAddressValidatorForTest(&FakeValidate);
    return window;
}

const Space& SpaceAt(const BrowserWindow& window, std::size_t index) {
    return window.spaces()[index];
}

TEST(BrowserWindowPagesTest, KeymapEditsTakeEffectAndShortcutsRunActions) {
    CefRefPtr<BrowserWindow> window = MakeWindow();
    EXPECT_EQ(window->keymap(), Keymap::Defaults());
    ASSERT_TRUE(window->SetKeyBinding(KeyAction::kNewTab, ParseKeyBinding("Mod+Shift+Y")));
    EXPECT_EQ(window->keymap().ActionFor(*ParseKeyBinding("Mod+Shift+Y")), KeyAction::kNewTab);
    ASSERT_TRUE(window->SetKeyBinding(KeyAction::kCloseTab, std::nullopt));
    EXPECT_FALSE(window->keymap().Binding(KeyAction::kCloseTab).has_value());
    ASSERT_TRUE(window->ResetKeyBinding(KeyAction::kNewTab));
    EXPECT_EQ(window->keymap().Binding(KeyAction::kNewTab), ParseKeyBinding("Mod+T"));
    window->ResetKeymap();
    EXPECT_EQ(window->keymap(), Keymap::Defaults());

    window->RunKeyAction(KeyAction::kNewTab);
    EXPECT_EQ(SpaceAt(*window, 0).tab_count(), 2U);
    window->RunKeyAction(KeyAction::kTogglePinTab);
    EXPECT_EQ(SpaceAt(*window, 0).pinned_count(), 1U);
    window->RunKeyAction(KeyAction::kNewSpace);
    EXPECT_EQ(window->space_count(), 2U);
    // Page toggles are no-ops without chrome, never crashes.
    window->RunKeyAction(KeyAction::kSettings);
    window->RunKeyAction(KeyAction::kTabOverview);
    EXPECT_FALSE(window->internal_page().has_value());
}

TEST(BrowserWindowPagesTest, SettingsStateListsShortcutsSourcesAndConflicts) {
    CefRefPtr<BrowserWindow> window = MakeWindow();
    ASSERT_TRUE(window->SetKeyBinding(KeyAction::kSettings, ParseKeyBinding("Mod+T")));
    const auto state = json::Parse(window->SettingsStateJson());
    ASSERT_TRUE(state.has_value());
    const Value* settings = state->FindMember("settings");
    ASSERT_NE(settings, nullptr);
    EXPECT_EQ(settings->StringOr("theme_pref", ""), "system");
    const Value* shortcuts = settings->FindMember("shortcuts");
    ASSERT_EQ(shortcuts->array_val.size(), kKeyActionCount);
    int conflicts = 0;
    for (const Value& row : shortcuts->array_val) {
        if (row.BoolOr("conflict", false)) ++conflicts;
        if (row.StringOr("id", "") == "settings") {
            EXPECT_EQ(row.StringOr("binding", ""), "Mod+T");
            EXPECT_EQ(row.StringOr("default_binding", ""), "Mod+,");
        }
    }
    EXPECT_EQ(conflicts, 2);  // new_tab and settings
    const Value* sources = settings->FindMember("import")->FindMember("sources");
    bool has_arc = false;
    for (const Value& source : sources->array_val) {
        if (source.StringOr("id", "") == "arc") has_arc = true;
    }
    EXPECT_TRUE(has_arc);
    EXPECT_FALSE(settings->FindMember("endpoint")->BoolOr("running", true));
    EXPECT_NE(state->FindMember("theme"), nullptr);
}

TEST(BrowserWindowPagesTest, SettingsMessagesUpdateTheWindow) {
    CefRefPtr<BrowserWindow> window = MakeWindow();
    window->OnLocalPageMessage(LocalPageKind::kSettings,
                               Value::MakeObject()
                                   .Set("type", Value::String("set_theme"))
                                   .Set("value", Value::String("dark")));
    EXPECT_EQ(window->theme_preference(), ThemePreference::kDark);
    window->OnLocalPageMessage(LocalPageKind::kSettings,
                               Value::MakeObject()
                                   .Set("type", Value::String("set_binding"))
                                   .Set("action", Value::String("new_tab"))
                                   .Set("binding", Value::String("Mod+Alt+T")));
    EXPECT_EQ(window->keymap().Binding(KeyAction::kNewTab), ParseKeyBinding("Mod+Alt+T"));
    // An invalid binding leaves the shortcut alone and surfaces a notice.
    window->OnLocalPageMessage(LocalPageKind::kSettings,
                               Value::MakeObject()
                                   .Set("type", Value::String("set_binding"))
                                   .Set("action", Value::String("new_tab"))
                                   .Set("binding", Value::String("T")));
    EXPECT_EQ(window->keymap().Binding(KeyAction::kNewTab), ParseKeyBinding("Mod+Alt+T"));
    EXPECT_NE(
        json::Parse(window->SettingsStateJson())->FindMember("settings")->StringOr("message", ""),
        "");
    window->OnLocalPageMessage(LocalPageKind::kSettings,
                               Value::MakeObject().Set("type", Value::String("reset_shortcuts")));
    EXPECT_EQ(window->keymap(), Keymap::Defaults());
}

TEST(BrowserWindowPagesTest, OverviewOperationsWorkAcrossSpaces) {
    CefRefPtr<BrowserWindow> window = MakeWindow();
    ASSERT_TRUE(window->OpenNewTab("https://a.test/"));
    ASSERT_TRUE(window->OpenNewTab("https://b.test/"));
    ASSERT_TRUE(window->CreateSpace("Research"));
    ASSERT_TRUE(window->OpenNewTab("https://r.test/"));
    ASSERT_TRUE(window->SelectSpaceIndex(0));
    ASSERT_EQ(SpaceAt(*window, 0).tab_count(), 3U);

    // Pin the last tab of space 0: it joins the pinned tray at the front.
    ASSERT_TRUE(window->SetTabPinnedAt(0, 2, true));
    EXPECT_EQ(SpaceAt(*window, 0).tabs()[0].startup_url(), "https://b.test/");
    // A regular tab cannot be dragged into the pinned tray.
    ASSERT_TRUE(window->MoveTabWithinSpace(0, 2, 0));
    EXPECT_EQ(SpaceAt(*window, 0).tabs()[0].startup_url(), "https://b.test/");
    EXPECT_EQ(SpaceAt(*window, 0).tabs()[1].startup_url(), "https://a.test/");

    // Close a tab in the inactive space without switching to it.
    ASSERT_TRUE(window->CloseTabAt(1, 0));
    EXPECT_EQ(window->active_space_index(), 0U);
    EXPECT_EQ(SpaceAt(*window, 1).tab_count(), 1U);

    // Moving reopens the URL in the target space and keeps the pin.
    ASSERT_TRUE(window->MoveTabToSpace(0, 0, 1));
    EXPECT_EQ(SpaceAt(*window, 0).tab_count(), 2U);
    ASSERT_EQ(SpaceAt(*window, 1).tab_count(), 2U);
    EXPECT_EQ(SpaceAt(*window, 1).tabs()[0].startup_url(), "https://b.test/");
    EXPECT_TRUE(SpaceAt(*window, 1).tabs()[0].pinned());
    EXPECT_FALSE(window->MoveTabToSpace(0, 0, 0));
    EXPECT_FALSE(window->CloseTabAt(9, 0));

    // The overview state mirrors the model.
    const auto overview = json::Parse(window->TabOverviewStateJson());
    ASSERT_TRUE(overview.has_value());
    const Value& spaces = *overview->FindMember("overview")->FindMember("spaces");
    ASSERT_EQ(spaces.array_val.size(), 2U);
    EXPECT_EQ(spaces.array_val[1].StringOr("name", ""), "Research");
    EXPECT_EQ(spaces.array_val[1].FindMember("tabs")->array_val.size(), 2U);
    EXPECT_TRUE(spaces.array_val[0].BoolOr("active", false));

    // Activating from the overview switches space and tab.
    ASSERT_TRUE(window->ActivateTabAt(1, 1));
    EXPECT_EQ(window->active_space_index(), 1U);
    EXPECT_EQ(SpaceAt(*window, 1).active_tab_index(), 1U);
}

TEST(BrowserWindowPagesTest, ArcSpacesImportAsPinnedSpaces) {
    CefRefPtr<BrowserWindow> window = MakeWindow();
    std::size_t tabs = 0;
    const std::size_t added = window->AddImportedSpaces(
        {{.name = "Work",
          .color_argb = 0xFF3366FFU,
          .pinned_tabs = {{"Linear", "https://linear.app/"}, {"Bad", "javascript:alert(1)"}}},
         {.name = "Empty", .color_argb = std::nullopt, .pinned_tabs = {}}},
        &tabs);
    EXPECT_EQ(added, 1U);
    EXPECT_EQ(tabs, 1U);
    ASSERT_EQ(window->space_count(), 2U);
    const Space& work = SpaceAt(*window, 1);
    EXPECT_EQ(work.name(), "Work");
    EXPECT_EQ(work.color().argb, 0xFF3366FFU);
    ASSERT_EQ(work.tab_count(), 1U);
    EXPECT_TRUE(work.tabs()[0].pinned());
    EXPECT_EQ(window->active_space_index(), 0U);  // importing never steals focus
    EXPECT_EQ(window->ImportFromBrowser("netscape"), "Unknown import source.");
}

}  // namespace
}  // namespace island
