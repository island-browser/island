#ifndef ISLAND_ACTIVE_TAB_PROVIDER_H_
#define ISLAND_ACTIVE_TAB_PROVIDER_H_

#include "include/cef_base.h"

class CefBrowser;

namespace island {

// The one seam palette submission uses to reach the page it should navigate.
// BrowserWindow implements it from the active space's active tab, so the
// palette never holds a CefBrowser and the multi-space restructuring changes
// only this implementation.
class ActiveTabProvider {
  public:
    virtual ~ActiveTabProvider() = default;

    // The active tab's browser, or nullptr when no tab has one yet. A null
    // return is a defined no-op for every caller, never an error.
    [[nodiscard]] virtual CefRefPtr<CefBrowser> ActiveBrowser() = 0;
};

}  // namespace island

#endif  // ISLAND_ACTIVE_TAB_PROVIDER_H_
