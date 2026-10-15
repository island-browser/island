// BrowserWindow's find-in-page and Reader view behavior. The declarations live in
// browser_window.h; this file keeps them out of the already very large browser_window.cc.
// The CEF view types must be complete before browser_window.h (see tests/browser_window_test.cpp).
// clang-format off
#include "include/views/cef_overlay_controller.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_window.h"
#include "browser_window.h"
// clang-format on

#include <algorithm>
#include <functional>
#include <random>
#include <string>
#include <utility>

#include "include/base/cef_bind.h"
#include "include/base/cef_callback.h"
#include "include/cef_browser.h"
#include "include/cef_frame.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"
#include "window_agent_host.h"

namespace island {
namespace {

std::string RandomReaderToken() {
    static constexpr char kHex[] = "0123456789abcdef";
    std::random_device device;
    std::string token;
    for (int i = 0; i < 32; ++i) {
        token += kHex[device() % 16];
    }
    return token;
}

std::size_t HashUrl(std::string_view url) { return std::hash<std::string_view>{}(url); }

}  // namespace

// --- Find in page ---------------------------------------------------------------------------

Tab* BrowserWindow::FindTabById(TabId id) noexcept {
    for (Space& space : spaces_) {
        if (Tab* tab = space.FindTab(id); tab != nullptr) {
            return tab;
        }
    }
    return nullptr;
}

void BrowserWindow::ApplyFindRequest(const FindRequest& request, CefRefPtr<CefBrowser> browser) {
    if (browser == nullptr || request.kind == FindRequest::Kind::kNone) {
        return;
    }
    CefRefPtr<CefBrowserHost> host = browser->GetHost();
    if (host == nullptr) {
        return;
    }
    if (request.kind == FindRequest::Kind::kFind) {
        host->Find(request.text, request.forward, request.match_case, request.find_next);
    } else {
        host->StopFinding(request.clear_selection);
    }
}

CefRect BrowserWindow::FindAnchorBounds() const {
    if (chrome_ == nullptr) {
        return CefRect();
    }
    // The page card is the browser content panel inset by the gutter that makes it float.
    const DipRect content = chrome_->view_bounds_snapshot().browser_content_bounds;
    const int pad = BrowserChrome::BrowserContentPaddingDip();
    return CefRect(content.x + pad, content.y + pad, std::max(0, content.width - 2 * pad),
                   std::max(0, content.height - 2 * pad));
}

void BrowserWindow::ShowFindOverlay() {
    if (closing_ || window_ == nullptr || chrome_ == nullptr) {
        return;
    }
    if (find_overlay_ == nullptr) {
        find_overlay_ = std::make_unique<FindBarOverlay>(
            *this, window_, ResolvedTokens(ResolvedChromeTheme()), icon_resource_root_);
    }
    find_overlay_->Show(find_model_.query(), find_model_.match_case(), FindAnchorBounds());
}

void BrowserWindow::UpdateFindBarBounds() {
    CEF_REQUIRE_UI_THREAD();
    if (!closing_ && find_overlay_ != nullptr && find_overlay_->visible()) {
        find_overlay_->UpdateBounds(FindAnchorBounds());
    }
}

void BrowserWindow::ScheduleFindBarBounds() {
    if (closing_ || find_overlay_ == nullptr || !find_overlay_->visible()) {
        return;
    }
    // The layout that moves the page card runs after this call; re-anchor once it has.
    CefPostTask(TID_UI, CefCreateClosureTask(base::BindOnce(&BrowserWindow::UpdateFindBarBounds,
                                                            CefRefPtr<BrowserWindow>(this))));
}

void BrowserWindow::FindInPage(std::string_view query) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_ || internal_page_.has_value()) {
        return;
    }
    Tab* tab = active_tab();
    if (tab == nullptr) {
        return;
    }
    const bool was_open = find_model_.open();
    FindRequest request = find_model_.Open();
    if (!was_open) {
        find_tab_ = tab->id();
    }
    if (!query.empty()) {
        const FindRequest typed = find_model_.SetQuery(query);
        if (typed.kind != FindRequest::Kind::kNone) {
            request = typed;
        }
    }
    if (was_open && query.empty() && find_overlay_ != nullptr && find_overlay_->visible()) {
        find_overlay_->FocusField();  // Cmd+F again: select the text so typing replaces it
    } else {
        ShowFindOverlay();
    }
    ApplyFindRequest(request, tab->browser());
    RefreshFindCount(false);
}

void BrowserWindow::StepFind(bool forward) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return;
    }
    if (!find_model_.open()) {
        FindInPage();  // Cmd+G with the bar closed reopens it on the remembered query
        return;
    }
    const Tab* tab = find_tab_.has_value() ? FindTabById(*find_tab_) : nullptr;
    ApplyFindRequest(find_model_.Step(forward), tab != nullptr ? tab->browser() : nullptr);
}

void BrowserWindow::CloseFindBar(bool refocus_page) {
    CEF_REQUIRE_UI_THREAD();
    if (find_model_.open()) {
        const Tab* tab = find_tab_.has_value() ? FindTabById(*find_tab_) : nullptr;
        const FindRequest request = find_model_.Close();
        ApplyFindRequest(request, tab != nullptr ? tab->browser() : nullptr);
    }
    find_tab_.reset();
    if (find_overlay_ != nullptr) {
        find_overlay_->Hide();
    }
    if (refocus_page && !closing_) {
        FocusBrowserView();
    }
}

void BrowserWindow::RefreshFindCount(bool final_update) {
    if (find_overlay_ == nullptr || !find_overlay_->visible()) {
        return;
    }
    const bool no_matches = final_update && find_model_.reported() && find_model_.total() == 0 &&
                            !find_model_.query().empty();
    find_overlay_->SetCount(find_model_.CountLabel(), no_matches);
}

void BrowserWindow::OnFindQueryChanged(std::string text) {
    CEF_REQUIRE_UI_THREAD();
    const Tab* tab = find_tab_.has_value() ? FindTabById(*find_tab_) : nullptr;
    ApplyFindRequest(find_model_.SetQuery(text), tab != nullptr ? tab->browser() : nullptr);
    RefreshFindCount(false);
}

void BrowserWindow::OnFindStep(bool forward) { StepFind(forward); }

void BrowserWindow::OnFindMatchCaseChanged(bool match_case) {
    CEF_REQUIRE_UI_THREAD();
    const Tab* tab = find_tab_.has_value() ? FindTabById(*find_tab_) : nullptr;
    ApplyFindRequest(find_model_.SetMatchCase(match_case),
                     tab != nullptr ? tab->browser() : nullptr);
    RefreshFindCount(false);
}

void BrowserWindow::OnFindCloseRequested() { CloseFindBar(/*refocus_page=*/true); }

CefRefPtr<CefFindHandler> BrowserWindow::GetFindHandler() { return this; }

void BrowserWindow::OnFindResult(CefRefPtr<CefBrowser> browser, int, int count, const CefRect&,
                                 int active_match_ordinal, bool final_update) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_ || !find_model_.open() || !find_tab_.has_value()) {
        return;
    }
    const Tab* owner = FindTabByBrowser(browser);
    if (owner == nullptr || owner->id() != *find_tab_) {
        return;
    }
    find_model_.OnResult(count, active_match_ordinal);
    RefreshFindCount(final_update);
}

// --- Reader view ----------------------------------------------------------------------------

bool BrowserWindow::ReaderActiveFor(const Tab& tab) const {
    const auto session = reader_sessions_.find(tab.id());
    if (session == reader_sessions_.end()) {
        return false;
    }
    const std::string& url = tab.navigation_state().snapshot().url;
    return url.size() == session->second.url_size && HashUrl(url) == session->second.url_hash;
}

bool BrowserWindow::ReaderActive(TabId tab) const {
    const Tab* found = const_cast<BrowserWindow*>(this)->FindTabById(tab);
    return found != nullptr && ReaderActiveFor(*found);
}

std::string BrowserWindow::EffectiveUrl(const Tab& tab) const {
    if (ReaderActiveFor(tab)) {
        return reader_sessions_.at(tab.id()).origin_url;
    }
    return tab.navigation_state().snapshot().url;
}

void BrowserWindow::UpdateReaderButton() {
    if (chrome_ == nullptr) {
        return;
    }
    const Tab* tab = active_tab();
    if (tab == nullptr) {
        chrome_->SetReaderState(false, false);
        return;
    }
    const bool active = ReaderActiveFor(*tab);
    const bool available =
        agent_host_ != nullptr &&
        (active || IsReaderEligibleUrl(tab->navigation_state().snapshot().url));
    chrome_->SetReaderState(available, active);
}

void BrowserWindow::ToggleReaderMode() {
    CEF_REQUIRE_UI_THREAD();
    if (closing_ || internal_page_.has_value()) {
        return;
    }
    Tab* tab = active_tab();
    if (tab == nullptr) {
        return;
    }
    if (ReaderActiveFor(*tab)) {
        ExitReaderMode(*tab);
        return;
    }
    const std::string origin = WebUrlOf(*tab);
    if (!IsReaderEligibleUrl(origin) || tab->browser() == nullptr || agent_host_ == nullptr) {
        return;
    }
    const TabId id = tab->id();
    json::Value params =
        json::Value::MakeObject()
            .Set("expression", json::Value::String(ReaderExtractionExpression()))
            .Set("returnByValue", json::Value::Bool(true))
            .Set("awaitPromise", json::Value::Bool(false));
    CefRefPtr<BrowserWindow> self(this);
    agent_host_->DevToolsCall(
        std::nullopt, "Runtime.evaluate", std::move(params),
        [self, id, origin](agent::DevToolsReply reply) {
            if (self->closing_) {
                return;
            }
            const Tab* current = self->FindTabById(id);
            // The user moved on while the page was being read: drop the result.
            if (current == nullptr || self->WebUrlOf(*current) != origin) {
                return;
            }
            std::optional<std::string> extraction;
            std::string error;
            if (!reply.ok) {
                error = reply.error;
            } else if (reply.result.FindMember("exceptionDetails") != nullptr) {
                error = "Island couldn't read this page.";
            } else if (const json::Value* result = reply.result.FindMember("result");
                       result != nullptr) {
                const json::Value* value = result->FindMember("value");
                if (value != nullptr && value->IsString()) {
                    extraction = value->string_val;
                }
            }
            self->ShowReaderDocument(id, origin, std::move(extraction), error);
        });
}

std::string BrowserWindow::ShowReaderDocument(TabId id, std::string_view origin_url,
                                              std::optional<std::string> extraction_json,
                                              std::string_view error) {
    CEF_REQUIRE_UI_THREAD();
    Tab* tab = FindTabById(id);
    if (closing_ || tab == nullptr) {
        return {};
    }
    std::optional<ReaderArticle> article;
    if (extraction_json.has_value()) {
        article = ParseReaderArticle(*extraction_json);
    }
    const std::string token = RandomReaderToken();
    const ReaderSettings settings =
        ReaderSettings::Normalized(prefs_.reader_size, prefs_.reader_width, prefs_.reader_theme);
    const std::string html =
        BuildReaderHtml(article.has_value() ? &*article : nullptr, article.has_value() ? "" : error,
                        token, settings, chrome_snapshot_.theme == ChromeTheme::kDark);
    const std::string url = ReaderDataUrl(html);

    // Sessions of closed tabs would otherwise accumulate.
    for (auto it = reader_sessions_.begin(); it != reader_sessions_.end();) {
        it = FindTabById(it->first) == nullptr ? reader_sessions_.erase(it) : std::next(it);
    }
    reader_sessions_[id] = ReaderSession{.origin_url = std::string(origin_url),
                                         .token = token,
                                         .url_hash = HashUrl(url),
                                         .url_size = url.size()};
    CefRefPtr<CefBrowser> browser = tab->browser();
    CefRefPtr<CefFrame> frame = browser != nullptr ? browser->GetMainFrame() : nullptr;
    if (frame != nullptr) {
        frame->LoadURL(url);
    } else {
        // Headless: there is no commit to wait for, so record it the way the commit would.
        tab->navigation_state().SetAddress(url);
    }
    UpdateReaderButton();
    return url;
}

void BrowserWindow::ExitReaderMode(Tab& tab) {
    const auto session = reader_sessions_.find(tab.id());
    CefRefPtr<CefBrowser> browser = tab.browser();
    if (session == reader_sessions_.end() || browser == nullptr) {
        return;
    }
    // The reader document was loaded on top of the page, so Back returns to it intact (scroll
    // position, form state); without history, load the page again.
    if (browser->CanGoBack()) {
        browser->GoBack();
        return;
    }
    if (CefRefPtr<CefFrame> frame = browser->GetMainFrame(); frame != nullptr) {
        frame->LoadURL(session->second.origin_url);
    }
}

void BrowserWindow::HandleReaderMessage(Tab& tab, const json::Value& message) {
    CEF_REQUIRE_UI_THREAD();
    const auto session = reader_sessions_.find(tab.id());
    if (closing_ || session == reader_sessions_.end() ||
        message.StringOr("token", "") != session->second.token) {
        return;
    }
    const std::string_view type = message.StringOr("type", "");
    if (type == "reader_exit") {
        ExitReaderMode(tab);
    } else if (type == "reader_settings") {
        const ReaderSettings settings = ReaderSettings::Normalized(
            static_cast<int>(message.IntOr("size", ReaderSettings::kDefaultSize)),
            message.StringOr("width", ""), message.StringOr("theme", ""));
        if (settings.size != prefs_.reader_size || settings.width != prefs_.reader_width ||
            settings.theme != prefs_.reader_theme) {
            prefs_.reader_size = settings.size;
            prefs_.reader_width = settings.width;
            prefs_.reader_theme = settings.theme;
            SavePrefs();
        }
    }
}

}  // namespace island
