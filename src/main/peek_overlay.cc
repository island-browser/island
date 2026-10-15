#include "peek_overlay.h"

#include <algorithm>
#include <string>
#include <utility>

#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_request_context.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_browser_view_delegate.h"
#include "include/views/cef_button_delegate.h"
#include "include/views/cef_label_button.h"
#include "include/views/cef_overlay_controller.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_panel_delegate.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_helpers.h"
#include "runtime_style.h"

namespace island {
namespace {

constexpr int kTitleBarHeightDip = 36;
constexpr int kCardMarginXDip = 56;
constexpr int kCardMarginYDip = 36;
constexpr int kCardMaxWidthDip = 1080;
constexpr int kCardMinSizeDip = 240;
constexpr int kAddressMaxChars = 96;
constexpr int kButtonWidthDip = 92;

[[nodiscard]] bool IsHttpUrl(const std::string& url) {
    return url.rfind("https://", 0) == 0 || url.rfind("http://", 0) == 0;
}

[[nodiscard]] std::string TruncateAddress(const std::string& url) {
    if (url.size() <= static_cast<std::size_t>(kAddressMaxChars)) {
        return url;
    }
    return url.substr(0, kAddressMaxChars - 1) + "\xE2\x80\xA6";
}

}  // namespace

// Re-asserts the panel's token color: CEF resets custom view colors on theme changes.
class PeekOverlay::PanelDelegate final : public CefPanelDelegate {
  public:
    enum class Role { kContent, kTitleBar };

    PanelDelegate(PeekOverlay* overlay, Role role, CefSize preferred_size)
        : overlay_(overlay), role_(role), preferred_size_(preferred_size) {}

    void Detach() { overlay_ = nullptr; }

    CefSize GetPreferredSize(CefRefPtr<CefView>) override { return preferred_size_; }

    void OnThemeChanged(CefRefPtr<CefView> view) override {
        if (overlay_ == nullptr) {
            return;
        }
        view->SetBackgroundColor(role_ == Role::kContent
                                     ? overlay_->tokens_.surface.argb
                                     : overlay_->tokens_.surface_secondary.argb);
        overlay_->ApplyColors();
    }

  private:
    PeekOverlay* overlay_;
    Role role_;
    CefSize preferred_size_;

    IMPLEMENT_REFCOUNTING(PanelDelegate);
};

class PeekOverlay::ButtonDelegate final : public CefButtonDelegate {
  public:
    explicit ButtonDelegate(PeekOverlay* overlay) : overlay_(overlay) {}

    void Detach() { overlay_ = nullptr; }

    void OnButtonPressed(CefRefPtr<CefButton> button) override {
        CEF_REQUIRE_UI_THREAD();
        if (overlay_ != nullptr) {
            overlay_->HandleButton(button->GetID());
        }
    }

    void OnThemeChanged(CefRefPtr<CefView>) override {
        if (overlay_ != nullptr) {
            overlay_->ApplyColors();
        }
    }

  private:
    PeekOverlay* overlay_;

    IMPLEMENT_REFCOUNTING(ButtonDelegate);
};

// The peek browser's client and view delegate. Popups inside the card load in the card;
// Escape is claimed before the page sees it.
class PeekOverlay::Client final : public CefClient,
                                  public CefLifeSpanHandler,
                                  public CefDisplayHandler,
                                  public CefKeyboardHandler,
                                  public CefBrowserViewDelegate {
  public:
    explicit Client(PeekOverlay* overlay) : overlay_(overlay) {}

    void Detach() { overlay_ = nullptr; }

    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
    CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
    CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override { return this; }

    bool OnBeforePopup(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame>, int,
                       const CefString& target_url, const CefString&, cef_window_open_disposition_t,
                       bool, const CefPopupFeatures&, CefWindowInfo&, CefRefPtr<CefClient>&,
                       CefBrowserSettings&, CefRefPtr<CefDictionaryValue>&, bool*) override {
        CEF_REQUIRE_UI_THREAD();
        const std::string url = target_url.ToString();
        if (browser != nullptr && IsHttpUrl(url)) {
            browser->GetMainFrame()->LoadURL(url);
        }
        return true;
    }

    void OnTitleChange(CefRefPtr<CefBrowser>, const CefString& title) override {
        CEF_REQUIRE_UI_THREAD();
        if (overlay_ != nullptr) {
            overlay_->OnPageTitle(title.ToString());
        }
    }

    void OnAddressChange(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame,
                         const CefString& url) override {
        CEF_REQUIRE_UI_THREAD();
        if (overlay_ != nullptr && frame != nullptr && frame->IsMain()) {
            overlay_->OnPageAddress(url.ToString());
        }
    }

    bool OnPreKeyEvent(CefRefPtr<CefBrowser>, const CefKeyEvent& event, CefEventHandle,
                       bool*) override {
        CEF_REQUIRE_UI_THREAD();
        if (overlay_ != nullptr && event.type == KEYEVENT_RAWKEYDOWN &&
            event.windows_key_code == kPeekEscapeKeyCode) {
            return overlay_->HandleEscape();
        }
        return false;
    }

    void OnBrowserCreated(CefRefPtr<CefBrowserView>, CefRefPtr<CefBrowser> browser) override {
        CEF_REQUIRE_UI_THREAD();
        if (overlay_ != nullptr) {
            overlay_->OnBrowserReady(std::move(browser));
        }
    }

    void OnBrowserDestroyed(CefRefPtr<CefBrowserView>, CefRefPtr<CefBrowser> browser) override {
        CEF_REQUIRE_UI_THREAD();
        if (overlay_ != nullptr) {
            overlay_->OnBrowserGone(std::move(browser));
        }
    }

    ChromeToolbarType GetChromeToolbarType(CefRefPtr<CefBrowserView>) override {
        return CEF_CTT_NONE;
    }
    cef_runtime_style_t GetBrowserRuntimeStyle() override { return kIslandRuntimeStyle; }

  private:
    PeekOverlay* overlay_;

    IMPLEMENT_REFCOUNTING(Client);
};

PeekOverlay::PeekOverlay(PeekOverlayHost& host, CefRefPtr<CefWindow> window, ChromeTokens tokens)
    : host_(&host), window_(std::move(window)), tokens_(tokens) {}

PeekOverlay::~PeekOverlay() { Detach(); }

DipRect PeekOverlay::CardBounds(const DipRect& content) noexcept {
    const int width = std::max(std::min(content.width - 2 * kCardMarginXDip, kCardMaxWidthDip),
                               std::min(kCardMinSizeDip, content.width));
    const int height =
        std::max(content.height - 2 * kCardMarginYDip, std::min(kCardMinSizeDip, content.height));
    return {.x = content.x + (content.width - width) / 2,
            .y = content.y + (content.height - height) / 2,
            .width = width,
            .height = height};
}

bool PeekOverlay::OwnsBrowser(const CefRefPtr<CefBrowser>& browser) const {
    return browser != nullptr && browser_ != nullptr && browser->IsSame(browser_);
}

void PeekOverlay::Open(const std::string& url, CefRefPtr<CefRequestContext> request_context,
                       const DipRect& content_bounds) {
    CEF_REQUIRE_UI_THREAD();
    if (detached_ || window_ == nullptr || !IsHttpUrl(url)) {
        return;
    }
    if (state_.open()) {
        // A second link replaces the first: drop the old card without reporting a close.
        state_.Close();
        TearDownViews();
    }
    static_cast<void>(state_.Open(url));
    BuildViews(url, std::move(request_context));
    if (overlay_lifetime_.Show() == OverlayStep::kCreate) {
        // can_activate is true so the page can take keyboard focus. The overlay only exists
        // while the peek is open (see overlay_lifetime.h) and covers just the card.
        overlay_.Adopt(window_->AddOverlayView(card_root_, CEF_DOCKING_MODE_CUSTOM,
                                               /*can_activate=*/true));
    }
    if (!overlay_.has_overlay()) {
        state_.Close();
        TearDownViews();
        return;
    }
    UpdateBounds(content_bounds);
    overlay_.get()->SetVisible(true);
    ApplyColors();
    if (browser_view_ != nullptr) {
        browser_view_->RequestFocus();
    }
}

void PeekOverlay::BuildViews(const std::string& url, CefRefPtr<CefRequestContext> request_context) {
    closing_ = false;
    client_ = new Client(this);
    content_delegate_ = new PanelDelegate(this, PanelDelegate::Role::kContent, CefSize());
    title_delegate_ =
        new PanelDelegate(this, PanelDelegate::Role::kTitleBar, CefSize(0, kTitleBarHeightDip));
    button_delegate_ = new ButtonDelegate(this);

    content_ = CefPanel::CreatePanel(content_delegate_);
    content_->SetID(static_cast<int>(ChromeViewId::kPeekCard));
    CefBoxLayoutSettings column;
    column.horizontal = static_cast<int>(false);
    column.between_child_spacing = 0;
    CefRefPtr<CefBoxLayout> column_layout = content_->SetToBoxLayout(column);

    title_bar_ = CefPanel::CreatePanel(title_delegate_);
    title_bar_->SetID(static_cast<int>(ChromeViewId::kPeekTitleBar));
    CefBoxLayoutSettings row;
    row.horizontal = static_cast<int>(true);
    row.between_child_spacing = tokens_.spacing_2_dip;
    row.inside_border_insets = CefInsets(4, tokens_.spacing_3_dip, 4, tokens_.spacing_2_dip);
    row.cross_axis_alignment = CEF_AXIS_ALIGNMENT_CENTER;
    CefRefPtr<CefBoxLayout> row_layout = title_bar_->SetToBoxLayout(row);

    address_ = CefLabelButton::CreateLabelButton(button_delegate_, TruncateAddress(url));
    address_->SetID(static_cast<int>(ChromeViewId::kPeekAddress));
    address_->SetAccessibleName("Peek address");
    address_->SetFocusable(false);
    address_->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_LEFT);
    address_->SetMinimumSize(CefSize(tokens_.spacing_6_dip, kTitleBarHeightDip - 8));
    title_bar_->AddChildView(address_);
    row_layout->SetFlexForView(address_, 1);

    open_as_tab_ = CefLabelButton::CreateLabelButton(button_delegate_, "Open as tab");
    open_as_tab_->SetID(static_cast<int>(ChromeViewId::kPeekOpenAsTab));
    open_as_tab_->SetAccessibleName("Open as tab");
    open_as_tab_->SetTooltipText("Move this page into the current space as a tab");
    open_as_tab_->SetFocusable(true);
    open_as_tab_->SetInkDropEnabled(true);
    open_as_tab_->SetMinimumSize(CefSize(kButtonWidthDip, kTitleBarHeightDip - 8));
    title_bar_->AddChildView(open_as_tab_);

    close_ = CefLabelButton::CreateLabelButton(button_delegate_, "Close");
    close_->SetID(static_cast<int>(ChromeViewId::kPeekClose));
    close_->SetAccessibleName("Close peek");
    close_->SetTooltipText("Close (Esc)");
    close_->SetFocusable(true);
    close_->SetInkDropEnabled(true);
    close_->SetMinimumSize(CefSize(kButtonWidthDip - 28, kTitleBarHeightDip - 8));
    title_bar_->AddChildView(close_);

    content_->AddChildView(title_bar_);
    browser_view_ = CefBrowserView::CreateBrowserView(client_, CefString(url), CefBrowserSettings(),
                                                      nullptr, std::move(request_context), client_);
    content_->AddChildView(browser_view_);
    column_layout->SetFlexForView(browser_view_, 1);

    card_root_ = card_.Wrap(content_, tokens_);
    content_->SetBackgroundColor(tokens_.surface.argb);
    title_bar_->SetBackgroundColor(tokens_.surface_secondary.argb);
}

void PeekOverlay::TearDownViews() {
    if (client_ != nullptr) {
        client_->Detach();
    }
    if (overlay_lifetime_.Hide() == OverlayStep::kDestroy) {
        // Order matters: hide the card, close its browser, then release the overlay (which
        // destroys it on the next UI task, because we may be inside the card's own handler).
        if (overlay_.has_overlay() && overlay_.get()->IsValid()) {
            overlay_.get()->SetVisible(false);
        }
        if (browser_ != nullptr) {
            browser_->GetHost()->CloseBrowser(true);
        }
        overlay_.Release();
    } else if (browser_ != nullptr) {
        browser_->GetHost()->CloseBrowser(true);
    }
    browser_ = nullptr;
    browser_view_ = nullptr;
    address_ = nullptr;
    open_as_tab_ = nullptr;
    close_ = nullptr;
    title_bar_ = nullptr;
    content_ = nullptr;
    card_root_ = nullptr;
    if (content_delegate_ != nullptr) {
        content_delegate_->Detach();
    }
    if (title_delegate_ != nullptr) {
        title_delegate_->Detach();
    }
    if (button_delegate_ != nullptr) {
        button_delegate_->Detach();
    }
    content_delegate_ = nullptr;
    title_delegate_ = nullptr;
    button_delegate_ = nullptr;
    client_ = nullptr;
}

void PeekOverlay::Close() {
    CEF_REQUIRE_UI_THREAD();
    if (!state_.Close()) {
        return;
    }
    TearDownViews();
    if (!detached_) {
        host_->OnPeekClosed();
    }
}

void PeekOverlay::UpdateBounds(const DipRect& content_bounds) {
    CEF_REQUIRE_UI_THREAD();
    if (detached_ || !overlay_.has_overlay() || !overlay_.get()->IsValid()) {
        return;
    }
    const DipRect card = CardBounds(content_bounds);
    card_.SetContentSize(card.width, card.height);
    overlay_.get()->SetBounds(
        OverlayCard::OverlayBoundsForContent(CefRect(card.x, card.y, card.width, card.height)));
}

void PeekOverlay::ApplyTheme(ChromeTokens tokens) {
    CEF_REQUIRE_UI_THREAD();
    tokens_ = tokens;
    ApplyColors();
}

void PeekOverlay::ApplyColors() {
    if (content_ == nullptr) {
        return;
    }
    card_.Apply(tokens_);
    content_->SetBackgroundColor(tokens_.surface.argb);
    title_bar_->SetBackgroundColor(tokens_.surface_secondary.argb);
    address_->SetBackgroundColor(tokens_.surface_secondary.argb);
    address_->SetEnabledTextColors(tokens_.text_secondary.argb);
    address_->SetFontList("Geist Mono, 12px");
    open_as_tab_->SetBackgroundColor(tokens_.text.argb);
    open_as_tab_->SetEnabledTextColors(tokens_.surface.argb);
    open_as_tab_->SetFontList("Geist, Medium 13px");
    close_->SetBackgroundColor(tokens_.surface.argb);
    close_->SetEnabledTextColors(tokens_.text.argb);
    close_->SetFontList("Geist, Medium 13px");
}

void PeekOverlay::RefreshAddress() {
    if (address_ != nullptr) {
        address_->SetText(TruncateAddress(state_.url()));
        address_->SetTooltipText(state_.title().empty() ? state_.url() : state_.title());
    }
}

void PeekOverlay::HandleButton(int id) {
    if (id == static_cast<int>(ChromeViewId::kPeekClose)) {
        Close();
    } else if (id == static_cast<int>(ChromeViewId::kPeekOpenAsTab)) {
        std::optional<std::string> url = state_.TakeForTab();
        if (url.has_value()) {
            TearDownViews();
            host_->OnPeekOpenAsTab(std::move(*url));
        }
    }
}

void PeekOverlay::OnBrowserReady(CefRefPtr<CefBrowser> browser) {
    if (state_.open()) {
        browser_ = std::move(browser);
    }
}

void PeekOverlay::OnBrowserGone(CefRefPtr<CefBrowser> browser) {
    if (OwnsBrowser(browser)) {
        browser_ = nullptr;
    }
}

void PeekOverlay::OnPageTitle(const std::string& title) {
    state_.SetTitle(title);
    RefreshAddress();
}

void PeekOverlay::OnPageAddress(const std::string& url) {
    state_.SetUrl(url);
    RefreshAddress();
}

bool PeekOverlay::HandleEscape() {
    if (!state_.HandleKey(kPeekEscapeKeyCode)) {
        return false;
    }
    // HandleKey already cleared the state; finish the teardown the way Close() does.
    TearDownViews();
    if (!detached_) {
        host_->OnPeekClosed();
    }
    return true;
}

void PeekOverlay::Detach() {
    CEF_REQUIRE_UI_THREAD();
    if (detached_) {
        return;
    }
    detached_ = true;
    state_.Close();
    TearDownViews();
    overlay_.DestroyNow();
}

}  // namespace island
