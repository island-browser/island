#ifndef ISLAND_MAIN_STARTUP_OPTIONS_H_
#define ISLAND_MAIN_STARTUP_OPTIONS_H_

#include <span>
#include <string_view>

namespace island {

inline constexpr std::string_view kProductionInitialUrl =
    "data:text/html;charset=utf-8,%3C%21doctype%20html%3E%3Ctitle%3EIsland%3C%2Ftitle%3E"
    "%3Ch1%3EIsland%20Browser%3C%2Fh1%3E";

inline constexpr std::string_view kSmokeTestInitialUrl =
    "data:text/html;charset=utf-8,%3C%21doctype%20html%3E%3Ctitle%3EIsland%20Smoke%20"
    "Test%3C%2Ftitle%3E"
    "%3Ch1%20id%3D%22island-phase1-smoke-ok%22%3EISLAND_PHASE1_SMOKE_OK%3C%2Fh1%3E"
    "%3Ca%20href%3D%22%23island-phase1-smoke-history%22%3EAdvance%20smoke%20history%3C%2Fa%3E"
    "%3Cp%20id%3D%22island-phase1-smoke-history%22%3ESmoke%20history%20entry%3C%2Fp%3E";

class StartupOptions final {
  public:
    [[nodiscard]] static StartupOptions Parse(std::span<const std::string_view> arguments);

    [[nodiscard]] constexpr std::string_view initial_url() const {
        return smoke_test_ ? kSmokeTestInitialUrl : kProductionInitialUrl;
    }

    [[nodiscard]] constexpr bool is_smoke_test() const { return smoke_test_; }

  private:
    explicit constexpr StartupOptions(bool smoke_test) : smoke_test_(smoke_test) {}

    bool smoke_test_;
};

}  // namespace island

#endif
