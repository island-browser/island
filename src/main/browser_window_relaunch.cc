// 0.10 relaunch: a second Island launch hands its addresses to the running window (R-D10).

#include "browser_window.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_helpers.h"

namespace island {

void BrowserWindow::HandleRelaunch(const std::vector<std::string>& urls) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return;
    }
    if (window_ != nullptr) {
        window_->Show();
        window_->Activate();
    }
    for (const std::string& url : urls) {
        // OpenNewTab applies the address allow-list; a refused address is dropped.
        static_cast<void>(OpenNewTab(url));
    }
}

}  // namespace island
