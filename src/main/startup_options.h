#ifndef ISLAND_MAIN_STARTUP_OPTIONS_H_
#define ISLAND_MAIN_STARTUP_OPTIONS_H_

#include <span>
#include <string_view>

namespace island {

// The production start page is Island's own search page (src/main/pages/start.html),
// embedded at configure time and loaded as a base64 `data:` URL by BrowserWindow, so
// StartupOptions leaves the production URL empty and the window supplies it. The page
// carries no external reference; startup requests no network resource until the user
// submits a search. The smoke run keeps its fixed, deterministic marker page below.

inline constexpr std::string_view kSmokeTestInitialUrl =
    "data:text/html;charset=utf-8,%3C%21doctype%20html%3E%3Ctitle%3EIsland%20Smoke%20"
    "Test%3C%2Ftitle%3E"
    "%3Ch1%20id%3D%22island-phase1-smoke-ok%22%3EISLAND_PHASE1_SMOKE_OK%3C%2Fh1%3E"
    "%3Ca%20href%3D%22%23island-phase1-smoke-history%22%3EAdvance%20smoke%20history%3C%2Fa%3E"
    "%3Cp%20id%3D%22island-phase1-smoke-history%22%3ESmoke%20history%20entry%3C%2Fp%3E";

class StartupOptions final {
  public:
    [[nodiscard]] static StartupOptions Parse(std::span<const std::string_view> arguments);

    // The smoke run's fixed page, or empty for production: BrowserWindow then
    // opens the built-in start page.
    [[nodiscard]] constexpr std::string_view initial_url() const {
        return smoke_test_ ? kSmokeTestInitialUrl : std::string_view();
    }

    [[nodiscard]] constexpr bool is_smoke_test() const { return smoke_test_; }

  private:
    explicit constexpr StartupOptions(bool smoke_test) : smoke_test_(smoke_test) {}

    bool smoke_test_;
};

}  // namespace island

#endif
