#include "local_page.h"

#include <utility>

#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_frame.h"
#include "include/cef_parser.h"
#include "include/cef_request.h"
#include "include/views/cef_browser_view_delegate.h"
#include "include/wrapper/cef_helpers.h"
#include "local_pages_html.h"

namespace island {

class LocalPage::Client final : public CefClient,
                                public CefDisplayHandler,
                                public CefLifeSpanHandler,
                                public CefLoadHandler,
                                public CefRequestHandler,
                                public CefBrowserViewDelegate {
  public:
    Client(LocalPageKind kind, LocalPageDelegate* delegate)
        : kind_(kind), delegate_(delegate), page_url_(PageUrl(kind)) {}

    void Detach() { delegate_ = nullptr; }

    void Render(std::string state_json) {
        CEF_REQUIRE_UI_THREAD();
        if (browser_ == nullptr || !loaded_) {
            pending_state_ = std::move(state_json);
            return;
        }
        Execute(RenderScript(state_json));
    }

    void Focus() {
        if (browser_ != nullptr && loaded_) Execute("window.islandFocus && window.islandFocus()");
    }

    void CloseBrowser() {
        if (browser_ != nullptr) browser_->GetHost()->CloseBrowser(true);
    }

    // CefClient
    CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
    CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
    CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }

    // CefDisplayHandler: the page's only channel back to native.
    bool OnConsoleMessage(CefRefPtr<CefBrowser>, cef_log_severity_t, const CefString& message,
                          const CefString&, int) override {
        CEF_REQUIRE_UI_THREAD();
        std::optional<json::Value> decoded = DecodeMessage(message.ToString());
        if (!decoded) return false;  // ordinary console output keeps logging
        if (delegate_ != nullptr) delegate_->OnLocalPageMessage(kind_, *decoded);
        return true;
    }

    // CefLifeSpanHandler
    bool OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int, const CefString& target_url,
                       const CefString&, CefLifeSpanHandler::WindowOpenDisposition, bool,
                       const CefPopupFeatures&, CefWindowInfo&, CefRefPtr<CefClient>&,
                       CefBrowserSettings&, CefRefPtr<CefDictionaryValue>&, bool*) override {
        ForwardLink(target_url.ToString());
        return true;  // the panel never opens windows
    }
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override { browser_ = browser; }
    void OnBeforeClose(CefRefPtr<CefBrowser>) override {
        browser_ = nullptr;
        loaded_ = false;
    }

    // CefLoadHandler
    void OnLoadEnd(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, int) override {
        if (!frame->IsMain()) return;
        loaded_ = true;
        if (pending_state_.has_value()) {
            Execute(RenderScript(*pending_state_));
            pending_state_.reset();
        }
    }

    // CefRequestHandler: the panel page is the only document it ever shows.
    bool OnBeforeBrowse(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame,
                        CefRefPtr<CefRequest> request, bool, bool) override {
        const std::string url = request->GetURL().ToString();
        if (frame->IsMain() && url == page_url_) return false;
        ForwardLink(url);
        return true;
    }

    // CefBrowserViewDelegate
    ChromeToolbarType GetChromeToolbarType(CefRefPtr<CefBrowserView>) override {
        return CEF_CTT_NONE;
    }

  private:
    void Execute(const std::string& script) {
        CefRefPtr<CefFrame> frame = browser_->GetMainFrame();
        if (frame != nullptr) frame->ExecuteJavaScript(script, page_url_, 0);
    }

    void ForwardLink(const std::string& url) {
        if (delegate_ == nullptr) return;
        if (url.rfind("https://", 0) != 0 && url.rfind("http://", 0) != 0) return;
        delegate_->OnLocalPageMessage(kind_, json::Value::MakeObject()
                                                 .Set("type", json::Value::String("open_url"))
                                                 .Set("url", json::Value::String(url)));
    }

    LocalPageKind kind_;
    LocalPageDelegate* delegate_;
    std::string page_url_;
    CefRefPtr<CefBrowser> browser_;
    bool loaded_ = false;
    std::optional<std::string> pending_state_;

    IMPLEMENT_REFCOUNTING(Client);
};

LocalPage::LocalPage(LocalPageKind kind, LocalPageDelegate& delegate)
    : kind_(kind), client_(new Client(kind, &delegate)) {
    CEF_REQUIRE_UI_THREAD();
    CefBrowserSettings settings;
    view_ = CefBrowserView::CreateBrowserView(client_, PageUrl(kind), settings, nullptr, nullptr,
                                              client_);
}

LocalPage::~LocalPage() { Close(); }

void LocalPage::Render(const std::string& state_json) {
    if (client_ != nullptr) client_->Render(state_json);
}

void LocalPage::Focus() {
    if (view_ != nullptr) view_->RequestFocus();
    if (client_ != nullptr) client_->Focus();
}

void LocalPage::Close() {
    if (client_ == nullptr) return;
    client_->Detach();
    client_->CloseBrowser();
    client_ = nullptr;
    view_ = nullptr;
}

std::string LocalPage::PageUrl(LocalPageKind kind) {
    const std::string_view html = LocalPageHtml(kind);
    return "data:text/html;charset=utf-8;base64," +
           CefBase64Encode(html.data(), html.size()).ToString();
}

std::optional<json::Value> LocalPage::DecodeMessage(std::string_view console_text) {
    if (console_text.substr(0, kMessagePrefix.size()) != kMessagePrefix) return std::nullopt;
    std::optional<json::Value> message = json::Parse(console_text.substr(kMessagePrefix.size()));
    if (!message || !message->IsObject() || message->StringOr("type", "").empty()) {
        return std::nullopt;
    }
    return message;
}

std::string LocalPage::RenderScript(std::string_view state_json) {
    // JSON is a JavaScript expression; escape the two line separators older
    // parsers reject inside string literals so any agent text is safe.
    std::string safe;
    safe.reserve(state_json.size());
    for (std::size_t i = 0; i < state_json.size(); ++i) {
        if (static_cast<unsigned char>(state_json[i]) == 0xE2 && i + 2 < state_json.size() &&
            static_cast<unsigned char>(state_json[i + 1]) == 0x80 &&
            (static_cast<unsigned char>(state_json[i + 2]) == 0xA8 ||
             static_cast<unsigned char>(state_json[i + 2]) == 0xA9)) {
            safe += static_cast<unsigned char>(state_json[i + 2]) == 0xA8 ? "\\u2028" : "\\u2029";
            i += 2;
        } else {
            safe += state_json[i];
        }
    }
    return "window.islandRender && window.islandRender(" + safe + ");";
}

}  // namespace island
