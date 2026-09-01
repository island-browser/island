#ifndef ISLAND_BROWSER_WINDOW_H_
#define ISLAND_BROWSER_WINDOW_H_

#include <memory>
#include <string>
#include <string_view>

#include "active_tab_provider.h"
#include "browser_chrome.h"
#include "browser_command.h"
#include "chrome_snapshot.h"
#include "include/cef_client.h"
#include "include/internal/cef_types.h"
#include "include/views/cef_browser_view_delegate.h"
#include "include/views/cef_window_delegate.h"
#include "navigation_state.h"
#include "search_palette.h"
#include "sidebar_state.h"
#include "space.h"
#include "tab.h"

class CefBrowser;
class CefBrowserView;
class CefFrame;
class CefOverlayController;
class CefWindow;

namespace island {

[[nodiscard]] inline ChromeTheme ClassifyChromeTheme(cef_color_t primary_background) noexcept {
    constexpr std::uint32_t kLuminanceScale = 10000;
    constexpr std::uint32_t kRedWeight = 2126;
    constexpr std::uint32_t kGreenWeight = 7152;
    constexpr std::uint32_t kBlueWeight = 722;
    constexpr std::uint32_t kDarkThemeLuminance = 128;

    const std::uint32_t luminance = (kRedWeight * CefColorGetR(primary_background) +
                                     kGreenWeight * CefColorGetG(primary_background) +
                                     kBlueWeight * CefColorGetB(primary_background)) /
                                    kLuminanceScale;
    return luminance < kDarkThemeLuminance ? ChromeTheme::kDark : ChromeTheme::kLight;
}

class BrowserWindow : public CefClient,
                      public CefDisplayHandler,
                      public CefLifeSpanHandler,
                      public CefLoadHandler,
                      public CefWindowDelegate,
                      public CefBrowserViewDelegate,
                      public BrowserChromeHost,
                      public ActiveTabProvider,
                      public SearchPaletteHost,
                      public NavigationObserver {
  public:
    static CefRefPtr<BrowserWindow> Create(std::string initial_url);

    void ExecuteCommand(BrowserCommand command);
    void SetNavigationObserver(NavigationObserver* observer);
    void SetChromeObserver(ChromeObserver* observer);
    [[nodiscard]] const NavigationSnapshot& navigation_snapshot() const noexcept;
    [[nodiscard]] const ChromeSnapshot& chrome_snapshot() const noexcept;
    [[nodiscard]] ChromeViewTreeNode chrome_view_tree_snapshot() const;
    void RequestClose();
    // Opens the Cmd/Ctrl+K search palette, creating it on first use. Public so the
    // macOS main menu can reach it: CefWindow::SetAccelerator never dispatches on
    // macOS, where NSMenu key equivalents own the command keys.
    void ShowSearchPalette();
    // Flips the explicit Cmd/Ctrl+B pin. Available on every platform; hover is a
    // macOS-only enhancement layered on the same state.
    void ToggleSidebar();
    // The macOS hover seam's entry point: a pointer position in window DIP.
    void OnSidebarHoverPointer(int x_dip);
    // Navigates the active tab to the provider URL for `query`. A blank query is
    // rejected and a null ActiveBrowser() is a defined no-op; neither navigates.
    void SubmitSearchQuery(std::string_view query, SearchProviderId provider);

    void ExecuteBrowserCommand(BrowserCommand command) override;
    void BeginAddressEditing() override;
    void CancelAddressEditing() override;
    void SubmitAddressDraft(std::string_view draft) override;
    void FocusBrowserView() override;
    void OnNavigationChanged(const NavigationSnapshot& snapshot) override;

    [[nodiscard]] CefRefPtr<CefBrowser> ActiveBrowser() override;

    void OnSearchPaletteSubmitted(const SearchSubmission& submission) override;
    void OnSearchPaletteDismissed() override;

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
    void OnWindowBoundsChanged(CefRefPtr<CefWindow> window, const CefRect& new_bounds) override;
    void OnThemeColorsChanged(CefRefPtr<CefWindow> window, bool chrome_theme) override;
    CefSize GetMinimumSize(CefRefPtr<CefView> view) override;
    bool CanClose(CefRefPtr<CefWindow> window) override;
    bool OnAccelerator(CefRefPtr<CefWindow> window, int command_id) override;

    void OnBrowserCreated(CefRefPtr<CefBrowserView> browser_view,
                          CefRefPtr<CefBrowser> browser) override;
    void OnBrowserDestroyed(CefRefPtr<CefBrowserView> browser_view,
                            CefRefPtr<CefBrowser> browser) override;
    ChromeToolbarType GetChromeToolbarType(CefRefPtr<CefBrowserView> browser_view) override;
    void OnFocus(CefRefPtr<CefView> view) override;

  private:
    enum AcceleratorId {
        kBackAccelerator = 1,
        kForwardAccelerator,
        kReloadAccelerator,
        kReloadWithControlAccelerator,
        kFocusAddressAccelerator,
        kOpenPaletteAccelerator,
        kToggleSidebarAccelerator,
    };

    explicit BrowserWindow(std::string initial_url);

    [[nodiscard]] Space& active_space() noexcept { return spaces_[active_space_index_]; }
    [[nodiscard]] const Space& active_space() const noexcept {
        return spaces_[active_space_index_];
    }
    [[nodiscard]] Tab* FindTabByBrowser(CefRefPtr<CefBrowser> browser) noexcept;
    [[nodiscard]] const Tab* FindTabByBrowser(CefRefPtr<CefBrowser> browser) const noexcept;
    [[nodiscard]] Tab* FindTabByBrowserView(CefRefPtr<CefBrowserView> browser_view) noexcept;
    [[nodiscard]] Tab* active_tab() noexcept;
    [[nodiscard]] const Tab* active_tab() const noexcept;
    void ApplyTheme(CefRefPtr<CefWindow> window, ChromeTheme theme, bool notify_views);
    void PublishChromeSnapshot();
    void DetachChromeAndObservers();
    void CreateHoverSliver();
    void ApplySidebarState();
    void UninstallHoverSeam();
    void UpdateWindowTitle();
    void CloseNavigationAndQuitMessageLoop();

    std::string initial_url_;
    AddressBarModel address_bar_model_;
    std::unique_ptr<BrowserChrome> chrome_;
    std::unique_ptr<SearchPalette> search_palette_;
    SidebarState sidebar_state_;
    CefRefPtr<CefPanel> hover_sliver_;
    CefRefPtr<CefOverlayController> hover_sliver_overlay_;
    void* hover_seam_ = nullptr;
    NavigationObserver* navigation_observer_ = nullptr;
    ChromeObserver* chrome_observer_ = nullptr;
    ChromeSnapshot chrome_snapshot_;
    CefRefPtr<CefWindow> window_;
    std::vector<Space> spaces_;
    std::size_t active_space_index_ = 0;
    bool browser_was_created_ = false;
    bool closing_ = false;
    bool message_loop_quit_ = false;

    IMPLEMENT_REFCOUNTING(BrowserWindow);
    DISALLOW_COPY_AND_ASSIGN(BrowserWindow);
};

}  // namespace island

#endif  // ISLAND_BROWSER_WINDOW_H_
