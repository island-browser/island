#ifndef ISLAND_BROWSER_WINDOW_H_
#define ISLAND_BROWSER_WINDOW_H_

#include <string>

#include "browser_command.h"
#include "include/cef_client.h"
#include "include/views/cef_browser_view_delegate.h"
#include "include/views/cef_window_delegate.h"
#include "navigation_state.h"

class CefBrowser;
class CefBrowserView;
class CefFrame;
class CefWindow;

namespace island {

class BrowserWindow : public CefClient,
                      public CefDisplayHandler,
                      public CefLifeSpanHandler,
                      public CefLoadHandler,
                      public CefWindowDelegate,
                      public CefBrowserViewDelegate {
  public:
    static CefRefPtr<BrowserWindow> Create(std::string initial_url);

    void ExecuteCommand(BrowserCommand command);
    void SetNavigationObserver(NavigationObserver* observer);
    [[nodiscard]] const NavigationSnapshot& navigation_snapshot() const noexcept;
    void RequestClose();

    CefRefPtr<CefDisplayHandler> GetDisplayHandler() override;
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override;
    CefRefPtr<CefLoadHandler> GetLoadHandler() override;

    void OnAddressChange(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                         const CefString& url) override;
    void OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) override;

    bool OnBeforePopup(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int popup_id,
                       const CefString& target_url, const CefString& target_frame_name,
                       WindowOpenDisposition target_disposition, bool user_gesture,
                       const CefPopupFeatures& popup_features, CefWindowInfo& window_info,
                       CefRefPtr<CefClient>& client, CefBrowserSettings& settings,
                       CefRefPtr<CefDictionaryValue>& extra_info,
                       bool* no_javascript_access) override;
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
    bool DoClose(CefRefPtr<CefBrowser> browser) override;
    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

    void OnLoadingStateChange(CefRefPtr<CefBrowser> browser, bool is_loading, bool can_go_back,
                              bool can_go_forward) override;
    void OnLoadStart(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                     TransitionType transition_type) override;
    void OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                   int http_status_code) override;
    void OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, ErrorCode error_code,
                     const CefString& error_text, const CefString& failed_url) override;

    void OnWindowCreated(CefRefPtr<CefWindow> window) override;
    void OnWindowDestroyed(CefRefPtr<CefWindow> window) override;
    bool CanClose(CefRefPtr<CefWindow> window) override;
    bool OnAccelerator(CefRefPtr<CefWindow> window, int command_id) override;

    void OnBrowserCreated(CefRefPtr<CefBrowserView> browser_view,
                          CefRefPtr<CefBrowser> browser) override;
    void OnBrowserDestroyed(CefRefPtr<CefBrowserView> browser_view,
                            CefRefPtr<CefBrowser> browser) override;
    ChromeToolbarType GetChromeToolbarType(CefRefPtr<CefBrowserView> browser_view) override;

  private:
    enum AcceleratorId {
        kBackAccelerator = 1,
        kForwardAccelerator,
        kReloadAccelerator,
        kReloadWithControlAccelerator,
    };

    explicit BrowserWindow(std::string initial_url);

    [[nodiscard]] bool IsMainBrowser(CefRefPtr<CefBrowser> browser) const;
    void UpdateWindowTitle();
    void CloseNavigationAndQuitMessageLoop();

    std::string initial_url_;
    NavigationState navigation_state_;
    CefRefPtr<CefWindow> window_;
    CefRefPtr<CefBrowserView> browser_view_;
    CefRefPtr<CefBrowser> browser_;
    bool browser_was_created_ = false;
    bool message_loop_quit_ = false;

    IMPLEMENT_REFCOUNTING(BrowserWindow);
    DISALLOW_COPY_AND_ASSIGN(BrowserWindow);
};

}  // namespace island

#endif  // ISLAND_BROWSER_WINDOW_H_
