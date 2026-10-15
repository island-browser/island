#ifndef ISLAND_MAIN_PEEK_OVERLAY_H_
#define ISLAND_MAIN_PEEK_OVERLAY_H_

#include <optional>
#include <string>

#include "browser_chrome.h"
#include "chrome_snapshot.h"
#include "design_tokens.h"
#include "include/cef_base.h"
#include "overlay_card.h"
#include "overlay_lifetime.h"
#include "peek_state.h"

class CefBrowser;
class CefBrowserView;
class CefLabelButton;
class CefPanel;
class CefRequestContext;
class CefWindow;

namespace island {

// The window-side seam the peek card reports through.
class PeekOverlayHost {
  public:
    virtual ~PeekOverlayHost() = default;

    // "Open as tab": the card has already closed; open |url| in the active space.
    virtual void OnPeekOpenAsTab(std::string url) = 0;
    // The card closed (button, Escape, or a replacement); focus returns to the page.
    virtual void OnPeekClosed() = 0;
};

// The floating "Little Arc" card: a rounded-looking card (CEF views cannot round corners; the
// card is a hairline-outlined, shadowed panel like the palettes) with a title bar holding the
// address and the "Open as tab" / "Close" buttons above its own CefBrowserView. It lives in an
// overlay of the one existing CefWindow that exists only while a peek is open (see
// overlay_lifetime.h: a live overlay kills mouse input on macOS), and the overlay covers only
// the card, so the page around it stays clickable.
class PeekOverlay final {
  public:
    PeekOverlay(PeekOverlayHost& host, CefRefPtr<CefWindow> window, ChromeTokens tokens);
    ~PeekOverlay();

    PeekOverlay(const PeekOverlay&) = delete;
    PeekOverlay& operator=(const PeekOverlay&) = delete;

    // Opens |url| in the card (replacing an open peek's page).
    void Open(const std::string& url, CefRefPtr<CefRequestContext> request_context,
              const DipRect& content_bounds);
    // Hides the card, closes its browser, and releases the overlay. Idempotent.
    void Close();
    void UpdateBounds(const DipRect& content_bounds);
    void ApplyTheme(ChromeTokens tokens);
    void Detach();

    [[nodiscard]] bool open() const noexcept { return state_.open(); }
    [[nodiscard]] const PeekState& state() const noexcept { return state_; }
    // The card's browser (null until created); tests and captures read it.
    [[nodiscard]] CefRefPtr<CefBrowser> browser() const noexcept { return browser_; }
    [[nodiscard]] bool OwnsBrowser(const CefRefPtr<CefBrowser>& browser) const;

    // The card's bounds inside the content area, in window DIP.
    [[nodiscard]] static DipRect CardBounds(const DipRect& content) noexcept;

  private:
    class Client;
    class PanelDelegate;
    class ButtonDelegate;

    void BuildViews(const std::string& url, CefRefPtr<CefRequestContext> request_context);
    void TearDownViews();
    void ApplyColors();
    void RefreshAddress();
    void HandleButton(int id);
    void OnBrowserReady(CefRefPtr<CefBrowser> browser);
    void OnBrowserGone(CefRefPtr<CefBrowser> browser);
    void OnPageTitle(const std::string& title);
    void OnPageAddress(const std::string& url);
    bool HandleEscape();

    PeekOverlayHost* host_;
    CefRefPtr<CefWindow> window_;
    ChromeTokens tokens_;
    PeekState state_;
    OverlaySlot overlay_;
    OverlayLifetime overlay_lifetime_;
    OverlayCard card_;
    CefRefPtr<CefPanel> card_root_;
    CefRefPtr<CefPanel> content_;
    CefRefPtr<CefPanel> title_bar_;
    CefRefPtr<CefLabelButton> address_;
    CefRefPtr<CefLabelButton> open_as_tab_;
    CefRefPtr<CefLabelButton> close_;
    CefRefPtr<CefBrowserView> browser_view_;
    CefRefPtr<CefBrowser> browser_;
    CefRefPtr<Client> client_;
    CefRefPtr<PanelDelegate> content_delegate_;
    CefRefPtr<PanelDelegate> title_delegate_;
    CefRefPtr<ButtonDelegate> button_delegate_;
    bool detached_ = false;
    bool closing_ = false;
};

}  // namespace island

#endif  // ISLAND_MAIN_PEEK_OVERLAY_H_
